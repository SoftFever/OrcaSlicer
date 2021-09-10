#include "SendSystemInfoDialog.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/format.hpp"
#include "slic3r/Utils/Http.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "OpenGLManager.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/split.hpp>

#include "GL/glew.h"

#include <wx/stattext.h>
#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

// Declaration of a free function defined in OpenGLManager.cpp:
std::string gl_get_string_safe(GLenum param, const std::string& default_value);


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
    GUI::DPIDialog(parent, wxID_ANY, _L("Send system info"), wxDefaultPosition, /*wxDefaultSize*/wxSize(MIN_WIDTH * GUI::wxGetApp().em_unit(), MIN_HEIGHT * GUI::wxGetApp().em_unit()),
           wxDEFAULT_DIALOG_STYLE/*|wxRESIZE_BORDER*/)
{
    int version_major = 0;
    int version_minor = 0;
    bool is_alpha = false;
    bool is_beta = false;
    extract_major_minor(SLIC3R_VERSION, version_major, version_minor, &is_alpha, &is_beta);
    std::string app_name = std::string(SLIC3R_APP_NAME) + " " + std::to_string(version_major)
                           + "." + std::to_string(version_minor) + " "
                           + (is_alpha ? "Alpha" : is_beta ? "Beta" : "");

    const int em = GUI::wxGetApp().em_unit();
    m_min_width = MIN_WIDTH * em;
    m_min_height = MIN_HEIGHT * em;

    const wxFont& mono_font = GUI::wxGetApp().code_font();

    auto *topSizer = new wxBoxSizer(wxVERTICAL);

//    topSizer->SetMinSize(wxSize(-1, m_min_height));
    //auto *panel = new wxPanel(this);
    auto *vsizer = new wxBoxSizer(wxVERTICAL);
//    panel->SetSizer(vsizer);

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2, 1, 1, 0);
    grid_sizer->SetFlexibleDirection(wxBOTH);
    grid_sizer->AddGrowableCol(0, 1);
    grid_sizer->AddGrowableRow(0, 1);
    grid_sizer->AddGrowableRow(1, 1);

/*    auto *topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(panel, 1, wxEXPAND | wxALL, DIALOG_MARGIN);
    SetMinSize(wxSize(m_min_width, m_min_height));
    SetSizerAndFit(topsizer);
*/
    std::string filename(__FILE__);
    size_t last_slash_idx = filename.find_last_of("/");
    if (last_slash_idx != std::string::npos)
        filename = filename.substr(last_slash_idx+1);
    auto* text = new wxStaticText(/*panel*/this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(m_min_width, -1)/**/);
    text->SetLabelMarkup(
        GUI::format_wxstr(_L("This is the first time you are running %1%. We would like to "
           "ask you to send some of your system information to us. This will only "
           "happen once and we will not ask you to do this again (only after you "
           "upgrade to the next version)."), app_name )
      + "\n\n"
      + "<b>" + _L("Why is it needed") + "</b>"
      + "\n"
      + _L("If we know your hardware, operating system, etc., it will greatly help us "
           "in development, prioritization and possible deprecation of features that "
           "are no more needed (for example legacy OpenGL support). This will help "
           "us to focus our effort more efficiently and spend time on features that "
           "are needed the most.")
      + "\n\n"
      + "<b>" + _L("Is it safe?") + "</b>\n"
      + GUI::format_wxstr(
            _L("We do not send any personal information nor anything that would allow us "
               "to identify you later. To detect duplicate entries, a number derived "
               "from your username is sent, but it cannot be used to recover the username. "
               "Apart from that, only general data about your OS, hardware and OpenGL "
               "installation are sent. PrusaSlicer is open source, if you want to "
               "inspect the code actually performing the communication, see %1%."),
               std::string("<i>") + filename + "</i>")
      + "\n\n"
      + "<b>" + _L("Verbatim data that will be sent:") + "</b>"
      + "\n\n");
    vsizer/*grid_sizer*/->Add(text/*, 0, wxEXPAND*/);


    auto* txt_json = new wxTextCtrl(/*panel*/this, wxID_ANY, m_system_info_json,
                                   wxDefaultPosition, wxDefaultSize,
                                   wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    txt_json->SetFont(mono_font);
    txt_json->ShowPosition(0);
    vsizer/*grid_sizer*/->Add(txt_json, 1, wxEXPAND, SPACING);

    auto* ask_later_button = new wxButton(/*panel*/this, wxID_ANY, _L("Ask me next time"));
    auto* dont_send_button = new wxButton(/*panel*/this, wxID_ANY, _L("Do not send anything"));
    auto* send_button = new wxButton(/*panel*/this, wxID_ANY, _L("Send system info"));

    auto* hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->Add(ask_later_button);
    hsizer->AddSpacer(em);
    hsizer->Add(dont_send_button);
    hsizer->AddSpacer(em);
    hsizer->Add(send_button);

//    vsizer->Add(grid_sizer, 1, wxEXPAND);
    vsizer->Add(hsizer, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);

    //panel->SetSizer(vsizer);
    //vsizer->SetSizeHints(panel);

    topSizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);


#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
#endif

    const auto size = GetSize();
    SetSize(std::max(size.GetWidth(), m_min_width),
            std::max(size.GetHeight(), m_min_height));
//    Layout();

    send_button->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                    {
                                        send_info(m_system_info_json);
                                        save_version();
                                        EndModal(0);
                                    });
    dont_send_button->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                         {
                                             save_version();
                                             EndModal(0);
                                         });
    ask_later_button->Bind(wxEVT_BUTTON, [this](const wxEvent&) { EndModal(0); });
}



void SendSystemInfoDialog::on_dpi_changed(const wxRect&)
{
    /*const int& em = em_unit();

    msw_buttons_rescale(this, em, { p->btn_close->GetId(),
                                    p->btn_rescan->GetId(),
                                    p->btn_flash->GetId(),
                                    p->hex_picker->GetPickerCtrl()->GetId()
                                                            });

    p->min_width = MIN_WIDTH * em;
    p->min_height = MIN_HEIGHT * em;
    p->min_height_expanded = MIN_HEIGHT_EXPANDED * em;

    const int min_height = p->spoiler->IsExpanded() ? p->min_height_expanded : p->min_height;
    SetMinSize(wxSize(p->min_width, min_height));
    Fit();

    Refresh();*/
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






