#ifndef slic3r_GUI_JusPrinUtils_hpp_
#define slic3r_GUI_JusPrinUtils_hpp_

#include <wx/string.h>

namespace Slic3r { namespace GUI {

class JusPrinUtils
{
public:
    // Get the JusPrin server base URL, removing trailing slash if present
    static wxString GetJusPrinBaseUrl();
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_JusPrinUtils_hpp_