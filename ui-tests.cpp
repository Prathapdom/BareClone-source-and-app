#ifndef UNICODE
#define UNICODE
#endif
#include "search.hpp"
#include <commctrl.h>
#include <iostream>
#include <stdexcept>
DWORD targetPid=0;HWND app=nullptr;
BOOL CALLBACK findWindow(HWND h,LPARAM){DWORD pid=0;GetWindowThreadProcessId(h,&pid);wchar_t cls[100];GetClassNameW(h,cls,100);if(pid==targetPid && wcscmp(cls,L"BareClone.Main")==0){app=h;return FALSE;}return TRUE;}
void require(bool ok,const char*s){if(!ok)throw std::runtime_error(s);}
std::wstring label(int id){wchar_t out[2048];GetWindowTextW(GetDlgItem(app,id),out,2048);return out;}
bool waitLabel(const wchar_t* needle,DWORD timeout=10000){auto start=GetTickCount64();while(GetTickCount64()-start<timeout){if(label(307).find(needle)!=std::wstring::npos)return true;Sleep(30);}std::wcerr<<L"Last status: "<<label(307)<<L"\n";return false;}
std::wstring settings;
std::wstring field(int id){wchar_t out[2048]{};SendMessageW(GetDlgItem(app,id),WM_GETTEXT,2048,(LPARAM)out);return out;}
int count(){return (int)SendMessageW(GetDlgItem(app,116),LVM_GETITEMCOUNT,0,0);}
COLORREF listBack(){return (COLORREF)SendMessageW(GetDlgItem(app,116),LVM_GETBKCOLOR,0,0);}
void menu(int id){SendMessageW(app,WM_COMMAND,MAKEWPARAM(id,0),0);}
void click(int id){SendMessageW(app,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),(LPARAM)GetDlgItem(app,id));}
int wmain(int argc,wchar_t** argv){PROCESS_INFORMATION pi{};std::wstring root;
try{require(argc==2,"pass app executable path");wchar_t temp[32768];GetTempPathW(32768,temp);root=std::wstring(temp)+L"bareclone-ui-"+std::to_wstring(GetCurrentProcessId());CreateDirectoryW(root.c_str(),nullptr);
    FILE*f=_wfopen((root+L"\\test.txt").c_str(),L"wb");require(f!=nullptr,"create GUI fixture");fputs("alpha one\nalpha two\nBeta\n",f);fclose(f);
    settings=L"Software\\BareClone\\UITest-"+std::to_wstring(GetCurrentProcessId());SetEnvironmentVariableW(L"BARECLONE_SETTINGS_KEY",settings.c_str());
    std::wstring command=L"\""+std::wstring(argv[1])+L"\"";STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    require(CreateProcessW(argv[1],&command[0],nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi),"launch GUI");CloseHandle(pi.hThread);targetPid=pi.dwProcessId;
    for(int i=0;i<100&&!app;++i){EnumWindows(findWindow,0);Sleep(50);}require(app!=nullptr,"GUI window created");
    menu(432);require(listBack()==RGB(43,43,43),"dark theme recolours results");require(!(GetWindowLongPtrW(GetDlgItem(app,101),GWL_EXSTYLE)&WS_EX_CLIENTEDGE),"dark fields use flat outlines");
    menu(433);require(listBack()==RGB(0,0,0),"AMOLED theme paints results pure black");
    SendMessageW(GetDlgItem(app,102),WM_SETTEXT,0,(LPARAM)root.c_str());SendMessageW(GetDlgItem(app,101),WM_SETTEXT,0,(LPARAM)L"alpha");click(106);
    require(waitLabel(L"Search complete"),"literal search completes through worker IPC");require(SendMessageW(GetDlgItem(app,116),LVM_GETITEMCOUNT,0,0)==2,"virtual result list receives two rows");
    SendMessageW(GetDlgItem(app,111),BM_SETCHECK,BST_CHECKED,0);SendMessageW(GetDlgItem(app,101),WM_SETTEXT,0,(LPARAM)L"[");click(106);require(waitLabel(L"Invalid regular expression"),"invalid regex handled without GUI crash");
    SendMessageW(GetDlgItem(app,111),BM_SETCHECK,BST_UNCHECKED,0);
    f=_wfopen((root+L"\\large.txt").c_str(),L"wb");require(f!=nullptr,"create cancellation fixture");std::string block(1024*1024,'x');for(int i=0;i<32;++i)fwrite(block.data(),1,block.size(),f);fclose(f);
    SendMessageW(GetDlgItem(app,101),WM_SETTEXT,0,(LPARAM)L"not-present");click(106);
    DWORD_PTR response=0;require(SendMessageTimeoutW(app,WM_NULL,0,0,SMTO_ABORTIFHUNG,500,&response)!=0,"UI responds during search");click(107);require(waitLabel(L"Stopped",3000),"stop interrupts child promptly");
    SendMessageW(GetDlgItem(app,103),WM_SETTEXT,0,(LPARAM)L"test.txt");SendMessageW(GetDlgItem(app,101),WM_SETTEXT,0,(LPARAM)L"Beta");click(106);require(waitLabel(L"Search complete"),"search restarts after cancellation");require(SendMessageW(GetDlgItem(app,116),LVM_GETITEMCOUNT,0,0)==1,"restarted search has fresh results");
    SendMessageW(GetDlgItem(app,101),WM_SETTEXT,0,(LPARAM)L"changed");SendMessageW(GetDlgItem(app,103),WM_SETTEXT,0,(LPARAM)L"*");
    menu(400);require(field(101)==L"Beta"&&field(103)==L"test.txt","open recent restores the latest search");require(waitLabel(L"Search complete"),"recent search runs");require(count()==1,"recent search finds one row");
    menu(402);require(field(101)==L"["&&SendMessageW(GetDlgItem(app,111),BM_GETCHECK,0,0)==BST_CHECKED,"recent search restores regex option");require(waitLabel(L"Invalid regular expression"),"recent regex search runs");
    menu(403);require(field(101)==L"alpha"&&SendMessageW(GetDlgItem(app,111),BM_GETCHECK,0,0)==BST_UNCHECKED,"older recent search restores its options");require(waitLabel(L"Search complete"),"older recent search runs");require(count()==2,"older recent search finds two rows");
    wchar_t stored[8192];DWORD size=sizeof(stored);require(RegGetValueW(HKEY_CURRENT_USER,(settings+L"\\Recent").c_str(),L"3",RRF_RT_REG_SZ,nullptr,stored,&size)==ERROR_SUCCESS,"recent searches persist");
    menu(420);size=sizeof(stored);require(RegGetValueW(HKEY_CURRENT_USER,(settings+L"\\Recent").c_str(),L"0",RRF_RT_REG_SZ,nullptr,stored,&size)!=ERROR_SUCCESS,"clear removes recent searches");
    menu(431);require(listBack()==GetSysColor(COLOR_WINDOW),"light theme restores system list colours");
    DWORD theme=0;size=sizeof(theme);require(RegGetValueW(HKEY_CURRENT_USER,settings.c_str(),L"Theme",RRF_RT_REG_DWORD,nullptr,&theme,&size)==ERROR_SUCCESS&&theme==1,"theme choice persists");
    PostMessageW(app,WM_CLOSE,0,0);require(WaitForSingleObject(pi.hProcess,3000)==WAIT_OBJECT_0,"clean GUI shutdown");CloseHandle(pi.hProcess);pi.hProcess=nullptr;
    DeleteFileW((root+L"\\test.txt").c_str());DeleteFileW((root+L"\\large.txt").c_str());RemoveDirectoryW(root.c_str());RegDeleteTreeW(HKEY_CURRENT_USER,settings.c_str());
    std::cout<<"PASS: hidden GUI launch, worker IPC, virtual results, invalid regex, responsiveness, stop, restart, themes, open recent, shutdown\n";return 0;
}catch(const std::exception&e){if(pi.hProcess){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hProcess);}if(!settings.empty())RegDeleteTreeW(HKEY_CURRENT_USER,settings.c_str());std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
