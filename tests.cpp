#include "search.hpp"
#include <iostream>
#include <stdexcept>
using namespace search;
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void put(const std::wstring& path,const std::string& bytes){FILE*f=_wfopen(path.c_str(),L"wb");require(f!=nullptr,"create fixture");require(fwrite(bytes.data(),1,bytes.size(),f)==bytes.size(),"write fixture");fclose(f);}
std::vector<Result> run(Options o,Stats& s){std::vector<Result> rows;std::mutex m;Engine engine(o,s,[&](Result r){std::lock_guard<std::mutex> lock(m);rows.push_back(std::move(r));});engine.run();return rows;}
int main(){try{
    wchar_t temp[32768];GetTempPathW(32768,temp);auto root=std::wstring(temp)+L"bareclone-tests-"+std::to_wstring(GetCurrentProcessId());require(CreateDirectoryW(root.c_str(),nullptr)!=0,"create test directory");
    auto sub=root+L"\\sub",ignored=root+L"\\node_modules";CreateDirectoryW(sub.c_str(),nullptr);CreateDirectoryW(ignored.c_str(),nullptr);
    put(root+L"\\one.txt","Alpha one\r\nalphabet\nALPHA two\nlast alpha");put(sub+L"\\two.log","alpha\nBeta\n");put(ignored+L"\\ignore.txt","alpha\n");
    put(root+L"\\binary.bin",std::string("x\0alpha",7));put(root+L"\\unicode.txt","\xef\xbb\xbf"+utf8(L"\u6771\u4eac alpha\n"));
    std::wstring wide=L"Alpha wide\r\nGreek \u03a9\n";std::string wideBytes="\xff\xfe";wideBytes.append((const char*)wide.data(),wide.size()*sizeof(wchar_t));put(root+L"\\wide.txt",wideBytes);
    Options o;o.root=root;o.query=L"alpha";Stats s;auto r=run(o,s);require(r.size()==7,"literal case-insensitive, UTF BOM, recursion, exclusion");require(s.skipped==1,"binary skip");
    o.wholeWord=true;Stats w;require(run(o,w).size()==6,"whole-word boundary");
    o.matchCase=true;Stats c;require(run(o,c).size()==3,"case sensitive");
    o.matchCase=false;o.wholeWord=false;o.include=L"*.log";Stats g;require(run(o,g).size()==1,"filename filter");
    o.recursive=false;Stats n;require(run(o,n).empty(),"non-recursive search");
    o.include=L"*.txt";o.query=L"^Alpha [a-z]+$";o.regex=true;Stats rx;require(run(o,rx).size()==3,"regex anchors and CRLF");
    o.query=L"[";bool rejected=false;try{Matcher m(o);}catch(const std::regex_error&){rejected=true;}require(rejected,"invalid regex rejected");
    o.regex=false;o.query=L"\u03a9";Stats u;require(run(o,u).size()==1,"UTF16 decoding");
    o.query=L"\u03c9";Stats ul;require(run(o,ul).size()==1,"Unicode case folding");
    require(word(L'\u03a9') && !word(L'\u3002'),"Unicode word boundaries");
    o.query=L"alpha";o.include=L"*";Stats stopped;stopped.cancelled=true;require(run(o,stopped).empty() && stopped.done,"pre-cancelled search");
    require(glob(L"a*?.txt",L"ABC.txt") && !glob(L"*.log",L"a.txt"),"wildcard semantics");
    put(root+L"\\long.txt",std::string(MaxLine+1,'x')+"\nalpha\n");o.include=L"long.txt";Stats big;require(run(o,big).size()==1 && big.longLines==1,"oversized line bound and recovery");
    std::string bigFile;for(int i=0;i<100005;++i)bigFile+="alpha\n";put(root+L"\\cap.txt",bigFile);o.include=L"cap.txt";Stats cap;require(run(o,cap).size()==MaxResults && cap.capped,"result memory bound");
    for(auto name:{L"one.txt",L"binary.bin",L"unicode.txt",L"wide.txt",L"long.txt",L"cap.txt"})DeleteFileW((root+L"\\"+name).c_str());DeleteFileW((sub+L"\\two.log").c_str());DeleteFileW((ignored+L"\\ignore.txt").c_str());RemoveDirectoryW(sub.c_str());RemoveDirectoryW(ignored.c_str());RemoveDirectoryW(root.c_str());
    std::cout<<"PASS: literal, whole-word, case, filters, recursion, exclusions, binary detection, UTF8/UTF16, regex, cancellation, long lines, result cap\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
