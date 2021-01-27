#include "LibraryCheck.hpp"

#include <cstdio>
#include <boost/nowide/convert.hpp>

#ifdef  WIN32
#include <psapi.h>
# endif //WIN32

namespace Slic3r {

#ifdef  WIN32

//only dll name with .dll suffix - currently case sensitive
const std::vector<std::wstring> LibraryCheck::blacklist({ L"NahimicOSD.dll" });

bool LibraryCheck::get_blacklisted(std::vector<std::wstring>& names)
{
    if (m_found.empty())
        return false;
    for (const auto& lib : m_found)
        names.emplace_back(lib);
    return true;

}

std::wstring LibraryCheck::get_blacklisted_string()
{
    std::wstring ret;
    if (m_found.empty())
        return ret;
    //ret = L"These libraries has been detected inside of the PrusaSlicer process.\n"
    //    L"We suggest stopping or uninstalling these services if you experience crashes while using PrusaSlicer.\n\n";
    for (const auto& lib : m_found)
    {
        ret += lib;
        ret += L"\n";
    }
    return ret;
}

bool LibraryCheck::perform_check()
{
    
    DWORD   processID = GetCurrentProcessId();
    HMODULE hMods[1024];
    HANDLE  hProcess;
    DWORD   cbNeeded;
    unsigned int i;

    // Get a handle to the process.
    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
        PROCESS_VM_READ,
        FALSE, processID);
    if (NULL == hProcess)
        return false;
    // Get a list of all the modules in this process.
    if (EnumProcessModulesEx(hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL))
    {
        //printf("Total Dlls: %d\n", cbNeeded / sizeof(HMODULE));
        for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            TCHAR szModName[MAX_PATH];
            // Get the full path to the module's file.
            if (GetModuleFileNameEx(hProcess, hMods[i], szModName,
                sizeof(szModName) / sizeof(TCHAR)))
            {
                // Add to list if blacklisted
                if(LibraryCheck::is_blacklisted(szModName)) {
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

bool LibraryCheck::is_blacklisted(std::wstring dllpath)
{
    std::wstring dllname = boost::filesystem::path(dllpath).filename().wstring();
    //std::transform(dllname.begin(), dllname.end(), dllname.begin(), std::tolower);
    if (std::find(LibraryCheck::blacklist.begin(), LibraryCheck::blacklist.end(), dllname) != LibraryCheck::blacklist.end()) {
        //std::wprintf(L"%s is blacklisted\n", dllname.c_str());
        return true;
    }
    //std::wprintf(L"%s is NOT blacklisted\n", dllname.c_str());
    return false;
}
bool LibraryCheck::is_blacklisted(std::string dllpath)
{
    return LibraryCheck::is_blacklisted(boost::nowide::widen(dllpath));
}

#endif //WIN32

} // namespace Slic3r
