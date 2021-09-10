#include "SendSystemInfoDialog.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/format.hpp"
#include "slic3r/Utils/Http.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "OpenGLManager.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/split.hpp>

#include "GL/glew.h"

#include <wx/htmllbox.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

// Declaration of a free function defined in OpenGLManager.cpp:
std::string gl_get_string_safe(GLenum param, const std::string& default_value);



class ShowJsonDialog : public wxDialog
{
public:
    ShowJsonDialog(wxWindow* parent, const wxString& json, const wxSize& size)
        : wxDialog(parent, wxID_ANY, _L("Data to send"), wxDefaultPosition, size, wxCAPTION)
    {
        auto* text = new wxTextCtrl(this, wxID_ANY, json,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
        text->SetFont(wxGetApp().code_font());
        text->ShowPosition(0);

        auto* btn = new wxButton(this, wxID_CANCEL, _L("Close"));
        auto* vsizer = new wxBoxSizer(wxVERTICAL);
        auto *top_sizer = new wxBoxSizer(wxVERTICAL);
        vsizer->Add(text, 1, wxEXPAND);
        vsizer->AddSpacer(5);
        vsizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL);
        top_sizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT | wxBOTTOM, 10);
        SetSizer(top_sizer);
        #ifdef _WIN32
            wxGetApp().UpdateDlgDarkUI(this);
        #endif

    }
};




// Read a string formatted as SLIC3R_VERSION (e.g. "2.4.0-alpha1") and get major and
// minor versions and alpha/beta flags.
static void extract_major_minor(std::string version,
                                int& major, int& minor,
                                bool* is_alpha = nullptr, bool* is_beta = nullptr)
{
    if (is_alpha)
        *is_alpha = version.find("alpha") != std::string::npos;
    if (is_beta)
        *is_beta = version.find("beta") != std::string::npos;
    if (std::count(version.begin(), version.end(), '.') != 2)
        return;
    std::replace(version.begin(), version.end(), '.', ' ');
    std::istringstream ss{version};
    ss >> major >> minor;
}



// Last version where the info was sent / dialog dismissed is saved in appconfig.
// Only show the dialog when this info is not found (e.g. fresh install) or when
// current version is newer. Only major and minor versions are compared.
static bool should_dialog_be_shown()
{
    std::string last_sent_version = wxGetApp().app_config->get("system_info_sent_version");

    int last_sent_major = 0;
    int last_sent_minor = 0;
    int current_major = 0;
    int current_minor = 0;
    bool alpha = false;
    bool beta = false;
    extract_major_minor(SLIC3R_VERSION, current_major, current_minor, &alpha, &beta);
    extract_major_minor(last_sent_version, last_sent_major, last_sent_minor);


    if (current_major == 0 // This should never happen.
     /*|| alpha
     || beta*/)
        return false;

    return ((current_major > last_sent_major)
        || (current_major == last_sent_major && current_minor > last_sent_minor ));
}



static void send_info(const std::string& data)
{
    std::cout << data << std::endl;
}


// Following function saves current PrusaSlicer version into app config.
// It will be later used to decide whether to open the dialog or not.
static void save_version()
{
    wxGetApp().app_config->set("system_info_sent_version", SLIC3R_VERSION);
}



// Following function generates one string that will be shown in the preview
// and later sent if confirmed by the user.
static std::string generate_system_info_json()
{
    // Calculate hash of datadir path so it is possible to identify duplicates.
    // The result is mod 10000 so most of the information is lost and it is
    // not possible to unhash the datadir (which usually contains username).
    // It is more than enough to help identify duplicate entries.
    size_t datadir_hash = std::hash<std::string>{}(std::string(wxGetUserId().ToUTF8().data())) % 10000;

    // Get system language.
    std::string sys_language = "Unknown";
    const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
    if (lang_system != wxLANGUAGE_UNKNOWN)
        sys_language = wxLocale::GetLanguageInfo(lang_system)->CanonicalName.ToUTF8().data();

    // Build a property tree with all the information.
    namespace pt = boost::property_tree;

    pt::ptree root;
    root.put("PrusaSlicerVersion", SLIC3R_VERSION);
    root.put("BuildID", SLIC3R_BUILD_ID);
    root.put("UsernameHash", datadir_hash);
    root.put("Platform", platform_to_string(platform()));
    root.put("PlatformFlavor", platform_flavor_to_string(platform_flavor()));
    root.put("SystemLanguage", sys_language);
    root.put("TranslationLanguage: ", wxGetApp().app_config->get("translation_language"));

    pt::ptree hw_node;
    hw_node.put("ArchName", wxPlatformInfo::Get().GetArchName());
    hw_node.put("RAM_MB", size_t(Slic3r::total_physical_memory()/1000000));
    root.add_child("Hardware", hw_node);

    pt::ptree opengl_node;
    opengl_node.put("Version", OpenGLManager::get_gl_info().get_version());
    opengl_node.put("GLSLVersion", OpenGLManager::get_gl_info().get_glsl_version());
    opengl_node.put("Vendor", OpenGLManager::get_gl_info().get_vendor());
    opengl_node.put("Renderer", OpenGLManager::get_gl_info().get_renderer());
    // Generate list of OpenGL extensions:
    std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
    std::vector<std::string> extensions_list;
    boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_off);
    pt::ptree extensions_node;
    for (const std::string& s : extensions_list) {
        if (s.empty())
            continue;
        pt::ptree ext_node; // Create an unnamed node containing the value
        ext_node.put("", s);
        extensions_node.push_back(std::make_pair("", ext_node)); // Add this node to the list.
    }
    opengl_node.add_child("Extensions", extensions_node);
    root.add_child("OpenGL", opengl_node);

    // Serialize the tree into JSON and return it.
    std::stringstream ss;
    pt::write_json(ss, root);
    return ss.str();

    //std::cout << wxPlatformInfo::Get().GetOperatingSystemFamilyName() << std::endl;          // Unix
    //std::cout << wxPlatformInfo::Get().GetOperatingSystemDescription() << std::endl;         // Linux 4.15.0-142-generic x86_64
    // ? CPU, GPU, UNKNOWN, wxWidgets ???
    // tiskárny? budou už nainstalované?
}



SendSystemInfoDialog::SendSystemInfoDialog(wxWindow* parent)
    : m_system_info_json{generate_system_info_json()},
    GUI::DPIDialog(parent, wxID_ANY, _L("Send system info"), wxDefaultPosition, wxDefaultSize,
           wxDEFAULT_DIALOG_STYLE)
{
    // Get current PrusaSliver version info.
    int version_major = 0;
    int version_minor = 0;
    bool is_alpha = false;
    bool is_beta = false;
    extract_major_minor(SLIC3R_VERSION, version_major, version_minor, &is_alpha, &is_beta);
    std::string app_name = std::string(SLIC3R_APP_NAME) + " " + std::to_string(version_major)
                           + "." + std::to_string(version_minor) + " "
                           + (is_alpha ? "Alpha" : is_beta ? "Beta" : "");

    // Get current source file name.
    std::string filename(__FILE__);
    size_t last_slash_idx = filename.find_last_of("/\\");
    if (last_slash_idx != std::string::npos)
        filename = filename.substr(last_slash_idx+1);

    // Set dialog background color, fonts, etc.
    SetFont(wxGetApp().normal_font());
    wxColour bgr_clr = wxGetApp().get_window_default_clr();
    SetBackgroundColour(bgr_clr);
    const auto text_clr = wxGetApp().get_label_clr_default();
    auto text_clr_str = wxString::Format(wxT("#%02X%02X%02X"), text_clr.Red(), text_clr.Green(), text_clr.Blue());
    auto bgr_clr_str = wxString::Format(wxT("#%02X%02X%02X"), bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue());


    auto *topSizer = new wxBoxSizer(wxVERTICAL);
    auto *vsizer = new wxBoxSizer(wxVERTICAL);    

    wxString text0 = GUI::format_wxstr(_L("This is the first time you are running %1%. We would like to "
           "ask you to send some of your system information to us. This will only "
           "happen once and we will not ask you to do this again (only after you "
           "upgrade to the next version)."), app_name );
    wxString label1 = _L("Why is it needed");
    wxString text1 = _L("If we know your hardware, operating system, etc., it will greatly help us "
        "in development, prioritization and possible deprecation of features that "
        "are no more needed (for example legacy OpenGL support). This will help "
        "us to focus our effort more efficiently and spend time on features that "
        "are needed the most.");
    wxString label2 = _L("Is it safe?");
    wxString text2 = GUI::format_wxstr(
        _L("We do not send any personal information nor anything that would allow us "
           "to identify you later. To detect duplicate entries, a number derived "
           "from your username is sent, but it cannot be used to recover the username. "
           "Apart from that, only general data about your OS, hardware and OpenGL "
           "installation are sent. PrusaSlicer is open source, if you want to "
           "inspect the code actually performing the communication, see %1%."),
           std::string("<i>") + filename + "</i>");
    wxString label3 = _L("Show verbatim data that will be sent");

    auto* html_window = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER);
    wxString html = GUI::format_wxstr(
            "<html><body bgcolor=%1%><font color=%2%>"
            + text0 + "<br /><br />"
            + "<b>" + label1 + "</b><br />"
            + text1 + "<br /><br />"
            + "<b>" + label2 + "</b><br />"
            + text2 + "<br /><br />"
            + "<b><a href=\"show\">" + label3 + "</a></b><br />"
            + "</font></body></html>", bgr_clr_str, text_clr_str);
    html_window->SetPage(html);
    html_window->Bind(wxEVT_HTML_LINK_CLICKED, [this](wxHtmlLinkEvent &evt) {
                                                   ShowJsonDialog dlg(this, m_system_info_json, GetSize().Scale(0.9, 0.7));
                                                   dlg.ShowModal();
    });

    vsizer->Add(html_window, 1, wxEXPAND);

    m_btn_ask_later = new wxButton(this, wxID_ANY, _L("Ask me next time"));
    m_btn_dont_send = new wxButton(this, wxID_ANY, _L("Do not send anything"));
    m_btn_send = new wxButton(this, wxID_ANY, _L("Send system info"));

    auto* hsizer = new wxBoxSizer(wxHORIZONTAL);
    const int em = GUI::wxGetApp().em_unit();
    hsizer->Add(m_btn_ask_later);
    hsizer->AddSpacer(em);
    hsizer->Add(m_btn_dont_send);
    hsizer->AddSpacer(em);
    hsizer->Add(m_btn_send);

    vsizer->Add(hsizer, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);
    topSizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);


#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
#endif

    const auto size = GetSize();
    SetSize(std::max(size.GetWidth(), MIN_WIDTH * em),
            std::max(size.GetHeight(), MIN_HEIGHT * em));

    m_btn_send->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                    {
                                        send_info(m_system_info_json);
                                        save_version();
                                        EndModal(0);
                                    });
    m_btn_dont_send->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                         {
                                             save_version();
                                             EndModal(0);
                                         });
    m_btn_ask_later->Bind(wxEVT_BUTTON, [this](const wxEvent&) { EndModal(0); });
}



void SendSystemInfoDialog::on_dpi_changed(const wxRect&)
{
    const int& em = em_unit();
    msw_buttons_rescale(this, em, { m_btn_send->GetId(),
                                    m_btn_dont_send->GetId(),
                                    m_btn_ask_later->GetId() });
    SetMinSize(wxSize(MIN_WIDTH * em, MIN_HEIGHT * em));
    Fit();
    Refresh();
}



void show_send_system_info_dialog_if_needed()
{
    if (wxGetApp().is_gcode_viewer() || ! should_dialog_be_shown())
        return;

    SendSystemInfoDialog dlg(wxGetApp().mainframe);
    dlg.ShowModal();
}


} // namespace GUI
} // namespace Slic3r






