#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace search {
constexpr size_t MaxResults = 100000;
constexpr size_t MaxResultBytes = 64 * 1024 * 1024;
constexpr size_t MaxLine = 1024 * 1024;
struct Options {
    std::wstring root, query, include = L"*", exclude = L".git;node_modules;.svn;.hg";
    bool recursive = true, matchCase = false, wholeWord = false, regex = false;
    unsigned workers = 4;
};
struct Result { std::wstring path, text; uint64_t line; };
struct Stats {
    std::atomic<uint64_t> files{0}, bytes{0}, skipped{0}, errors{0}, matches{0}, longLines{0}, resultBytes{0};
    std::atomic<bool> cancelled{false}, done{false}, capped{false};
};
inline std::wstring lower(std::wstring s) {
    bool ascii=true;
    for(auto& c:s) {if(c>=128){ascii=false;break;}if(c>=L'A' && c<=L'Z')c+=L'a'-L'A';}
    if(!ascii && !s.empty()) {
        int n=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr,0);
        if(n>0){std::wstring mapped(n,L' ');LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),(int)s.size(),&mapped[0],n,nullptr,nullptr,0);return mapped;}
    }
    return s;
}
inline std::wstring decode(const std::string& s) {
    if(s.empty()) return {};
    UINT cp=CP_UTF8; int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(!n) { cp=CP_ACP; n=MultiByteToWideChar(cp,0,s.data(),(int)s.size(),nullptr,0); }
    std::wstring out(n,L' '); if(n) MultiByteToWideChar(cp,0,s.data(),(int)s.size(),&out[0],n); return out;
}
inline std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string out(n,' '); WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),&out[0],n,nullptr,nullptr); return out;
}
inline std::vector<std::wstring> split(const std::wstring& s) {
    std::vector<std::wstring> out; size_t p=0;
    while(p<s.size()) { size_t e=s.find(L';',p); if(e==s.npos)e=s.size(); auto v=s.substr(p,e-p);
        size_t a=v.find_first_not_of(L" \t"), b=v.find_last_not_of(L" \t");
        if(a!=v.npos)out.push_back(lower(v.substr(a,b-a+1))); p=e+1; }
    return out;
}
// Iterative wildcard matching, constant stack use even for hostile patterns.
inline bool glob(const std::wstring& pattern,const std::wstring& name) {
    const auto folded=lower(name);
    size_t p=0,n=0,star=std::wstring::npos,retry=0;
    while(n<name.size()) {
        if(p<pattern.size() && (pattern[p]==L'?' || pattern[p]==folded[n])) {++p;++n;}
        else if(p<pattern.size() && pattern[p]==L'*') {star=p++;retry=n;}
        else if(star!=std::wstring::npos) {p=star+1;n=++retry;}
        else return false;
    }
    while(p<pattern.size() && pattern[p]==L'*')++p; return p==pattern.size();
}
inline bool anyGlob(const std::vector<std::wstring>& patterns,const std::wstring& name) {
    for(auto& p:patterns) if(glob(p,name)) return true; return false;
}
inline bool word(wchar_t c) {
    if(c<128)return c==L'_' || (c>=L'0' && c<=L'9') || (c>=L'A' && c<=L'Z') || (c>=L'a' && c<=L'z');
    WORD type=0;return GetStringTypeW(CT_CTYPE1,&c,1,&type) && (type&(C1_ALPHA|C1_DIGIT));
}
class Matcher {
    Options options; std::wstring needle; std::wregex expression;
public:
    explicit Matcher(const Options& o):options(o),needle(o.matchCase?o.query:lower(o.query)) {
        if(o.regex) expression=std::wregex(o.query,std::regex::ECMAScript | (o.matchCase?std::regex::flag_type{}:std::regex::icase));
    }
    bool matches(const std::wstring& line) const {
        if(options.regex) {
            for(std::wsregex_iterator it(line.begin(),line.end(),expression),end;it!=end;++it) {
                size_t p=it->position(),len=it->length();
                if(!options.wholeWord || ((p==0 || !word(line[p-1])) && (p+len==line.size() || !word(line[p+len]))))return true;
            }
            return false;
        }
        auto folded=options.matchCase?line:lower(line); size_t p=0;
        while((p=folded.find(needle,p))!=folded.npos) {
            if(!options.wholeWord || ((p==0 || !word(line[p-1])) && (p+needle.size()==line.size() || !word(line[p+needle.size()]))))return true;
            ++p;
        }
        return false;
    }
};
// Regex runs in a disposable child process in the GUI. Literal search stays in-process.
class Engine {
    Options options; Stats& stats;
    std::function<void(Result)> emit;
    std::mutex mutex; std::condition_variable available, space;
    std::deque<std::wstring> queue; bool enumerationDone=false;
    std::vector<std::wstring> includes,excludes;
    bool stopped() const {return stats.cancelled || stats.capped;}
    void visit(const std::wstring& root) {
        std::vector<std::wstring> dirs{root};
        while(!dirs.empty() && !stopped()) {
            auto dir=std::move(dirs.back()); dirs.pop_back();
            WIN32_FIND_DATAW entry; HANDLE h=FindFirstFileExW((dir+L"\\*").c_str(),FindExInfoBasic,&entry,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH);
            if(h==INVALID_HANDLE_VALUE) {++stats.errors;continue;}
            do {
                if(stopped()) break;
                std::wstring name=entry.cFileName;
                if(name==L"." || name==L".." || anyGlob(excludes,name))continue;
                auto path=dir+L"\\"+name;
                if(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {++stats.skipped;continue;}
                if(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {if(options.recursive)dirs.push_back(path);continue;}
                if(!anyGlob(includes,name))continue;
                std::unique_lock<std::mutex> lock(mutex);
                space.wait_for(lock,std::chrono::milliseconds(50),[&]{return queue.size()<256 || stopped();});
                while(queue.size()>=256 && !stopped())space.wait_for(lock,std::chrono::milliseconds(50));
                if(stopped())break;
                queue.push_back(std::move(path)); available.notify_one();
            } while(FindNextFileW(h,&entry));
            DWORD error=GetLastError(); if(error!=ERROR_NO_MORE_FILES && !stopped())++stats.errors;
            FindClose(h);
        }
    }
    void scan(const std::wstring& path,const Matcher& matcher) {
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(f==INVALID_HANDLE_VALUE){++stats.errors;return;}
        std::vector<unsigned char> buf(256*1024); DWORD count=0; bool first=true,wide=false,be=false,discard=false; int pending=-1;
        std::string line; std::wstring wline; uint64_t number=1;
        auto flush=[&] {
            if(discard){++stats.longLines;discard=false;line.clear();wline.clear();++number;return;}
            auto text=wide?std::move(wline):decode(line); line.clear(); wline.clear();
            if(!text.empty() && text.back()==L'\r')text.pop_back();
            // Bound regex input: std::regex has recursive implementations on some toolchains.
            if(options.regex && text.size()>4096){++stats.longLines;++number;return;}
            if(!stopped() && matcher.matches(text)) {
                auto index=stats.matches.fetch_add(1);
                if(index<MaxResults) {
                    if(text.size()>2000)text.resize(2000);
                    size_t cost=sizeof(Result)+(path.size()+text.size())*sizeof(wchar_t);
                    if(stats.resultBytes.fetch_add(cost)+cost>MaxResultBytes){stats.capped=true;}
                    else emit(Result{path,std::move(text),number});
                }
                else {stats.matches=MaxResults;stats.capped=true;}
            }
            ++number;
        };
        while(!stopped()) {
            if(!ReadFile(f,buf.data(),(DWORD)buf.size(),&count,nullptr)){++stats.errors;break;}
            if(count==0)break; stats.bytes+=count;
            size_t begin=0;
            if(first) {
                first=false;
                if(count>=2 && ((buf[0]==0xff && buf[1]==0xfe)||(buf[0]==0xfe && buf[1]==0xff))) {wide=true;be=buf[0]==0xfe;begin=2;}
                else {
                    if(std::find(buf.begin(),buf.begin()+count,0)!=buf.begin()+count){++stats.skipped;CloseHandle(f);return;}
                    if(count>=3 && buf[0]==0xef && buf[1]==0xbb && buf[2]==0xbf)begin=3;
                }
            }
            for(size_t i=begin;i<count && !stopped();++i) {
                if(wide) {
                    if(pending<0){pending=buf[i];continue;}
                    wchar_t ch=(wchar_t)(be?(pending*256+buf[i]):(pending+256*buf[i]));pending=-1;
                    if(ch==L'\n')flush(); else if(!discard){wline.push_back(ch);if(wline.size()>MaxLine){discard=true;wline.clear();}}
                } else {
                    char ch=(char)buf[i]; if(ch=='\n')flush(); else if(!discard){line.push_back(ch);if(line.size()>MaxLine){discard=true;line.clear();}}
                }
            }
        }
        if(!stopped() && (!line.empty() || !wline.empty() || discard))flush();
        CloseHandle(f);++stats.files;
    }
public:
    Engine(Options o, Stats& s,std::function<void(Result)> sink):options(std::move(o)),stats(s),emit(std::move(sink)),includes(split(options.include)),excludes(split(options.exclude)) {
        if(includes.empty())includes.push_back(L"*");
    }
    void run() {
        Matcher validate(options);
        unsigned count=std::max(1u,std::min(8u,options.workers)); std::vector<std::thread> workers;
        for(unsigned i=0;i<count;++i)workers.emplace_back([&]{
            try {
                Matcher matcher(options);
                while(!stopped()) {
                    std::wstring path;
                    { std::unique_lock<std::mutex> lock(mutex); available.wait_for(lock,std::chrono::milliseconds(50),[&]{return !queue.empty() || enumerationDone || stopped();});
                        if(stopped() || (queue.empty() && enumerationDone))break;
                        if(queue.empty())continue;
                        path=std::move(queue.front());queue.pop_front();space.notify_one(); }
                    scan(path,matcher);
                }
            } catch(...) {++stats.errors; stats.cancelled=true;}
        });
        try {visit(options.root);} catch(...) {++stats.errors;stats.cancelled=true;}
        {std::lock_guard<std::mutex> lock(mutex);enumerationDone=true;} available.notify_all();
        for(auto& w:workers)w.join();stats.done=true;
    }
};
}
