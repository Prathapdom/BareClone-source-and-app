#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include "search.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <memory>
#include <sstream>
#include <iomanip>

using namespace search;
namespace wire {
bool write(HANDLE h,const void* data,size_t size) { auto p=(const char*)data; while(size){DWORD n=0;if(!WriteFile(h,p,(DWORD)std::min(size,size_t(65536)),&n,nullptr)||!n)return false;p+=n;size-=n;}return true; }
bool read(HANDLE h,void* data,size_t size) { auto p=(char*)data;while(size){DWORD n=0;if(!ReadFile(h,p,(DWORD)std::min(size,size_t(65536)),&n,nullptr)||!n)return false;p+=n;size-=n;}return true; }
template<class T>bool put(HANDLE h,const T& v){return write(h,&v,sizeof(v));}
template<class T>bool get(HANDLE h,T& v){return read(h,&v,sizeof(v));}
bool putString(HANDLE h,const std::wstring& s){uint32_t n=(uint32_t)s.size();return put(h,n)&&write(h,s.data(),n*sizeof(wchar_t));}
bool getString(HANDLE h,std::wstring& s,uint32_t limit=32768){uint32_t n=0;if(!get(h,n)||n>limit)return false;s.resize(n);return !n||read(h,&s[0],n*sizeof(wchar_t));}
struct Snapshot { uint64_t files,bytes,skipped,errors,matches,longLines; uint32_t capped; };
Snapshot snapshot(const Stats& s){return {s.files.load(),s.bytes.load(),s.skipped.load(),s.errors.load(),s.matches.load(),s.longLines.load(),s.capped?1u:0u};}
}
int workerMain() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HANDLE in=GetStdHandle(STD_INPUT_HANDLE),out=GetStdHandle(STD_OUTPUT_HANDLE);
    Options o;uint32_t flags=0;
    if(!wire::getString(in,o.root)||!wire::getString(in,o.query)||!wire::getString(in,o.include)||!wire::getString(in,o.exclude)||!wire::get(in,flags))return 2;
    o.recursive=flags&1;o.matchCase=flags&2;o.wholeWord=flags&4;o.regex=flags&8;
    Stats stats;std::mutex pipeMutex;std::atomic<bool> complete{false};
    auto status=[&]{std::lock_guard<std::mutex> lock(pipeMutex);uint32_t tag=2;auto s=wire::snapshot(stats);wire::put(out,tag);wire::put(out,s);};
    std::thread reporter([&]{while(!complete){status();std::this_thread::sleep_for(std::chrono::milliseconds(150));}});
    int result=0;
    try { Engine engine(o,stats,[&](Result r){std::lock_guard<std::mutex> lock(pipeMutex);uint32_t tag=1;if(!wire::put(out,tag)||!wire::putString(out,r.path)||!wire::putString(out,r.text)||!wire::put(out,r.line))stats.cancelled=true;});engine.run(); }
    catch(const std::regex_error&){result=3;}
    catch(...){result=4;}
    complete=true;reporter.join();status();return result;
}

enum { Query=101, Root, Includes, Excludes, Browse, Search, Stop, Recursive, Case, Whole, Regex, Export, Open, Reveal, Copy, Results, Preview, RecentMenu, ThemeMenu };
enum { RecentFirst=400, RecentClear=420, ThemeSystem=430, ThemeLight, ThemeDark, ThemeAmoled };
HWND window=nullptr,list=nullptr,preview=nullptr,statusLabel=nullptr,summary=nullptr;
HFONT font=nullptr,titleFont=nullptr,monoFont=nullptr;
HBRUSH background=nullptr;
std::shared_ptr<std::vector<Result>> rows=std::make_shared<std::vector<Result>>();
std::deque<Result> pending;
std::mutex pendingMutex;std::condition_variable queueSpace;
wire::Snapshot progress{};
std::atomic<bool> readerDone{true},cancelled{false},exportDone{true};
std::atomic<DWORD> exitCode{0};std::atomic<bool> exportOK{false};
std::thread reader,exporter;
HANDLE job=nullptr,child=nullptr;
bool running=false,exporting=false,closeAfterExport=false;
ULONGLONG started=0;int shownCount=0;
int scale=96;
int px(int n){return MulDiv(n,scale,96);}
HWND control(int id){return GetDlgItem(window,id);}
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,&s[0],n+1);s.resize(n);return s;}
void setStatus(const std::wstring& s){SetWindowTextW(statusLabel,s.c_str());}

// Settings live under HKCU. BARECLONE_SETTINGS_KEY lets the UI tests use a throwaway key.
std::wstring settingsKey(){wchar_t key[256];DWORD n=GetEnvironmentVariableW(L"BARECLONE_SETTINGS_KEY",key,256);return n&&n<256?std::wstring(key):std::wstring(L"Software\\BareClone");}
DWORD readSetting(const wchar_t* name,DWORD fallback){DWORD v=0,size=sizeof(v);return RegGetValueW(HKEY_CURRENT_USER,settingsKey().c_str(),name,RRF_RT_REG_DWORD,nullptr,&v,&size)==ERROR_SUCCESS?v:fallback;}
void writeSetting(const wchar_t* name,DWORD v){HKEY k;if(RegCreateKeyExW(HKEY_CURRENT_USER,settingsKey().c_str(),0,nullptr,0,KEY_WRITE,nullptr,&k,nullptr))return;RegSetValueExW(k,name,0,REG_DWORD,(const BYTE*)&v,sizeof(v));RegCloseKey(k);}

// Appearance. Light keeps the classic themed controls; Dark and AMOLED draw flat fields and buttons.
enum ThemeMode : DWORD { SystemTheme, LightTheme, DarkTheme, AmoledTheme };
struct Palette { bool dark; COLORREF window,surface,text,muted,border,button,hot,pressed,accent,accentHot; };
const Palette lightPalette{false,RGB(247,248,250),RGB(255,255,255),RGB(35,43,54),RGB(35,43,54),RGB(200,204,210),RGB(255,255,255),RGB(240,240,240),RGB(230,230,230),RGB(0,103,192),RGB(0,90,170)};
const Palette darkPalette{true,RGB(32,32,32),RGB(43,43,43),RGB(240,240,240),RGB(160,160,160),RGB(72,72,72),RGB(55,55,55),RGB(66,66,66),RGB(45,45,45),RGB(0,103,192),RGB(24,124,212)};
const Palette amoledPalette{true,RGB(0,0,0),RGB(0,0,0),RGB(232,232,232),RGB(140,140,140),RGB(54,54,54),RGB(12,12,12),RGB(30,30,30),RGB(0,0,0),RGB(0,95,184),RGB(18,118,210)};
ThemeMode themeMode=SystemTheme;Palette pal=lightPalette;HBRUSH surfaceBrush=nullptr;int fontHeight=16;
std::vector<std::pair<int,RECT>> fields; // Dark modes: outlined field rectangles painted by the main window.
bool isField(int id){return id==Query||id==Root||id==Includes||id==Excludes||id==Results||id==Preview;}
void invalidateField(int id){for(auto&f:fields)if(f.first==id){RECT r=f.second;InflateRect(&r,1,1);InvalidateRect(window,&r,FALSE);}}

HWND add(const wchar_t* cls,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h,DWORD ex=0) {
    HWND c=CreateWindowExW(ex,cls,label,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;
}
void move(int id,int x,int y,int w,int h){
    if(!pal.dark || !isField(id)){MoveWindow(control(id),px(x),px(y),px(std::max(1,w)),px(std::max(1,h)),TRUE);return;}
    RECT r{px(x),px(y),px(x)+px(std::max(1,w)),px(y)+px(std::max(1,h))};int inset=id==Results?px(2):px(6);
    bool found=false;for(auto&f:fields)if(f.first==id){f.second=r;found=true;}if(!found)fields.push_back({id,r});
    int ix=r.left+inset,iy=r.top+inset,iw=r.right-r.left-2*inset,ih=r.bottom-r.top-2*inset;
    if(id!=Results && id!=Preview){ix=r.left+px(8);iw=r.right-r.left-px(16);ih=std::min<int>(r.bottom-r.top-px(4),fontHeight+px(4));iy=r.top+(r.bottom-r.top-ih)/2;}
    MoveWindow(control(id),ix,iy,std::max(1,iw),std::max(1,ih),TRUE);
}
void layout() {
    RECT rc;GetClientRect(window,&rc);int w=MulDiv(rc.right,96,scale),h=MulDiv(rc.bottom,96,scale);
    move(RecentMenu,w-234,24,100,30);move(ThemeMenu,w-124,24,100,30);
    move(Query,24,103,w-250,30);move(Search,w-210,102,88,32);move(Stop,w-112,102,88,32);
    move(Root,24,164,w-154,29);move(Browse,w-118,163,94,31);
    move(Includes,24,225,(w-60)/2,28);move(Excludes,36+(w-60)/2,225,(w-60)/2,28);
    MoveWindow(control(304),px(36+(w-60)/2),px(205),px(400),px(18),TRUE);
    int listHeight=std::max(100,h-475);
    move(Results,24,322,w-48,listHeight);
    move(Preview,24,330+listHeight,w-48,95);
    move(Open,24,h-44,100,28);move(Reveal,134,h-44,116,28);move(Copy,260,h-44,100,28);move(Export,w-136,h-44,112,28);
    MoveWindow(statusLabel,px(374),px(h-41),px(std::max(10,w-530)),px(24),TRUE);
    ListView_SetColumnWidth(list,0,px(std::max(200,(w-48)*36/100)));ListView_SetColumnWidth(list,1,px(75));ListView_SetColumnWidth(list,2,px(std::max(250,(w-48)*64/100-85)));
    if(pal.dark)InvalidateRect(window,nullptr,TRUE);
}
void enableSearchControls(bool enabled) {
    for(int id:{Query,Root,Includes,Excludes,Browse,Search,Recursive,Case,Whole,Regex})EnableWindow(control(id),enabled);
    EnableWindow(control(Stop),!enabled && running);EnableWindow(control(Export),enabled && !rows->empty());
}
void requestStop(){if(!running)return;cancelled=true;queueSpace.notify_all();if(job)TerminateJobObject(job,ERROR_CANCELLED);setStatus(L"Stopping search…");EnableWindow(control(Stop),FALSE);}
bool checked(int id){return SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED;}

// Recent searches: newest first, stored as unit-separator-joined REG_SZ values "0".."9".
struct Recent { std::wstring root,query,include,exclude; uint32_t flags;
    bool operator==(const Recent& o)const{return root==o.root&&query==o.query&&include==o.include&&exclude==o.exclude&&flags==o.flags;} };
constexpr size_t MaxRecent=10;constexpr wchar_t RecentSeparator=L'\x1f';
std::vector<Recent> recents;
void loadRecents(){recents.clear();HKEY k;if(RegOpenKeyExW(HKEY_CURRENT_USER,(settingsKey()+L"\\Recent").c_str(),0,KEY_READ,&k))return;
    for(size_t i=0;i<MaxRecent;++i){auto name=std::to_wstring(i);DWORD type=0,size=0;if(RegQueryValueExW(k,name.c_str(),nullptr,&type,nullptr,&size)||type!=REG_SZ||size>262144)break;
        std::wstring v(size/sizeof(wchar_t)+1,L'\0');if(RegQueryValueExW(k,name.c_str(),nullptr,nullptr,(BYTE*)&v[0],&size))break;v.resize(wcsnlen(v.c_str(),v.size()));
        std::vector<std::wstring> parts(1);for(wchar_t c:v){if(c==RecentSeparator)parts.emplace_back();else parts.back()+=c;}
        if(parts.size()==5 && !parts[0].empty() && !parts[1].empty())recents.push_back({parts[0],parts[1],parts[2],parts[3],(uint32_t)wcstoul(parts[4].c_str(),nullptr,10)&15u});}
    RegCloseKey(k);}
void saveRecents(){auto path=settingsKey()+L"\\Recent";RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());if(recents.empty())return;HKEY k;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&k,nullptr))return;
    for(size_t i=0;i<recents.size();++i){auto&r=recents[i];std::wstring v=r.root+RecentSeparator+r.query+RecentSeparator+r.include+RecentSeparator+r.exclude+RecentSeparator+std::to_wstring(r.flags);
        RegSetValueExW(k,std::to_wstring(i).c_str(),0,REG_SZ,(const BYTE*)v.c_str(),(DWORD)((v.size()+1)*sizeof(wchar_t)));}
    RegCloseKey(k);}
void addRecent(const Recent& r){for(auto* s:{&r.root,&r.query,&r.include,&r.exclude})if(s->find(RecentSeparator)!=std::wstring::npos)return;
    recents.erase(std::remove(recents.begin(),recents.end(),r),recents.end());recents.insert(recents.begin(),r);if(recents.size()>MaxRecent)recents.resize(MaxRecent);saveRecents();}
void startSearch();
void applyRecent(size_t i){if(running||exporting||i>=recents.size())return;Recent r=recents[i];
    SetWindowTextW(control(Root),r.root.c_str());SetWindowTextW(control(Query),r.query.c_str());SetWindowTextW(control(Includes),r.include.c_str());SetWindowTextW(control(Excludes),r.exclude.c_str());
    int bit=1;for(int id:{Recursive,Case,Whole,Regex}){SendMessageW(control(id),BM_SETCHECK,(r.flags&bit)?BST_CHECKED:BST_UNCHECKED,0);bit<<=1;}
    startSearch();}
void startSearch() {
    if(running||exporting)return;
    Options o;o.root=text(control(Root));o.query=text(control(Query));o.include=text(control(Includes));o.exclude=text(control(Excludes));
    while(o.root.size()>3 && (o.root.back()==L'\\'||o.root.back()==L'/'))o.root.pop_back();
    if(o.query.empty()){MessageBoxW(window,L"Enter text or a regular expression to search for.",L"Search text needed",MB_OK|MB_ICONINFORMATION);SetFocus(control(Query));return;}
    DWORD attrs=GetFileAttributesW(o.root.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY)){MessageBoxW(window,L"Choose an existing folder that you can read.",L"Folder unavailable",MB_OK|MB_ICONWARNING);return;}
    o.recursive=checked(Recursive);o.matchCase=checked(Case);o.wholeWord=checked(Whole);o.regex=checked(Regex);
    // Parse regex in a child, never on the UI thread.
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE inputRead=nullptr,inputWrite=nullptr,outputRead=nullptr,outputWrite=nullptr;
    if(!CreatePipe(&inputRead,&inputWrite,&sa,0)||!CreatePipe(&outputRead,&outputWrite,&sa,0)) {
        if(inputRead)CloseHandle(inputRead);if(inputWrite)CloseHandle(inputWrite);MessageBoxW(window,L"Could not create search pipes.",L"Search failed",MB_OK|MB_ICONERROR);return;
    }
    SetHandleInformation(inputWrite,HANDLE_FLAG_INHERIT,0);SetHandleInformation(outputRead,HANDLE_FLAG_INHERIT,0);
    job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit))){if(job)CloseHandle(job);job=nullptr;for(auto h:{inputRead,inputWrite,outputRead,outputWrite})CloseHandle(h);MessageBoxW(window,L"Could not create an isolated search job.",L"Search failed",MB_OK|MB_ICONERROR);return;}
    wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);std::wstring command=L"\""+std::wstring(executable)+L"\" --worker";
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=inputRead;si.hStdOutput=outputWrite;si.hStdError=outputWrite;
    PROCESS_INFORMATION pi{};
    BOOL ok=CreateProcessW(executable,&command[0],nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
    if(ok && !AssignProcessToJobObject(job,pi.hProcess)){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);ok=FALSE;}
    CloseHandle(inputRead);CloseHandle(outputWrite);
    if(!ok){CloseHandle(inputWrite);CloseHandle(outputRead);CloseHandle(job);job=nullptr;MessageBoxW(window,L"Could not start the search process.",L"Search failed",MB_OK|MB_ICONERROR);return;}
    child=pi.hProcess;ResumeThread(pi.hThread);CloseHandle(pi.hThread);
    addRecent({o.root,o.query,o.include,o.exclude,(o.recursive?1u:0u)|(o.matchCase?2u:0u)|(o.wholeWord?4u:0u)|(o.regex?8u:0u)});
    rows=std::make_shared<std::vector<Result>>();rows->reserve(4096);ListView_SetItemCountEx(list,0,0);shownCount=0;
    SetWindowTextW(preview,L"Select a result to preview its matching line. Double-click to open the file.");
    {std::lock_guard<std::mutex> lock(pendingMutex);pending.clear();progress={};}
    cancelled=false;readerDone=false;exitCode=0;running=true;started=GetTickCount64();enableSearchControls(false);setStatus(L"Searching…");
    reader=std::thread([o,inputWrite,outputRead]{
        uint32_t flags=(o.recursive?1:0)|(o.matchCase?2:0)|(o.wholeWord?4:0)|(o.regex?8:0);
        bool sent=wire::putString(inputWrite,o.root)&&wire::putString(inputWrite,o.query)&&wire::putString(inputWrite,o.include)&&wire::putString(inputWrite,o.exclude)&&wire::put(inputWrite,flags);CloseHandle(inputWrite);
        if(sent)while(!cancelled){uint32_t tag=0;if(!wire::get(outputRead,tag))break;
            if(tag==1){Result r;if(!wire::getString(outputRead,r.path)||!wire::getString(outputRead,r.text,2000)||!wire::get(outputRead,r.line))break;
                std::unique_lock<std::mutex> lock(pendingMutex);queueSpace.wait(lock,[]{return pending.size()<2048||cancelled;});if(cancelled)break;pending.push_back(std::move(r));}
            else if(tag==2){wire::Snapshot s{};if(!wire::get(outputRead,s))break;std::lock_guard<std::mutex> lock(pendingMutex);progress=s;}
            else break;
        }
        CloseHandle(outputRead);
        // Killing the job interrupts a pathological regex and blocked file reads.
        if(cancelled || !sent)TerminateJobObject(job,ERROR_CANCELLED);
        WaitForSingleObject(child,INFINITE);DWORD code=0;GetExitCodeProcess(child,&code);exitCode=code;readerDone=true;
    });
}
int selected(){return ListView_GetNextItem(list,-1,LVNI_SELECTED);}
void showPreview(){int n=selected();if(n<0 || (size_t)n>=rows->size())return;auto& r=(*rows)[n];auto s=r.path+L"  •  Line "+std::to_wstring(r.line)+L"\r\n\r\n"+r.text;SetWindowTextW(preview,s.c_str());}
void openSelected(bool reveal){int n=selected();if(n<0 || (size_t)n>=rows->size())return;auto& r=(*rows)[n];
    if(reveal){std::wstring args=L"/select,\""+r.path+L"\"";ShellExecuteW(window,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOWNORMAL);}
    else {std::wstring args=L"\""+r.path+L"\"";auto result=(INT_PTR)ShellExecuteW(window,L"open",L"notepad.exe",args.c_str(),nullptr,SW_SHOWNORMAL);if(result<=32)MessageBoxW(window,L"Windows could not open this file in Notepad.",L"Open file",MB_OK|MB_ICONINFORMATION);}
}
void copySelected(){int n=selected();if(n<0 || (size_t)n>=rows->size())return;auto&r=(*rows)[n];auto s=r.path+L":"+std::to_wstring(r.line)+L": "+r.text;
    if(!OpenClipboard(window))return;HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(s.size()+1)*sizeof(wchar_t));
    if(memory){void* p=GlobalLock(memory);if(p){memcpy(p,s.c_str(),(s.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}CloseClipboard();
}
void browse(){IFileOpenDialog* dialog=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))){
        DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(L"Choose a folder to search");
        if(SUCCEEDED(dialog->Show(window))){IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){SetWindowTextW(control(Root),path);CoTaskMemFree(path);}item->Release();}}dialog->Release();}
}
std::string csv(const std::wstring& s){auto u=utf8(s);std::string out="\"";for(char c:u){if(c=='\"')out+='\"';out+=c;}return out+'\"';}
void exportCSV(){if(running||exporting||rows->empty())return;wchar_t path[32768]=L"BareClone-results.csv";OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;ofn.lpstrFilter=L"CSV files\0*.csv\0\0";ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.lpstrDefExt=L"csv";ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    if(!GetSaveFileNameW(&ofn))return;exporting=true;exportDone=false;enableSearchControls(false);EnableWindow(control(Stop),FALSE);setStatus(L"Exporting CSV…");auto data=rows;std::wstring destination=path;
    exporter=std::thread([data,destination]{FILE* f=_wfopen(destination.c_str(),L"wb");bool ok=f!=nullptr;if(f){const char* header="\xef\xbb\xbf" "File,Line,Text\r\n";ok=fwrite(header,1,strlen(header),f)==strlen(header);
        for(auto&r:*data){auto line=csv(r.path)+","+std::to_string(r.line)+","+csv(r.text)+"\r\n";if(fwrite(line.data(),1,line.size(),f)!=line.size()){ok=false;break;}}
        if(fclose(f))ok=false;}exportOK=ok;exportDone=true;});
}
void tick(){
    if(exporting && exportDone){exporter.join();exporting=false;enableSearchControls(true);setStatus(exportOK?L"CSV exported.":L"CSV export failed. Check disk space and permissions.");if(closeAfterExport)PostMessageW(window,WM_CLOSE,0,0);}
    if(!running)return;
    wire::Snapshot s{};bool empty=false;
    {std::lock_guard<std::mutex> lock(pendingMutex);for(int i=0;i<1000 && !pending.empty();++i){rows->push_back(std::move(pending.front()));pending.pop_front();}s=progress;empty=pending.empty();}queueSpace.notify_all();
    if((int)rows->size()!=shownCount){shownCount=(int)rows->size();ListView_SetItemCountEx(list,shownCount,LVSICF_NOINVALIDATEALL|LVSICF_NOSCROLL);InvalidateRect(list,nullptr,FALSE);}
    double elapsed=(GetTickCount64()-started)/1000.0;std::wostringstream detail;
    detail<<rows->size()<<L" matching lines  ·  "<<s.files<<L" files  ·  "<<std::fixed<<std::setprecision(1)<<s.bytes/1048576.0<<L" MiB  ·  "<<elapsed<<L" s";
    SetWindowTextW(summary,detail.str().c_str());
    if(readerDone && empty){reader.join();CloseHandle(child);child=nullptr;CloseHandle(job);job=nullptr;running=false;enableSearchControls(true);
        std::wstring message=cancelled?L"Stopped":(exitCode==3?L"Invalid regular expression":(exitCode!=0?L"Search process failed":(s.capped?L"Result limit reached — narrow your search":L"Search complete")));
        message+=L" · "+std::to_wstring(s.skipped)+L" skipped · "+std::to_wstring(s.errors)+L" errors";if(s.longLines)message+=L" · "+std::to_wstring(s.longLines)+L" oversized lines skipped";setStatus(message);
    }else if(!cancelled)setStatus(L"Searching…");
}
bool systemPrefersDark(){HIGHCONTRASTW hc{sizeof(hc),0,nullptr};if(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0)&&(hc.dwFlags&HCF_HIGHCONTRASTON))return false;
    DWORD v=1,size=sizeof(v);return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&v,&size)==ERROR_SUCCESS&&v==0;}
DWORD windowsBuild(){using Fn=LONG(WINAPI*)(OSVERSIONINFOW*);OSVERSIONINFOW v{};v.dwOSVersionInfoSize=sizeof(v);auto fn=(Fn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");return fn&&fn(&v)==0?v.dwBuildNumber:0;}
void setMenuTheme(bool dark){
    // Popup menus follow uxtheme ordinal 135 (SetPreferredAppMode) and 136 (FlushMenuThemes), stable since Windows 10 1903.
    static DWORD build=windowsBuild();static HMODULE ux=LoadLibraryExW(L"uxtheme.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(build<18362||!ux)return;
    auto setMode=(int(WINAPI*)(int))(void*)GetProcAddress(ux,MAKEINTRESOURCEA(135));auto flush=(void(WINAPI*)())(void*)GetProcAddress(ux,MAKEINTRESOURCEA(136));
    if(setMode)setMode(dark?2:3);if(flush)flush();}
void applyTheme(){
    ThemeMode effective=themeMode==SystemTheme?(systemPrefersDark()?DarkTheme:LightTheme):themeMode;
    pal=effective==AmoledTheme?amoledPalette:effective==DarkTheme?darkPalette:lightPalette;
    HBRUSH oldBackground=background,oldSurface=surfaceBrush;background=CreateSolidBrush(pal.window);surfaceBrush=CreateSolidBrush(pal.surface);
    SetClassLongPtrW(window,GCLP_HBRBACKGROUND,(LONG_PTR)background);if(oldBackground)DeleteObject(oldBackground);if(oldSurface)DeleteObject(oldSurface);
    BOOL dark=pal.dark;if(FAILED(DwmSetWindowAttribute(window,20,&dark,sizeof(dark))))DwmSetWindowAttribute(window,19,&dark,sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE (19 before 20H1)
    COLORREF caption=effective==AmoledTheme?RGB(0,0,0):0xFFFFFFFF,frame=effective==AmoledTheme?pal.border:0xFFFFFFFF; // Windows 11 caption/border colour, else ignored
    DwmSetWindowAttribute(window,35,&caption,sizeof(caption));DwmSetWindowAttribute(window,34,&frame,sizeof(frame));
    setMenuTheme(pal.dark);
    for(int id:{Query,Root,Includes,Excludes,Results,Preview}){HWND c=control(id);LONG_PTR ex=GetWindowLongPtrW(c,GWL_EXSTYLE);
        SetWindowLongPtrW(c,GWL_EXSTYLE,pal.dark?(ex&~WS_EX_CLIENTEDGE):(ex|WS_EX_CLIENTEDGE));SetWindowPos(c,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
    const wchar_t* explorer=pal.dark?L"DarkMode_Explorer":nullptr;
    SetWindowTheme(list,explorer,nullptr);SetWindowTheme(ListView_GetHeader(list),pal.dark?L"DarkMode_ItemsView":nullptr,nullptr);SetWindowTheme(preview,explorer,nullptr);
    if(HWND tip=ListView_GetToolTips(list))SetWindowTheme(tip,explorer,nullptr);
    COLORREF listBack=pal.dark?pal.surface:GetSysColor(COLOR_WINDOW);ListView_SetBkColor(list,listBack);ListView_SetTextBkColor(list,listBack);ListView_SetTextColor(list,pal.dark?pal.text:GetSysColor(COLOR_WINDOWTEXT));
    fields.clear();layout();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
}
void setTheme(ThemeMode mode){themeMode=mode;writeSetting(L"Theme",mode);applyTheme();}
void roundRect(HDC dc,const RECT& r,COLORREF fill,COLORREF line,int radius){HBRUSH b=CreateSolidBrush(fill);HPEN p=CreatePen(PS_SOLID,1,line);auto ob=SelectObject(dc,b);auto op=SelectObject(dc,p);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);DeleteObject(p);}
void paintFields(HDC dc){HWND focus=GetFocus();for(auto&f:fields)roundRect(dc,f.second,pal.surface,focus==control(f.first)?pal.accentHot:pal.border,px(6));}
LRESULT drawButton(NMCUSTOMDRAW* cd){
    if(!pal.dark || cd->dwDrawStage!=CDDS_PREPAINT)return CDRF_DODEFAULT;
    HWND b=cd->hdr.hwndFrom;HDC dc=cd->hdc;RECT rc=cd->rc;FillRect(dc,&rc,background);
    bool disabled=!IsWindowEnabled(b),pressed=cd->uItemState&CDIS_SELECTED,hot=cd->uItemState&CDIS_HOT,focus=cd->uItemState&CDIS_FOCUS,cues=cd->uItemState&CDIS_SHOWKEYBOARDCUES;
    wchar_t label[128];GetWindowTextW(b,label,128);SetBkMode(dc,TRANSPARENT);auto oldFont=SelectObject(dc,font);UINT prefix=cues?0:DT_HIDEPREFIX;
    if((GetWindowLongW(b,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX){
        bool on=SendMessageW(b,BM_GETCHECK,0,0)==BST_CHECKED;int box=px(16);RECT r{rc.left+px(1),rc.top+(rc.bottom-rc.top-box)/2,0,0};r.right=r.left+box;r.bottom=r.top+box;
        COLORREF fill=on?(hot?pal.accentHot:pal.accent):pal.surface;roundRect(dc,r,fill,on?fill:disabled?pal.border:hot?pal.text:pal.muted,px(5));
        if(on){HPEN p=CreatePen(PS_SOLID,std::max(1,px(2)),RGB(255,255,255));auto op=SelectObject(dc,p);
            POINT tick[]={{r.left+box*4/16,r.top+box*8/16},{r.left+box*7/16,r.top+box*11/16},{r.left+box*12/16,r.top+box*5/16}};Polyline(dc,tick,3);SelectObject(dc,op);DeleteObject(p);}
        RECT t{r.right+px(8),rc.top,rc.right,rc.bottom};SetTextColor(dc,disabled?pal.muted:pal.text);DrawTextW(dc,label,-1,&t,DT_SINGLELINE|DT_VCENTER|prefix);
        if(focus && cues){RECT f=t;DrawTextW(dc,label,-1,&f,DT_SINGLELINE|DT_CALCRECT|prefix);OffsetRect(&f,0,(t.bottom-t.top-(f.bottom-f.top))/2);InflateRect(&f,px(2),px(1));DrawFocusRect(dc,&f);}
    } else {
        bool primary=GetDlgCtrlID(b)==Search && !disabled;
        COLORREF fill=primary?(pressed?pal.accent:hot?pal.accentHot:pal.accent):(pressed?pal.pressed:hot?pal.hot:pal.button);
        roundRect(dc,rc,fill,focus&&cues?pal.text:primary?fill:pal.border,px(8));
        SetTextColor(dc,disabled?pal.muted:primary?RGB(255,255,255):pal.text);DrawTextW(dc,label,-1,&rc,DT_SINGLELINE|DT_VCENTER|DT_CENTER|prefix);
    }
    SelectObject(dc,oldFont);return CDRF_SKIPDEFAULT;
}
// The results header notifies the list view, not the main window, so the list is subclassed to paint it in dark modes.
LRESULT CALLBACK listProc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_NOTIFY && pal.dark){auto n=(NMHDR*)lp;HWND header=ListView_GetHeader(h);
        if(n->hwndFrom==header && n->code==NM_CUSTOMDRAW){auto cd=(NMCUSTOMDRAW*)lp;
            if(cd->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(cd->dwDrawStage==CDDS_ITEMPREPAINT){RECT r=cd->rc;HDC dc=cd->hdc;FillRect(dc,&r,surfaceBrush);
                HPEN p=CreatePen(PS_SOLID,1,pal.border);auto op=SelectObject(dc,p);MoveToEx(dc,r.left,r.bottom-1,nullptr);LineTo(dc,r.right,r.bottom-1);
                MoveToEx(dc,r.right-1,r.top+px(5),nullptr);LineTo(dc,r.right-1,r.bottom-px(5));SelectObject(dc,op);DeleteObject(p);
                wchar_t label[64]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=label;item.cchTextMax=64;Header_GetItem(header,(int)cd->dwItemSpec,&item);
                SetBkMode(dc,TRANSPARENT);SetTextColor(dc,pal.muted);auto oldFont=SelectObject(dc,font);r.left+=px(8);r.right-=px(4);
                DrawTextW(dc,label,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(dc,oldFont);return CDRF_SKIPDEFAULT;}
        }}
    return DefSubclassProc(h,msg,wp,lp);
}
std::wstring menuText(std::wstring s,size_t limit,bool keepEnd){
    if(s.size()>limit)s=keepEnd?s.substr(0,3)+L"…"+s.substr(s.size()-(limit-4)):s.substr(0,limit-1)+L"…";
    std::wstring out;for(wchar_t c:s){if(c==L'&')out+=L'&';out+=(c==L'\t'||c==L'\r'||c==L'\n')?L' ':c;}return out;}
void popup(HMENU m,int anchor){RECT r;GetWindowRect(control(anchor),&r);TPMPARAMS tp{sizeof(tp),r};TrackPopupMenuEx(m,TPM_LEFTALIGN|TPM_TOPALIGN|TPM_VERTICAL,r.left,r.bottom,window,&tp);DestroyMenu(m);}
void showRecentMenu(){HMENU m=CreatePopupMenu();UINT state=(running||exporting)?MF_GRAYED:0;
    if(recents.empty())AppendMenuW(m,MF_STRING|MF_GRAYED,0,L"No recent searches yet");
    for(size_t i=0;i<recents.size();++i){auto&r=recents[i];std::wstring label=L"&"+std::to_wstring((i+1)%10)+L"   "+menuText(r.query,48,false);
        if(r.include!=L"*")label+=L"   ["+menuText(r.include,24,false)+L"]";label+=L"\t"+menuText(r.root,56,true);AppendMenuW(m,MF_STRING|state,RecentFirst+i,label.c_str());}
    if(!recents.empty()){AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING|state,RecentClear,L"&Clear recent searches");}
    popup(m,RecentMenu);}
void showThemeMenu(){HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,ThemeSystem,L"Use &system setting");AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m,MF_STRING,ThemeLight,L"&Light");AppendMenuW(m,MF_STRING,ThemeDark,L"&Dark");AppendMenuW(m,MF_STRING,ThemeAmoled,L"&AMOLED black");
    CheckMenuRadioItem(m,ThemeSystem,ThemeAmoled,ThemeSystem+themeMode,MF_BYCOMMAND);popup(m,ThemeMenu);}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){switch(msg){
case WM_CREATE:{window=h;HDC dc=GetDC(h);scale=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(h,dc);
    font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    titleFont=CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    monoFont=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
    auto title=add(L"STATIC",L"BareClone",0,300,24,16,300,37);SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);
    add(L"STATIC",L"Find the line. Keep your flow.",0,301,26,57,700,22);
    add(L"STATIC",L"SEARCH TEXT OR REGULAR EXPRESSION",0,302,24,82,600,19);
    add(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,Query,24,103,680,30,WS_EX_CLIENTEDGE);
    add(L"BUTTON",L"Search",WS_TABSTOP|BS_DEFPUSHBUTTON,Search,720,103,88,32);add(L"BUTTON",L"Stop",WS_TABSTOP,Stop,820,103,88,32);
    add(L"STATIC",L"FOLDER",0,303,24,143,600,19);wchar_t cwd[32768];GetCurrentDirectoryW(32768,cwd);
    add(L"EDIT",cwd,WS_TABSTOP|ES_AUTOHSCROLL,Root,24,164,700,29,WS_EX_CLIENTEDGE);add(L"BUTTON",L"Browse…",WS_TABSTOP,Browse,800,164,94,31);
    add(L"STATIC",L"FILE PATTERNS  (separate with ;)",0,305,24,205,430,18);add(L"STATIC",L"EXCLUDE NAMES  (files and folders)",0,304,480,205,430,18);
    add(L"EDIT",L"*",WS_TABSTOP|ES_AUTOHSCROLL,Includes,24,225,430,28,WS_EX_CLIENTEDGE);add(L"EDIT",L".git;node_modules;.svn;.hg",WS_TABSTOP|ES_AUTOHSCROLL,Excludes,480,225,430,28,WS_EX_CLIENTEDGE);
    add(L"BUTTON",L"Subfolders",WS_TABSTOP|BS_AUTOCHECKBOX,Recursive,24,265,125,24);SendMessageW(control(Recursive),BM_SETCHECK,BST_CHECKED,0);
    add(L"BUTTON",L"Match case",WS_TABSTOP|BS_AUTOCHECKBOX,Case,169,265,125,24);add(L"BUTTON",L"Whole word",WS_TABSTOP|BS_AUTOCHECKBOX,Whole,314,265,130,24);add(L"BUTTON",L"Regex",WS_TABSTOP|BS_AUTOCHECKBOX,Regex,464,265,110,24);
    summary=add(L"STATIC",L"Ready. Search UTF-8, UTF-16, and local-codepage text files.",0,306,24,300,1000,20);
    list=add(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS,Results,24,322,900,200,WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    const wchar_t* columns[]={L"File",L"Line",L"Matching text"};for(int i=0;i<3;++i){LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=const_cast<wchar_t*>(columns[i]);col.cx=px(i==1?75:400);ListView_InsertColumn(list,i,&col);}
    preview=add(L"EDIT",L"Select a result to preview its matching line. Double-click to open the file.",WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,Preview,24,530,900,95,WS_EX_CLIENTEDGE);SendMessageW(preview,WM_SETFONT,(WPARAM)monoFont,TRUE);
    add(L"BUTTON",L"Open file",WS_TABSTOP,Open,24,640,100,28);add(L"BUTTON",L"Show in folder",WS_TABSTOP,Reveal,134,640,116,28);add(L"BUTTON",L"Copy result",WS_TABSTOP,Copy,260,640,100,28);add(L"BUTTON",L"Export CSV…",WS_TABSTOP,Export,800,640,112,28);
    statusLabel=add(L"STATIC",L"Ready",SS_PATHELLIPSIS,307,374,644,410,24);
    for(int id:{Query,Root,Includes,Excludes})SendMessageW(control(id),EM_SETLIMITTEXT,id==Query?4096:8192,0);
    add(L"BUTTON",L"&Recent ▾",WS_TABSTOP,RecentMenu,800,24,100,30);add(L"BUTTON",L"T&heme ▾",WS_TABSTOP,ThemeMenu,910,24,100,30);
    {HDC mdc=GetDC(h);auto old=SelectObject(mdc,font);TEXTMETRICW tm{};GetTextMetricsW(mdc,&tm);fontHeight=tm.tmHeight;SelectObject(mdc,old);ReleaseDC(h,mdc);}
    SetWindowSubclass(list,listProc,1,0);loadRecents();themeMode=(ThemeMode)std::min<DWORD>(readSetting(L"Theme",SystemTheme),AmoledTheme);
    enableSearchControls(true);SetTimer(h,1,60,nullptr);applyTheme();SetFocus(control(Query));return 0;}
case WM_SIZE:if(list)layout();return 0;
case WM_GETMINMAXINFO: {auto m=(MINMAXINFO*)lp;m->ptMinTrackSize.x=px(960);m->ptMinTrackSize.y=px(680);return 0;}
case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);if(pal.dark)paintFields(dc);EndPaint(h,&ps);return 0;}
case WM_LBUTTONDOWN:{POINT pt{(short)LOWORD(lp),(short)HIWORD(lp)};for(auto&f:fields)if(PtInRect(&f.second,pt)){SetFocus(control(f.first));break;}return 0;}
case WM_SETTINGCHANGE:if(themeMode==SystemTheme && lp && !lstrcmpW((LPCWSTR)lp,L"ImmersiveColorSet"))applyTheme();break;
case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;int id=GetDlgCtrlID((HWND)lp);
    if(pal.dark && isField(id)){SetBkColor(dc,pal.surface);SetTextColor(dc,pal.text);return (LRESULT)surfaceBrush;} // read-only preview and disabled fields
    SetBkColor(dc,pal.window);SetTextColor(dc,id>=301&&id<=305?pal.muted:pal.text);return (LRESULT)background;}
case WM_CTLCOLOREDIT:if(!pal.dark)break;SetBkColor((HDC)wp,pal.surface);SetTextColor((HDC)wp,pal.text);return (LRESULT)surfaceBrush;
case WM_CTLCOLORBTN:if(!pal.dark)break;return (LRESULT)background;
case WM_COMMAND:{int id=LOWORD(wp),code=HIWORD(wp);
    if(code==EN_SETFOCUS||code==EN_KILLFOCUS){invalidateField(id);return 0;}
    if(id>=RecentFirst && id<RecentFirst+(int)MaxRecent){applyRecent(id-RecentFirst);return 0;}
    if(id>=ThemeSystem && id<=ThemeAmoled){setTheme((ThemeMode)(id-ThemeSystem));return 0;}
    switch(id){case Search:startSearch();break;case Stop:requestStop();break;case Browse:browse();break;case Open:openSelected(false);break;case Reveal:openSelected(true);break;case Copy:copySelected();break;case Export:exportCSV();break;
    case RecentMenu:showRecentMenu();break;case ThemeMenu:showThemeMenu();break;case RecentClear:if(!running&&!exporting){recents.clear();saveRecents();setStatus(L"Recent searches cleared");}break;}return 0;}
case WM_NOTIFY:{auto n=(NMHDR*)lp;
    if(n->code==NM_CUSTOMDRAW){wchar_t cls[16];GetClassNameW(n->hwndFrom,cls,16);if(!lstrcmpiW(cls,L"Button"))return drawButton((NMCUSTOMDRAW*)lp);}
    if(n->hwndFrom==list && (n->code==NM_SETFOCUS||n->code==NM_KILLFOCUS)){invalidateField(Results);if(pal.dark)InvalidateRect(list,nullptr,FALSE);}
    if(n->hwndFrom==list && n->code==NM_CUSTOMDRAW && pal.dark){auto cd=(NMLVCUSTOMDRAW*)lp; // the dark theme's selection is nearly invisible on black
        if(cd->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
        if(cd->nmcd.dwDrawStage==CDDS_ITEMPREPAINT && ListView_GetItemState(list,(int)cd->nmcd.dwItemSpec,LVIS_SELECTED)){
            cd->nmcd.uItemState&=~(CDIS_SELECTED|CDIS_FOCUS);cd->clrTextBk=GetFocus()==list?RGB(0,84,153):pal.hot;cd->clrText=RGB(255,255,255);return CDRF_NEWFONT;}
        return CDRF_DODEFAULT;}
    if(n->hwndFrom==list){
    if(n->code==LVN_GETDISPINFOW){auto info=(NMLVDISPINFOW*)lp;int i=info->item.iItem;if(i>=0&&(size_t)i<rows->size()&&(info->item.mask&LVIF_TEXT)){auto&r=(*rows)[i];static thread_local std::wstring number;const wchar_t* s=nullptr;if(info->item.iSubItem==0)s=r.path.c_str();else if(info->item.iSubItem==1){number=std::to_wstring(r.line);s=number.c_str();}else s=r.text.c_str();lstrcpynW(info->item.pszText,s,info->item.cchTextMax);}return 0;}
    if(n->code==LVN_ITEMCHANGED)showPreview();if(n->code==NM_DBLCLK)openSelected(false);
}break;}
case WM_TIMER:tick();return 0;
case WM_CLOSE:if(exporting){closeAfterExport=true;setStatus(L"Finishing CSV export before closing…");return 0;}requestStop();if(reader.joinable())reader.join();if(exporter.joinable())exporter.join();if(child)CloseHandle(child);if(job)CloseHandle(job);DestroyWindow(h);return 0;
case WM_DESTROY:KillTimer(h,1);PostQuitMessage(0);return 0;
}return DefWindowProcW(h,msg,wp,lp);}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool worker=argc==2 && wcscmp(argv[1],L"--worker")==0;LocalFree(argv);if(worker)return workerMain();
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&cc);
    background=CreateSolidBrush(RGB(247,248,250));WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=proc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));cls.hbrBackground=background;cls.lpszClassName=L"BareClone.Main";RegisterClassExW(&cls);
    HWND h=CreateWindowExW(0,cls.lpszClassName,L"BareClone — native file search",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1160,800,nullptr,nullptr,instance,nullptr);if(!h)return 1;
    ShowWindow(h,show);UpdateWindow(h);MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){
        if(msg.message==WM_KEYDOWN){if(msg.wParam==VK_ESCAPE){requestStop();continue;}if(msg.wParam==VK_F5){startSearch();continue;}if(msg.wParam==VK_RETURN && GetFocus()==control(Query)){startSearch();continue;}
            if(msg.wParam=='C' && (GetKeyState(VK_CONTROL)&0x8000) && GetFocus()==list){copySelected();continue;}}
        if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    DeleteObject(font);DeleteObject(titleFont);DeleteObject(monoFont);DeleteObject(background);if(surfaceBrush)DeleteObject(surfaceBrush);CoUninitialize();return (int)msg.wParam;
}
