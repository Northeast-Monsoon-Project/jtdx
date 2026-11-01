// Preserve existing JTDX-F shortcuts while the real Windows process uses
// jtdx.exe, the basename required by JTAlert's application discovery.
#include <windows.h>
#include <string>
#include <vector>

namespace {
int failure(DWORD code)
{
  wchar_t text[1024]{};
  if(!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr,code,0,text,1024,nullptr))
    MessageBoxW(nullptr,std::to_wstring(code).c_str(),L"JTDX-F",MB_OK|MB_ICONERROR);
  else MessageBoxW(nullptr,text,L"JTDX-F",MB_OK|MB_ICONERROR);
  return 1;
}
}

int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int show)
{
  std::vector<wchar_t> module(32768);
  DWORD length=GetModuleFileNameW(nullptr,module.data(),DWORD(module.size()));
  if(!length)return failure(GetLastError());
  if(length>=module.size())return failure(ERROR_INSUFFICIENT_BUFFER);
  std::wstring path(module.data(),length);
  auto separator=path.find_last_of(L"\\/");
  if(separator==std::wstring::npos)return failure(ERROR_BAD_PATHNAME);
  path.resize(separator+1);path+=L"jtdx.exe";

  // Preserve the original Windows argument tail verbatim, including quoted
  // Unicode rig names. Only replace argv[0] with the actual application.
  auto tail=GetCommandLineW();
  while(*tail==L' ' || *tail==L'\t')++tail;
  if(*tail==L'"') {++tail;while(*tail && *tail!=L'"')++tail;if(*tail)++tail;}
  else while(*tail && *tail!=L' ' && *tail!=L'\t')++tail;
  std::wstring command=L"\""+path+L"\""+tail;
  std::vector<wchar_t> writable(command.begin(),command.end());writable.push_back(0);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);
  startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=WORD(show);
  PROCESS_INFORMATION child{};
  if(!CreateProcessW(path.c_str(),writable.data(),nullptr,nullptr,FALSE,0,nullptr,
                     nullptr,&startup,&child))return failure(GetLastError());
  CloseHandle(child.hThread);CloseHandle(child.hProcess);
  return 0;
}
