#include "JusPrinUtils.hpp"
#include "../GUI_App.hpp"

namespace Slic3r { namespace GUI {

wxString JusPrinUtils::GetJusPrinBaseUrl()
{
    wxString base_url = wxGetApp().app_config->get_with_default("jusprin_server", "base_url", "https://app.obico.io");

    // Remove trailing slash if present
    if (base_url.EndsWith("/")) {
        base_url = base_url.Left(base_url.Length() - 1);
    }

    return base_url;
}

}} // namespace Slic3r::GUI