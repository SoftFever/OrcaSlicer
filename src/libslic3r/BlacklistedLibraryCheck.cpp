#include "BlacklistedLibraryCheck.hpp"

#include <cstdio>
#include <boost/nowide/convert.hpp>

#ifdef  WIN32
#include <psapi.h>
# endif //WIN32

namespace Slic3r {

#ifdef  WIN32

//only dll name with .dll suffix - currently case sensitive
const std::vector<std::wstring> BlacklistedLibraryCheck::blacklist({ L"NahimicOSD.dll" });

bool BlacklistedLibraryCheck::get_blacklisted(std::vector<std::wstring>& names)
{
    if (m_found.empty())
        return false;
    for (const auto& lib : m_found)
        names.emplace_back(lib);
    return true;
}

std::wstring BlacklistedLibraryCheck::get_blacklisted_string()
{
    std::wstring ret;
    for (const auto& lib : m_found)
        ret += lib + L"\n";
    return ret;
}

bool BlacklistedLibraryCheck::perform_check()
{   
    // Get a handle to the process.
    HANDLE  hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId());
    if (NULL == hProcess)
        return false;

    // Get a list of all the modules in this process.
    HMODULE hMods[1024];
    DWORD   cbNeeded;
    if (EnumProcessModulesEx(hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL))
    {
        //printf("Total Dlls: %d\n", cbNeeded / sizeof(HMODULE));
        for (unsigned int i = 0; i < cbNeeded / sizeof(HMODULE); ++ i)
        {
            wchar_t szModName[MAX_PATH];
            // Get the full path to the module's file.
            if (GetModuleFileNameExW(hProcess, hMods[i], szModName, MAX_PATH))
            {
                // Add to list if blacklisted
                if (BlacklistedLibraryCheck::is_blacklisted(szModName)) {
                    //wprintf(L"Contains library: %s\n", szModName);
                    if (std::find(m_found.begin(), m_found.end(), szModName) == m_found.end())
                        m_found.emplace_back(szModName);
                } 
                //wprintf(L"%s\n", szModName);
            }
        }
    }

    CloseHandle(hProcess);
    //printf("\n");
    return !m_found.empty();
}

bool BlacklistedLibraryCheck::is_blacklisted(const std::wstring &dllpath)
{
    std::wstring dllname = boost::filesystem::path(dllpath).filename().wstring();
    //std::transform(dllname.begin(), dllname.end(), dllname.begin(), std::tolower);
    if (std::find(BlacklistedLibraryCheck::blacklist.begin(), BlacklistedLibraryCheck::blacklist.end(), dllname) != BlacklistedLibraryCheck::blacklist.end()) {
        //std::wprintf(L"%s is blacklisted\n", dllname.c_str());
        return true;
    }
    //std::wprintf(L"%s is NOT blacklisted\n", dllname.c_str());
    return false;
}
bool BlacklistedLibraryCheck::is_blacklisted(const std::string &dllpath)
{
    return BlacklistedLibraryCheck::is_blacklisted(boost::nowide::widen(dllpath));
}

#endif //WIN32

} // namespace Slic3r
