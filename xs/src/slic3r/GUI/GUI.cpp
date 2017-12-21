#include "GUI.hpp"

#include <assert.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#if __APPLE__
#import <IOKit/pwr_mgt/IOPMLib.h>
#elif _WIN32
#include <Windows.h>
#include "boost/nowide/convert.hpp"
#pragma comment(lib, "user32.lib")
#endif

#include <wx/app.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

#if __APPLE__
IOPMAssertionID assertionID;
#endif

void disable_screensaver()
{
    #if __APPLE__
    CFStringRef reasonForActivity = CFSTR("Slic3r");
    IOReturn success = IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, 
        kIOPMAssertionLevelOn, reasonForActivity, &assertionID); 
    // ignore result: success == kIOReturnSuccess
    #elif _WIN32
    SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
    #endif
}

void enable_screensaver()
{
    #if __APPLE__
    IOReturn success = IOPMAssertionRelease(assertionID);
    #elif _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
    #endif
}

std::vector<std::string> scan_serial_ports()
{
    std::vector<std::string> out;
#ifdef _WIN32
    // 1) Open the registry key SERIALCOM.
    HKEY hKey;
    LONG lRes = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey);
    assert(lRes == ERROR_SUCCESS);
    if (lRes == ERROR_SUCCESS) {
        // 2) Get number of values of SERIALCOM key.
        DWORD        cValues;                   // number of values for key 
        {
            TCHAR    achKey[255];               // buffer for subkey name
            DWORD    cbName;                    // size of name string 
            TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
            DWORD    cchClassName = MAX_PATH;   // size of class string 
            DWORD    cSubKeys=0;                // number of subkeys 
            DWORD    cbMaxSubKey;               // longest subkey size 
            DWORD    cchMaxClass;               // longest class string 
            DWORD    cchMaxValue;               // longest value name 
            DWORD    cbMaxValueData;            // longest value data 
            DWORD    cbSecurityDescriptor;      // size of security descriptor 
            FILETIME ftLastWriteTime;           // last write time 
            // Get the class name and the value count.
            lRes = RegQueryInfoKey(
                hKey,                    // key handle 
                achClass,                // buffer for class name 
                &cchClassName,           // size of class string 
                NULL,                    // reserved 
                &cSubKeys,               // number of subkeys 
                &cbMaxSubKey,            // longest subkey size 
                &cchMaxClass,            // longest class string 
                &cValues,                // number of values for this key 
                &cchMaxValue,            // longest value name 
                &cbMaxValueData,         // longest value data 
                &cbSecurityDescriptor,   // security descriptor 
                &ftLastWriteTime);       // last write time
            assert(lRes == ERROR_SUCCESS);
        }
        // 3) Read the SERIALCOM values.
        {
            DWORD dwIndex = 0;
            for (int i = 0; i < cValues; ++ i, ++ dwIndex) {
                wchar_t valueName[2048];
                DWORD	valNameLen = 2048;
                DWORD	dataType;
				wchar_t data[2048];
				DWORD	dataSize = 4096;
				lRes = ::RegEnumValueW(hKey, dwIndex, valueName, &valNameLen, nullptr, &dataType, (BYTE*)&data, &dataSize);
                if (lRes == ERROR_SUCCESS && dataType == REG_SZ && valueName[0] != 0)
					out.emplace_back(boost::nowide::narrow(data));
            }
        }
        ::RegCloseKey(hKey);
    }
#else
    // UNIX and OS X
    std::initializer_list<const char*> prefixes { "ttyUSB" , "ttyACM", "tty.", "cu.", "rfcomm" };
    for (auto &dir_entry : boost::filesystem::directory_iterator(boost::filesystem::path("/dev"))) {
        std::string name = dir_entry.path().filename().string();
        for (const char *prefix : prefixes) {
            if (boost::starts_with(name, prefix)) {
                out.emplace_back(dir_entry.path().string());
                break;
            }
        }
    }
#endif

    out.erase(std::remove_if(out.begin(), out.end(), 
        [](const std::string &key){ 
            return boost::starts_with(key, "Bluetooth") || boost::starts_with(key, "FireFly"); 
        }),
        out.end());
    return out;
}

bool debugged()
{
    #ifdef _WIN32
    return IsDebuggerPresent();
	#else
	return false;
    #endif /* _WIN32 */
}

void break_to_debugger()
{
    #ifdef _WIN32
    if (IsDebuggerPresent())
        DebugBreak();
    #endif /* _WIN32 */
}

// Passing the wxWidgets GUI classes instantiated by the Perl part to C++.
wxApp       *g_wxApp        = nullptr;
wxFrame     *g_wxMainFrame  = nullptr;
wxNotebook  *g_wxTabPanel   = nullptr;

void set_wxapp(wxApp *app)
{
    g_wxApp = app;
}

void set_main_frame(wxFrame *main_frame)
{
    g_wxMainFrame = main_frame;
}

void set_tab_panel(wxNotebook *tab_panel)
{
    g_wxTabPanel = tab_panel;
}

void add_debug_menu(wxMenuBar *menu)
{
#if 0
    auto debug_menu = new wxMenu();
    debug_menu->Append(wxWindow::NewControlId(1), "Some debug");
    menu->Append(debug_menu, _T("&Debug"));
#endif
}

void create_preset_tab(const char *name)
{
    auto  *panel  = new wxPanel(g_wxTabPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    // Vertical sizer to hold the choice menu and the rest of the page.
    auto  *sizer  = new wxBoxSizer(wxVERTICAL);
    sizer->SetSizeHints(panel);
    panel->SetSizer(sizer);
    auto  *button = new wxButton(panel, wxID_ANY, "Hello World", wxDefaultPosition, wxDefaultSize, 0);
    sizer->Add(button, 0, 0, 0);
    g_wxTabPanel->AddPage(panel, name);
}

} }
