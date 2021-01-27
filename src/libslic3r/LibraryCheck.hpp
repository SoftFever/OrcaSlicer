#ifndef slic3r_LibraryCheck_hpp_
#define slic3r_LibraryCheck_hpp_

#ifdef  WIN32
#include <windows.h>
#include <vector>
#include <string>
#endif //WIN32

namespace Slic3r {

#ifdef  WIN32
class LibraryCheck
{
public:
    static LibraryCheck& get_instance()
    {
        static LibraryCheck instance; 
                              
        return instance;
    }
private:
    LibraryCheck() {}

    std::vector<std::wstring> m_found;
public:
    LibraryCheck(LibraryCheck const&) = delete;
    void operator=(LibraryCheck const&) = delete;
    // returns all found blacklisted dlls
    bool get_blacklisted(std::vector<std::wstring>& names);
    std::wstring get_blacklisted_string();
    // returns true if enumerating found blacklisted dll
    bool perform_check();

    static bool is_blacklisted(std::string  dllpath);
    static bool is_blacklisted(std::wstring dllpath);
private:
    static const std::vector<std::wstring> blacklist;
};

#endif //WIN32

} // namespace Slic3r

#endif //slic3r_LibraryCheck_hpp_