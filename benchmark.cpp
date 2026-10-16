#include "search.hpp"
#include <iostream>
using namespace search;
int main(){wchar_t temp[32768];GetTempPathW(32768,temp);auto root=std::wstring(temp)+L"bareclone-bench-"+std::to_wstring(GetCurrentProcessId());if(!CreateDirectoryW(root.c_str(),nullptr))return 1;
    std::string line="2026-10-16 INFO request completed status=200 service=payments duration_ms=18 resource=example operational log entry";line.resize(127,' ');line+='\n';std::string block;
    for(int i=0;i<8192;++i)block+=line;block.replace(20,15,"BARECLONE_TOKEN");
    bool ok=true;for(int i=0;i<128;++i){auto file=root+L"\\"+std::to_wstring(i)+L".log";FILE*f=_wfopen(file.c_str(),L"wb");if(!f){ok=false;break;}if(fwrite(block.data(),1,block.size(),f)!=block.size())ok=false;fclose(f);}
    int result=0;
    if(ok){Options o;o.root=root;o.query=L"BARECLONE_TOKEN";o.include=L"*.log";
        for(bool sensitive:{false,true}){o.matchCase=sensitive;Stats stats;std::atomic<unsigned> hits{0};auto start=std::chrono::steady_clock::now();Engine e(o,stats,[&](Result){++hits;});e.run();double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::cout<<(sensitive?"Case sensitive":"Case insensitive")<<": "<<stats.files<<" files, "<<stats.bytes/1048576.0<<" MiB, "<<hits<<" matching lines, "<<seconds<<" s, "<<(stats.bytes/1048576.0)/seconds<<" MiB/s\n";
            if(hits!=128 || stats.files!=128 || stats.errors)result=1;}}
    else result=1;
    for(int i=0;i<128;++i)DeleteFileW((root+L"\\"+std::to_wstring(i)+L".log").c_str());RemoveDirectoryW(root.c_str());return result;
}
