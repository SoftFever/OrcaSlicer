#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include <wx/language.h>
#include <wx/notebook.h>
#include "Notebook.hpp"
#include "OG_CustomCtrl.hpp"
#include "wx/graphics.h"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/TextInput.hpp"
#include <wx/listimpl.cpp>
#include <wx/display.h>
#include <map>

#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#endif // _MSW_DARK_MODE
#endif //__WINDOWS__

namespace Slic3r { namespace GUI {

WX_DEFINE_LIST(RadioSelectorList);
wxDEFINE_EVENT(EVT_PREFERENCES_SELECT_TAB, wxCommandEvent);


class MyscrolledWindow : public wxScrolledWindow {
public:
    MyscrolledWindow(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxVSCROLL) : wxScrolledWindow(parent, id, pos, size, style) {}

    bool ShouldScrollToChildOnFocus(wxWindow* child) override { return false; }
};


wxBoxSizer *PreferencesDialog::create_item_title(wxString title, wxWindow *parent, wxString tooltip)
{
    wxBoxSizer *m_sizer_title = new wxBoxSizer(wxHORIZONTAL);

    auto m_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    m_title->SetForegroundColour(DESIGN_GRAY800_COLOR);
    m_title->SetFont(::Label::Head_13);
    m_title->Wrap(-1);
    //m_title->SetToolTip(tooltip);

    auto m_line = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line->SetBackgroundColour(DESIGN_GRAY400_COLOR);

    m_sizer_title->Add(m_title, 0, wxALIGN_CENTER | wxALL, 3);
    m_sizer_title->Add(0, 0, 0,  wxLEFT, 9);
    //m_sizer_title->Add(m_line, 0, wxEXPAND, 0);
    wxBoxSizer *sizer_line = new wxBoxSizer(wxVERTICAL);
    sizer_line->Add( m_line, 0, wxEXPAND, 0 );
    m_sizer_title->Add( sizer_line, 1, wxALIGN_CENTER, 0 );
    //m_sizer_title->Add( 0, 0, 0, wxEXPAND|wxLEFT, 80 );

    return m_sizer_title;
}

wxBoxSizer *PreferencesDialog::create_item_combobox(wxString title, wxWindow *parent, wxString tooltip, std::string param, std::vector<wxString> vlist)
{
    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_LARGE_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }


    auto use_inch = app_config->get(param);
    if (!use_inch.empty()) { combobox->SetSelection(atoi(use_inch.c_str())); }

    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    //// save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param](wxCommandEvent &e) {
        app_config->set(param, std::to_string(e.GetSelection()));
        e.Skip();
    });
    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_language_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<const wxLanguageInfo *> vlist)
{
    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);


    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_LARGE_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    auto language = app_config->get(param);
    m_current_language_selected = -1;
    std::vector<wxString>::iterator iter;
    for (size_t i = 0; i < vlist.size(); ++i) {
        auto language_name = vlist[i]->Description;

        if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CHINESE_SIMPLIFIED)) {
            language_name = wxString::FromUTF8("\xe4\xb8\xad\xe6\x96\x87\x28\xe7\xae\x80\xe4\xbd\x93\x29");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CHINESE)) {
            language_name = wxString::FromUTF8("\xe4\xb8\xad\xe6\x96\x87\x28\xe7\xb9\x81\xe4\xbd\x93\x29");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_SPANISH)) {
            language_name = wxString::FromUTF8("\x45\x73\x70\x61\xc3\xb1\x6f\x6c");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_GERMAN)) {
            language_name = wxString::FromUTF8("Deutsch");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CZECH)) {
            language_name = wxString::FromUTF8("Czech");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_SWEDISH)) {
            language_name = wxString::FromUTF8("\x53\x76\x65\x6e\x73\x6b\x61"); //Svenska
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_DUTCH)) {
            language_name = wxString::FromUTF8("Nederlands");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_FRENCH)) {
            language_name = wxString::FromUTF8("\x46\x72\x61\x6E\xC3\xA7\x61\x69\x73");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_HUNGARIAN)) {
            language_name = wxString::FromUTF8("Magyar");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_JAPANESE)) {
            language_name = wxString::FromUTF8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_ITALIAN)) {
            language_name = wxString::FromUTF8("\x69\x74\x61\x6c\x69\x61\x6e\x6f");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_KOREAN)) {
            language_name = wxString::FromUTF8("\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_RUSSIAN)) {
            language_name = wxString::FromUTF8("\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_UKRAINIAN)) {
            language_name = wxString::FromUTF8("Ukrainian");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_TURKISH)) {
            language_name = wxString::FromUTF8("Turkish");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_POLISH)) {
            language_name = wxString::FromUTF8("Polski");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_CATALAN)) {
            language_name = wxString::FromUTF8("Catalan");
        }
        else if (vlist[i] == wxLocale::GetLanguageInfo(wxLANGUAGE_PORTUGUESE_BRAZILIAN)) {
            language_name = wxString::FromUTF8("Português (Brasil)");
        }

        if (app_config->get(param) == vlist[i]->CanonicalName) {
            m_current_language_selected = i;
        }
        combobox->Append(language_name);
    }
    if (m_current_language_selected == -1 && language.size() >= 5) {
        language = language.substr(0, 2);
        for (size_t i = 0; i < vlist.size(); ++i) {
            if (vlist[i]->CanonicalName.StartsWith(language)) {
                m_current_language_selected = i;
                break;
            }
        }
    }
    combobox->SetSelection(m_current_language_selected);

    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    combobox->Bind(wxEVT_LEFT_DOWN, [this, combobox](wxMouseEvent &e) {
        m_current_language_selected = combobox->GetSelection();
        e.Skip();
    });

    combobox->Bind(wxEVT_COMBOBOX, [this, param, vlist, combobox](wxCommandEvent &e) {
        if (combobox->GetSelection() == m_current_language_selected)
            return;

        if (e.GetString().mb_str() != app_config->get(param)) {
            {
                //check if the project has changed
                if (wxGetApp().plater()->is_project_dirty()) {
                    auto result = MessageDialog(static_cast<wxWindow*>(this), _L("The current project has unsaved changes, save it before continue?"),
                        wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"), wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE).ShowModal();

                    if (result == wxID_YES) {
                        wxGetApp().plater()->save_project();
                    }
                }


                // the dialog needs to be destroyed before the call to switch_language()
                // or sometimes the application crashes into wxDialogBase() destructor
                // so we put it into an inner scope
                MessageDialog msg_wingow(nullptr, _L("Switching the language requires application restart.\n") + "\n" + _L("Do you want to continue?"),
                                         L("Language selection"), wxICON_QUESTION | wxOK | wxCANCEL);
                if (msg_wingow.ShowModal() == wxID_CANCEL) {
                    combobox->SetSelection(m_current_language_selected);
                    return;
                }
            }

            auto check = [this](bool yes_or_no) {
                // if (yes_or_no)
                //    return true;
                int act_btns = ActionButtons::SAVE;
                return wxGetApp().check_and_keep_current_preset_changes(_L("Switching application language"),
                                                                        _L("Switching application language while some presets are modified."), act_btns);
            };

            m_current_language_selected = combobox->GetSelection();
            if (m_current_language_selected >= 0 && m_current_language_selected < vlist.size()) {
                app_config->set(param, vlist[m_current_language_selected]->CanonicalName.ToUTF8().data());

                wxGetApp().load_language(vlist[m_current_language_selected]->CanonicalName, false);
                Close();
                // Reparent(nullptr);
                GetParent()->RemoveChild(this);
                wxGetApp().recreate_GUI(_L("Changing application language"));
            }
        }

        e.Skip();
    });

    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_region_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist)
{
    std::vector<wxString> local_regions = {"Asia-Pacific", "China", "Europe", "North America", "Others"};

    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_LARGE_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);
    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }

    AppConfig * config       = GUI::wxGetApp().app_config;

    int         current_region = 0;
    if (!config->get("region").empty()) {
        std::string country_code = config->get("region");
        for (auto i = 0; i < vlist.size(); i++) {
            if (local_regions[i].ToStdString() == country_code) {
                combobox->SetSelection(i);
                current_region = i;
            }
        }
    }

    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this, combobox, current_region, local_regions](wxCommandEvent &e) {
        auto region_index = e.GetSelection();
        auto region       = local_regions[region_index];

        /*auto area   = "";
        if (region == "CHN" || region == "China")
            area = "CN";
        else if (region == "USA")
            area = "US";
        else if (region == "Asia-Pacific")
            area = "Others";
        else if (region == "Europe")
            area = "US";
        else if (region == "North America")
            area = "US";
        else
            area = "Others";*/
        combobox->SetSelection(region_index);
        NetworkAgent* agent = wxGetApp().getAgent();
        AppConfig* config = GUI::wxGetApp().app_config;
        if (agent) {
            MessageDialog msg_wingow(this, _L("Changing the region will log out your account.\n") + "\n" + _L("Do you want to continue?"), L("Region selection"),
                                     wxICON_QUESTION | wxOK | wxCANCEL);
            if (msg_wingow.ShowModal() == wxID_CANCEL) {
                combobox->SetSelection(current_region);
                return;
            } else {
                wxGetApp().request_user_logout();
                config->set("region", region.ToStdString());
                auto area = config->get_country_code();
                if (agent) {
                    agent->set_country_code(area);
                }
                EndModal(wxID_CANCEL);
            }
        } else {
            config->set("region", region.ToStdString());
        }

        wxGetApp().update_publish_status();
        e.Skip();
    });

    return m_sizer_combox;
}

wxBoxSizer *PreferencesDialog::create_item_loglevel_combobox(wxString title, wxWindow *parent, wxString tooltip, std::vector<wxString> vlist)
{
    wxBoxSizer *m_sizer_combox = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_combox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    combo_title->SetFont(::Label::Body_13);
    combo_title->SetToolTip(tooltip);
    combo_title->Wrap(-1);
    m_sizer_combox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

    auto                            combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

    std::vector<wxString>::iterator iter;
    for (iter = vlist.begin(); iter != vlist.end(); iter++) { combobox->Append(*iter); }

    auto severity_level = app_config->get("log_severity_level");
    if (!severity_level.empty()) { combobox->SetValue(severity_level); }

    m_sizer_combox->Add(combobox, 0, wxALIGN_CENTER, 0);

    //// save config
    combobox->GetDropDown().Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        auto level = Slic3r::get_string_logging_level(e.GetSelection());
        Slic3r::set_logging_level(Slic3r::level_string_to_boost(level));
        app_config->set("log_severity_level",level);
        e.Skip();
     });
    return m_sizer_combox;
}


wxBoxSizer *PreferencesDialog::create_item_multiple_combobox(
    wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param, std::vector<wxString> vlista, std::vector<wxString> vlistb)
{
    std::vector<wxString> params;
    Split(app_config->get(param), "/", params);

    std::vector<wxString>::iterator iter;

   wxBoxSizer *m_sizer_tcombox= new wxBoxSizer(wxHORIZONTAL);
   m_sizer_tcombox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

   auto combo_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
   combo_title->SetToolTip(tooltip);
   combo_title->Wrap(-1);
   combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
   combo_title->SetFont(::Label::Body_13);
   m_sizer_tcombox->Add(combo_title, 0, wxALIGN_CENTER | wxALL, 3);

   auto combobox_left = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
   combobox_left->SetFont(::Label::Body_13);
   combobox_left->GetDropDown().SetFont(::Label::Body_13);


   for (iter = vlista.begin(); iter != vlista.end(); iter++) { combobox_left->Append(*iter); }
   combobox_left->SetValue(std::string(params[0].mb_str()));
   m_sizer_tcombox->Add(combobox_left, 0, wxALIGN_CENTER, 0);

   auto combo_title_add = new wxStaticText(parent, wxID_ANY, wxT("+"), wxDefaultPosition, wxDefaultSize, 0);
   combo_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
   combo_title->SetFont(::Label::Body_13);
   combo_title_add->Wrap(-1);
   m_sizer_tcombox->Add(combo_title_add, 0, wxALIGN_CENTER | wxALL, 3);

   auto combobox_right = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, DESIGN_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
   combobox_right->SetFont(::Label::Body_13);
   combobox_right->GetDropDown().SetFont(::Label::Body_13);

   for (iter = vlistb.begin(); iter != vlistb.end(); iter++) { combobox_right->Append(*iter); }
   combobox_right->SetValue(std::string(params[1].mb_str()));
   m_sizer_tcombox->Add(combobox_right, 0, wxALIGN_CENTER, 0);

    // save config
    combobox_left->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, combobox_right](wxCommandEvent &e) {
        auto config = e.GetString() + wxString("/") + combobox_right->GetValue();
        app_config->set(param, std::string(config.mb_str()));
        e.Skip();
    });

    combobox_right->GetDropDown().Bind(wxEVT_COMBOBOX, [this, param, combobox_left](wxCommandEvent &e) {
        auto config = combobox_left->GetValue() + wxString("/") + e.GetString();
        app_config->set(param, std::string(config.mb_str()));
        e.Skip();
    });

    return m_sizer_tcombox;
}

wxBoxSizer *PreferencesDialog::create_item_input(wxString title, wxString title2, wxWindow *parent, wxString tooltip, std::string param, std::function<void(wxString)> onchange)
{
    wxBoxSizer *sizer_input = new wxBoxSizer(wxHORIZONTAL);
    auto        input_title   = new wxStaticText(parent, wxID_ANY, title);
    input_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto       input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, DESIGN_INPUT_SIZE, wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_DIGITS);
    input->GetTextCtrl()->SetValidator(validator);

    auto second_title = new wxStaticText(parent, wxID_ANY, title2, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    second_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    second_title->SetFont(::Label::Body_13);
    second_title->SetToolTip(tooltip);
    second_title->Wrap(-1);

    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    sizer_input->Add(input_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 3);
    sizer_input->Add(second_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);

    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, param, input, onchange](wxCommandEvent &e) {
        auto value = input->GetTextCtrl()->GetValue();
        app_config->set(param, std::string(value.mb_str()));
        app_config->save();
        onchange(value);
        e.Skip();
    });

    input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, param, input, onchange](wxFocusEvent &e) {
        auto value = input->GetTextCtrl()->GetValue();
        app_config->set(param, std::string(value.mb_str()));
        onchange(value);
        e.Skip();
    });

    return sizer_input;
}

wxBoxSizer *PreferencesDialog::create_item_backup_input(wxString title, wxWindow *parent, wxString tooltip, std::string param)
{
    wxBoxSizer *m_sizer_input = new wxBoxSizer(wxHORIZONTAL);
    auto input_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    input_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    input_title->SetFont(::Label::Body_13);
    input_title->SetToolTip(tooltip);
    input_title->Wrap(-1);

    auto input = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, DESIGN_INPUT_SIZE, wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    input->SetBackgroundColor(input_bg);
    input->GetTextCtrl()->SetValue(app_config->get(param));
    wxTextValidator validator(wxFILTER_DIGITS);
    input->GetTextCtrl()->SetValidator(validator);


    auto second_title = new wxStaticText(parent, wxID_ANY, _L("Second"), wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    second_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    second_title->SetFont(::Label::Body_13);
    second_title->SetToolTip(tooltip);
    second_title->Wrap(-1);

    m_sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    m_sizer_input->Add(input_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
    m_sizer_input->Add(input, 0, wxALIGN_CENTER_VERTICAL, 0);
    m_sizer_input->Add(0, 0, 0, wxEXPAND | wxLEFT, 3);
    m_sizer_input->Add(second_title, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);


    input->GetTextCtrl()->Bind(wxEVT_COMMAND_TEXT_UPDATED, [this, param, input](wxCommandEvent &e) {
        m_backup_interval_time = input->GetTextCtrl()->GetValue();
        e.Skip();
    });

    std::function<void()> backup_interval = [this, param, input]() {
        m_backup_interval_time = input->GetTextCtrl()->GetValue();
        app_config->set("backup_interval", std::string(m_backup_interval_time.mb_str()));
        app_config->save();
        long backup_interval = 0;
        m_backup_interval_time.ToLong(&backup_interval);
        Slic3r::set_backup_interval(backup_interval);
    };

    input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [backup_interval](wxCommandEvent &e) {
        backup_interval();
        e.Skip();
    });

     input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [backup_interval](wxFocusEvent &e) {
        backup_interval();
        e.Skip();
    });

    if (app_config->get("backup_switch") == "true") {
        input->Enable(true);
        input->Refresh();
    } else {
        input->Enable(false);
        input->Refresh();
    }

    if (param == "backup_interval") { m_backup_interval_textinput = input; }
    return m_sizer_input;
}


wxBoxSizer *PreferencesDialog::create_item_switch(wxString title, wxWindow *parent, wxString tooltip ,std::string param)
{
    wxBoxSizer *m_sizer_switch = new wxBoxSizer(wxHORIZONTAL);
    auto switch_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, DESIGN_TITLE_SIZE, 0);
    switch_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    switch_title->SetFont(::Label::Body_13);
    switch_title->SetToolTip(tooltip);
    switch_title->Wrap(-1);
    auto switchbox = new ::SwitchButton(parent, wxID_ANY);

    /*auto index = app_config->get(param);
    if (!index.empty()) { combobox->SetSelection(atoi(index.c_str())); }*/

    m_sizer_switch->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    m_sizer_switch->Add(switch_title, 0, wxALIGN_CENTER | wxALL, 3);
    m_sizer_switch->Add( 0, 0, 1, wxEXPAND, 0 );
    m_sizer_switch->Add(switchbox, 0, wxALIGN_CENTER, 0);
    m_sizer_switch->Add( 0, 0, 0, wxEXPAND|wxLEFT, 40 );

    //// save config
    switchbox->Bind(wxEVT_TOGGLEBUTTON, [this, param](wxCommandEvent &e) {
        /* app_config->set(param, std::to_string(e.GetSelection()));
         app_config->save();*/
         e.Skip();
    });
    return m_sizer_switch;
}

wxBoxSizer* PreferencesDialog::create_item_darkmode_checkbox(wxString title, wxWindow* parent, wxString tooltip, int padding_left, std::string param)
{
    wxBoxSizer* m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto checkbox = new ::CheckBox(parent);
    checkbox->SetValue((app_config->get(param) == "1") ? true : false);
    m_dark_mode_ckeckbox = checkbox;

    m_sizer_checkbox->Add(checkbox, 0, wxALIGN_CENTER, 0);
    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    checkbox_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    checkbox_title->SetFont(::Label::Body_13);

    auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize(wxSize(size.x + FromDIP(40), -1));
    checkbox_title->Wrap(-1);
    m_sizer_checkbox->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);


    //// save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent& e) {
        app_config->set(param, checkbox->GetValue() ? "1" : "0");
        app_config->save();
        wxGetApp().Update_dark_mode_flag();

        //dark mode
#ifdef _MSW_DARK_MODE
        wxGetApp().force_colors_update();
        wxGetApp().update_ui_from_settings();
        set_dark_mode();
#endif
        SimpleEvent evt = SimpleEvent(EVT_GLCANVAS_COLOR_MODE_CHANGED);
        wxPostEvent(wxGetApp().plater(), evt);
        e.Skip();
        });

    checkbox->SetToolTip(tooltip);
    return m_sizer_checkbox;
}

void PreferencesDialog::set_dark_mode()
{
#ifdef __WINDOWS__
#ifdef _MSW_DARK_MODE
    NppDarkMode::SetDarkExplorerTheme(this->GetHWND());
    NppDarkMode::SetDarkTitleBar(this->GetHWND());
    wxGetApp().UpdateDlgDarkUI(this);
    SetActiveWindow(wxGetApp().mainframe->GetHWND());
    SetActiveWindow(GetHWND());
#endif
#endif
}

wxBoxSizer *PreferencesDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, std::string param)
{
    wxBoxSizer *m_sizer_checkbox  = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    auto checkbox = new ::CheckBox(parent);
    checkbox->SetValue(app_config->get_bool(param));

    m_sizer_checkbox->Add(checkbox, 0, wxALIGN_CENTER, 0);
    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    checkbox_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    checkbox_title->SetFont(::Label::Body_13);

    auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize(wxSize(size.x + FromDIP(5), -1));
    checkbox_title->Wrap(-1);
    m_sizer_checkbox->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);


     //// save config
    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent &e) {
        app_config->set_bool(param, checkbox->GetValue());
        app_config->save();

        // if (param == "staff_pick_switch") {
        //     bool pbool = app_config->get("staff_pick_switch") == "true";
        //     wxGetApp().switch_staff_pick(pbool);
        // }

         // backup
        if (param == "backup_switch") {
            bool pbool = app_config->get("backup_switch") == "true" ? true : false;
            std::string backup_interval = "10";
            app_config->get("backup_interval", backup_interval);
            Slic3r::set_backup_interval(pbool ? boost::lexical_cast<long>(backup_interval) : 0);
            if (m_backup_interval_textinput != nullptr) { m_backup_interval_textinput->Enable(pbool); }
        }

        if (param == "sync_user_preset") {
            bool sync = app_config->get("sync_user_preset") == "true" ? true : false;
            if (sync) {
                wxGetApp().start_sync_user_preset();
            } else {
                wxGetApp().stop_sync_user_preset();
            }
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " sync_user_preset: " << (sync ? "true" : "false");
        }

        #ifdef __WXMSW__
        if (param == "associate_3mf") {
             bool pbool = app_config->get("associate_3mf") == "true" ? true : false;
             if (pbool) {
                 wxGetApp().associate_files(L"3mf");
             } else {
                 wxGetApp().disassociate_files(L"3mf");
             }
        }

        if (param == "associate_stl") {
            bool pbool = app_config->get("associate_stl") == "true" ? true : false;
            if (pbool) {
                wxGetApp().associate_files(L"stl");
            } else {
                wxGetApp().disassociate_files(L"stl");
            }
        }

        if (param == "associate_step") {
            bool pbool = app_config->get("associate_step") == "true" ? true : false;
            if (pbool) {
                wxGetApp().associate_files(L"step");
            } else {
                wxGetApp().disassociate_files(L"step");
            }
        }

        if (param == "installed_networking") {
            bool pbool = app_config->get_bool("installed_networking");
            if (pbool) {
                GUI::wxGetApp().CallAfter([] { GUI::wxGetApp().ShowDownNetPluginDlg(); });
            }
        }

#endif // __WXMSW__

        if (param == "developer_mode") {
            m_developer_mode_def = app_config->get("developer_mode");
            if (m_developer_mode_def == "true") {
                Slic3r::GUI::wxGetApp().save_mode(comDevelop);
            } else {
                Slic3r::GUI::wxGetApp().save_mode(comAdvanced);
            }
        }

        // webview  dump_vedio
        if (param == "internal_developer_mode") {
            m_internal_developer_mode_def = app_config->get("internal_developer_mode");
            if (m_internal_developer_mode_def == "true") {
                Slic3r::GUI::wxGetApp().update_internal_development();
                Slic3r::GUI::wxGetApp().mainframe->show_log_window();
            } else {
                Slic3r::GUI::wxGetApp().update_internal_development();
            }
        }

        e.Skip();
    });

    //// for debug mode
    if (param == "developer_mode") { m_developer_mode_ckeckbox = checkbox; }
    if (param == "internal_developer_mode") { m_internal_developer_mode_ckeckbox = checkbox; }


    checkbox->SetToolTip(tooltip);
    return m_sizer_checkbox;
}

wxBoxSizer* PreferencesDialog::create_item_button(
    wxString title, wxString title2, wxWindow* parent, wxString tooltip, wxString tooltip2, std::function<void()> onclick, bool button_on_left/* = false*/)
{
    wxBoxSizer *m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    auto m_staticTextPath = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_staticTextPath->SetMaxSize(wxSize(FromDIP(240), -1));
    m_staticTextPath->SetForegroundColour(DESIGN_GRAY900_COLOR);
    m_staticTextPath->SetFont(::Label::Body_13);
    m_staticTextPath->Wrap(-1);
    m_staticTextPath->SetToolTip(tooltip);

    auto m_button_download = new Button(parent, title2);

    StateColor abort_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
                        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    m_button_download->SetBackgroundColor(abort_bg);
    StateColor abort_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetBorderColor(abort_bd);
    StateColor abort_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetTextColor(abort_text);
    m_button_download->SetFont(Label::Body_10);
    m_button_download->SetMinSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetCornerRadius(FromDIP(12));
    m_button_download->SetToolTip(tooltip2);

    m_button_download->Bind(wxEVT_BUTTON, [this, onclick](auto &e) { onclick(); });

    if (button_on_left) {
        m_sizer_checkbox->Add(m_button_download, 0, wxALL, FromDIP(5));
        m_sizer_checkbox->Add(m_staticTextPath, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    } else {
        m_sizer_checkbox->Add(m_staticTextPath, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
        m_sizer_checkbox->Add(m_button_download, 0, wxALL, FromDIP(5));
    }

    return m_sizer_checkbox;
}

wxWindow* PreferencesDialog::create_item_downloads(wxWindow* parent, int padding_left, std::string param)
{
    wxString download_path = wxString::FromUTF8(app_config->get("download_path"));
    auto item_panel = new wxWindow(parent, wxID_ANY);
    item_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);
    auto m_staticTextPath = new wxStaticText(item_panel, wxID_ANY, download_path, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    //m_staticTextPath->SetMaxSize(wxSize(FromDIP(440), -1));
    m_staticTextPath->SetForegroundColour(DESIGN_GRAY600_COLOR);
    m_staticTextPath->SetFont(::Label::Body_13);
    m_staticTextPath->Wrap(-1);

    auto m_button_download = new Button(item_panel, _L("Browse"));

    StateColor abort_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
    std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
    std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    m_button_download->SetBackgroundColor(abort_bg);
    StateColor abort_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetBorderColor(abort_bd);
    StateColor abort_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_download->SetTextColor(abort_text);
    m_button_download->SetFont(Label::Body_10);
    m_button_download->SetMinSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetSize(wxSize(FromDIP(58), FromDIP(22)));
    m_button_download->SetCornerRadius(FromDIP(12));

    m_button_download->Bind(wxEVT_BUTTON, [this, m_staticTextPath, item_panel](auto& e) {
        wxString defaultPath = wxT("/");
        wxDirDialog dialog(this, _L("Choose Download Directory"), defaultPath, wxDD_NEW_DIR_BUTTON);

        if (dialog.ShowModal() == wxID_OK) {
            wxString download_path = dialog.GetPath();
            std::string download_path_str = download_path.ToUTF8().data();
            app_config->set("download_path", download_path_str);
            m_staticTextPath->SetLabelText(download_path);
            item_panel->Layout();
        }
        });

    m_sizer_checkbox->Add(m_staticTextPath, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
    m_sizer_checkbox->Add(m_button_download, 0, wxALL, FromDIP(5));

    item_panel->SetSizer(m_sizer_checkbox);
    item_panel->Layout();

    return item_panel;
}

wxWindow *PreferencesDialog ::create_item_radiobox(wxString title, wxWindow *parent, wxString tooltip, int padding_left, int groupid, std::string param)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(28)));
    item->SetBackgroundColour(*wxWHITE);

    RadioBox *radiobox = new RadioBox(item);
    radiobox->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - radiobox->GetSize().GetHeight()) / 2));
    radiobox->Bind(wxEVT_LEFT_DOWN, &PreferencesDialog::OnSelectRadio, this);

    RadioSelector *rs = new RadioSelector;
    rs->m_groupid     = groupid;
    rs->m_param_name  = param;
    rs->m_radiobox    = radiobox;
    rs->m_selected    = false;
    m_radio_group.Append(rs);

    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left + radiobox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return item;
}

#ifdef WIN32
wxBoxSizer* PreferencesDialog::create_item_link_association(wxWindow* parent, wxString url_prefix, wxString website_name)
{
    wxString title = _L("Associate") + (boost::format(" %1%://") % url_prefix.c_str()).str();
    wxString tooltip = _L("Associate") + " " + url_prefix + ":// " + _L("with OrcaSlicer so that Orca can open models from") + " " + website_name;

    std::wstring registered_bin; // not used, just here to provide a ref to check fn
    bool reg_to_current_instance = wxGetApp().check_url_association(url_prefix.ToStdWstring(), registered_bin);

    auto* h_sizer = new wxBoxSizer(wxHORIZONTAL); // contains checkbox and other elements on the first line
    h_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, 23);

    // build checkbox
    auto checkbox = new ::CheckBox(parent);
    checkbox->SetToolTip(tooltip);
    checkbox->SetValue(reg_to_current_instance); // If registered to the current instance, checkbox should be checked
    checkbox->Enable(!reg_to_current_instance); // Since unregistering isn't supported, checkbox is disabled when checked

    h_sizer->Add(checkbox, 0, wxALIGN_CENTER, 0);
    h_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    // build text next to checkbox
    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title);
    checkbox_title->SetToolTip(tooltip);
    checkbox_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    checkbox_title->SetFont(::Label::Body_13);
    auto size = checkbox_title->GetTextExtent(title);
    checkbox_title->SetMinSize({ size.x + FromDIP(5), -1 });
    checkbox_title->Wrap(-1);
    h_sizer->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);

    auto* v_sizer = new wxBoxSizer(wxVERTICAL);
    v_sizer->Add(h_sizer);

    // build text below checkbox that indicates the instance currently registered to handle the link type
    auto* registered_instance_title = new wxStaticText(parent, wxID_ANY, "");
    registered_instance_title->SetForegroundColour(DESIGN_GRAY900_COLOR);
    registered_instance_title->SetFont(::Label::Body_13);
    registered_instance_title->Wrap(-1);

    // update the text below checkbox
    auto update_current_association_str = [=, &reg_to_current_instance](){
        // get registered binary for given link type
        std::wstring registered_bin;
        reg_to_current_instance = wxGetApp().check_url_association(url_prefix.wc_str(), registered_bin);

        // format registered binary to get only the path and remove excess chars
        if (!registered_bin.empty())
            // skip idx 0 because it is the first quotation mark
            registered_bin = registered_bin.substr(1, registered_bin.find(L'\"', 1) - 1);

        wxString current_association_str = _L("Current Association: ");
        if (reg_to_current_instance) {
            current_association_str += _L("Current Instance");
            registered_instance_title->SetToolTip(_L("Current Instance Path: ") + registered_bin);
        } else if (registered_bin.empty())
            current_association_str += _L("None");
        else
            current_association_str += registered_bin;

        registered_instance_title->SetLabel(current_association_str);
        auto size = registered_instance_title->GetTextExtent(current_association_str);
        registered_instance_title->SetMinSize({ size.x + FromDIP(5), -1 });
    };
    update_current_association_str();

    v_sizer->Add(registered_instance_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 60);

    checkbox->Bind(wxEVT_TOGGLEBUTTON, [=](wxCommandEvent& e) {
        wxGetApp().associate_url(url_prefix.ToStdWstring());
        checkbox->Disable();
        update_current_association_str();
        e.Skip();
    });

    return v_sizer;
}
#endif // WIN32

PreferencesDialog::PreferencesDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style)
    : DPIDialog(parent, id, _L("Preferences"), pos, size, style)
{
    SetBackgroundColour(*wxWHITE);
    create();
    wxGetApp().UpdateDlgDarkUI(this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        try {
            NetworkAgent* agent = GUI::wxGetApp().getAgent();
            if (agent) {
                json j;
                std::string value;
                value = wxGetApp().app_config->get("auto_calculate");
                j["auto_flushing"] = value;
                value = wxGetApp().app_config->get("auto_calculate_when_filament_change");
                j["auto_calculate_when_filament_change"] = value;
                agent->track_event("preferences_changed", j.dump());
            }
        } catch(...) {}
        event.Skip();
        });
}

void PreferencesDialog::create()
{
    app_config             = get_app_config();
    m_backup_interval_time = app_config->get("backup_interval");

    // set icon for dialog
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetSizeHints(wxDefaultSize, wxDefaultSize);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new MyscrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);

    m_sizer_body = new wxBoxSizer(wxVERTICAL);

    auto m_top_line = new wxPanel(m_scrolledWindow, wxID_ANY, wxDefaultPosition, wxSize(DESIGN_RESOUTION_PREFERENCES.x, 1), wxTAB_TRAVERSAL);
    m_top_line->SetBackgroundColour(DESIGN_GRAY400_COLOR);

    m_sizer_body->Add(m_top_line, 0, wxEXPAND, 0);

    auto general_page = create_general_page();
#if !BBL_RELEASE_TO_PUBLIC
    auto debug_page   = create_debug_page();
#endif

    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(28));
    m_sizer_body->Add(general_page, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(38));
#if !BBL_RELEASE_TO_PUBLIC
    m_sizer_body->Add(debug_page, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(38));
#endif
    m_sizer_body->Add(0, 0, 0, wxBOTTOM, FromDIP(28));
    m_scrolledWindow->SetSizerAndFit(m_sizer_body);

    main_sizer->Add(m_scrolledWindow, 1, wxEXPAND);

    SetSizer(main_sizer);
    Layout();
    Fit();
    int screen_height = wxDisplay(m_parent).GetClientArea().GetHeight();
    if (this->GetSize().GetY() > screen_height)
        this->SetSize(this->GetSize().GetX() + FromDIP(40), screen_height * 4 / 5);

    CenterOnParent();
    wxPoint start_pos = this->GetPosition();
    if (start_pos.y < 0) { this->SetPosition(wxPoint(start_pos.x, 0)); }

    //select first
    auto event = wxCommandEvent(EVT_PREFERENCES_SELECT_TAB);
    event.SetInt(0);
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

PreferencesDialog::~PreferencesDialog()
{
    m_radio_group.DeleteContents(true);
    m_hash_selector.clear();
}

void PreferencesDialog::on_dpi_changed(const wxRect &suggested_rect) { this->Refresh(); }

void PreferencesDialog::Split(const std::string &src, const std::string &separator, std::vector<wxString> &dest)
{
    std::string            str = src;
    std::string            substring;
    std::string::size_type start = 0, index;
    dest.clear();
    index = str.find_first_of(separator, start);
    do {
        if (index != std::string::npos) {
            substring = str.substr(start, index - start);
            dest.push_back(substring);
            start = index + separator.size();
            index = str.find(separator, start);
            if (start == std::string::npos) break;
        }
    } while (index != std::string::npos);

    substring = str.substr(start);
    dest.push_back(substring);
}

wxWindow* PreferencesDialog::create_general_page()
{
    auto page = new wxWindow(m_scrolledWindow, wxID_ANY);
    page->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

    auto title_general_settings = create_item_title(_L("General Settings"), page, _L("General Settings"));

    // bbs supported languages
    wxLanguage supported_languages[]{
        wxLANGUAGE_ENGLISH,
        wxLANGUAGE_CHINESE_SIMPLIFIED,
        wxLANGUAGE_CHINESE,
        wxLANGUAGE_GERMAN,
        wxLANGUAGE_CZECH,
        wxLANGUAGE_FRENCH,
        wxLANGUAGE_SPANISH,
        wxLANGUAGE_SWEDISH,
        wxLANGUAGE_DUTCH,
        wxLANGUAGE_HUNGARIAN,
        wxLANGUAGE_JAPANESE,
        wxLANGUAGE_ITALIAN,
        wxLANGUAGE_KOREAN,
        wxLANGUAGE_RUSSIAN,
        wxLANGUAGE_UKRAINIAN,
        wxLANGUAGE_TURKISH,
        wxLANGUAGE_POLISH,
        wxLANGUAGE_CATALAN,
        wxLANGUAGE_PORTUGUESE_BRAZILIAN
    };

    auto translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo *> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < translations.GetCount(); ++i) {
        const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(translations[i]);

        if (langinfo == nullptr) continue;
        int language_num = sizeof(supported_languages) / sizeof(supported_languages[0]);
        for (auto si = 0; si < language_num; si++) {
            if (langinfo == wxLocale::GetLanguageInfo(supported_languages[si])) {
                language_infos.emplace_back(langinfo);
            }
        }
        //if (langinfo != nullptr) language_infos.emplace_back(langinfo);
    }
    sort_remove_duplicates(language_infos);
    std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo *l, const wxLanguageInfo *r) { return l->Description < r->Description; });
    auto item_language = create_item_language_combobox(_L("Language"), page, _L("Language"), 50, "language", language_infos);

    std::vector<wxString> Regions         = {_L("Asia-Pacific"), _L("China"), _L("Europe"), _L("North America"), _L("Others")};
    auto                  item_region= create_item_region_combobox(_L("Login Region"), page, _L("Login Region"), Regions);

    auto item_stealth_mode = create_item_checkbox(_L("Stealth Mode"), page, _L("This stops the transmission of data to Bambu's cloud services. Users who don't use BBL machines or use LAN mode only can safely turn on this function."), 50, "stealth_mode");
    auto item_enable_plugin = create_item_checkbox(_L("Enable network plugin"), page, _L("Enable network plugin"), 50, "installed_networking");
    auto item_check_stable_version_only = create_item_checkbox(_L("Check for stable updates only"), page, _L("Check for stable updates only"), 50, "check_stable_update_only");

    std::vector<wxString> Units         = {_L("Metric") + " (mm, g)", _L("Imperial") + " (in, oz)"};
    auto item_currency = create_item_combobox(_L("Units"), page, _L("Units"), "use_inches", Units);
    auto item_single_instance = create_item_checkbox(_L("Allow only one OrcaSlicer instance"), page, 
    #if __APPLE__
            _L("On OSX there is always only one instance of app running by default. However it is allowed to run multiple instances "
                "of same app from the command line. In such case this settings will allow only one instance."), 
    #else
            _L("If this is enabled, when starting OrcaSlicer and another instance of the same OrcaSlicer is already running, that instance will be reactivated instead."), 
    #endif
            50, "single_instance");

    std::vector<wxString> DefaultPage = {_L("Home"), _L("Prepare")};
    auto item_default_page = create_item_combobox(_L("Default Page"), page, _L("Set the page opened on startup."), "default_page", DefaultPage);

    std::vector<wxString> CameraNavStyle = {_L("Default"), _L("Touchpad")};
    auto item_camera_navigation_style = create_item_combobox(_L("Camera style"), page, _L("Select camera navigation style.\nDefault: LMB+move for rotation, RMB/MMB+move for panning.\nTouchpad: Alt+move for rotation, Shift+move for panning."), "camera_navigation_style", CameraNavStyle);

    auto item_mouse_zoom_settings = create_item_checkbox(_L("Zoom to mouse position"), page, _L("Zoom in towards the mouse pointer's position in the 3D view, rather than the 2D window center."), 50, "zoom_to_mouse");
    auto item_use_free_camera_settings = create_item_checkbox(_L("Use free camera"), page, _L("If enabled, use free camera. If not enabled, use constrained camera."), 50, "use_free_camera");
    auto reverse_mouse_zoom = create_item_checkbox(_L("Reverse mouse zoom"), page, _L("If enabled, reverses the direction of zoom with mouse wheel."), 50, "reverse_mouse_wheel_zoom");

    auto item_show_splash_screen = create_item_checkbox(_L("Show splash screen"), page, _L("Show the splash screen during startup."), 50, "show_splash_screen");
    auto item_hints = create_item_checkbox(_L("Show \"Tip of the day\" notification after start"), page, _L("If enabled, useful hints are displayed at startup."), 50, "show_hints");

    auto item_calc_mode = create_item_checkbox(_L("Flushing volumes: Auto-calculate every time the color changed."), page, _L("If enabled, auto-calculate every time the color changed."), 50, "auto_calculate");
    auto item_calc_in_long_retract = create_item_checkbox(_L("Flushing volumes: Auto-calculate every time when the filament is changed."), page, _L("If enabled, auto-calculate every time when filament is changed"), 50, "auto_calculate_when_filament_change");
    auto item_remember_printer_config = create_item_checkbox(_L("Remember printer configuration"), page, _L("If enabled, Orca will remember and switch filament/process configuration for each printer automatically."), 50, "remember_printer_config");
    auto item_multi_machine = create_item_checkbox(_L("Multi-device Management(Take effect after restarting Orca)."), page, _L("With this option enabled, you can send a task to multiple devices at the same time and manage multiple devices."), 50, "enable_multi_machine");
    auto item_auto_arrange  = create_item_checkbox(_L("Auto arrange plate after cloning"), page, _L("Auto arrange plate after object cloning"), 50, "auto_arrange");
    auto title_presets = create_item_title(_L("Presets"), page, _L("Presets"));
    auto title_network = create_item_title(_L("Network"), page, _L("Network"));
    auto item_user_sync        = create_item_checkbox(_L("Auto sync user presets(Printer/Filament/Process)"), page, _L("User Sync"), 50, "sync_user_preset");
    auto item_system_sync        = create_item_checkbox(_L("Update built-in Presets automatically."), page, _L("System Sync"), 50, "sync_system_preset");
    auto item_save_presets = create_item_button(_L("Clear my choice on the unsaved presets."), _L("Clear"), page, L"", _L("Clear my choice on the unsaved presets."), []() {
        wxGetApp().app_config->set("save_preset_choise", "");
    });

#ifdef _WIN32
    auto title_associate_file = create_item_title(_L("Associate files to OrcaSlicer"), page, _L("Associate files to OrcaSlicer"));

    // associate file
    auto item_associate_3mf  = create_item_checkbox(_L("Associate .3mf files to OrcaSlicer"), page,
                                                        _L("If enabled, sets OrcaSlicer as default application to open .3mf files"), 50, "associate_3mf");
    auto item_associate_stl  = create_item_checkbox(_L("Associate .stl files to OrcaSlicer"), page,
                                                        _L("If enabled, sets OrcaSlicer as default application to open .stl files"), 50, "associate_stl");
    auto item_associate_step = create_item_checkbox(_L("Associate .step/.stp files to OrcaSlicer"), page,
                                                         _L("If enabled, sets OrcaSlicer as default application to open .step files"), 50, "associate_step");

    auto title_associate_url = create_item_title(_L("Associate web links to OrcaSlicer"), page, _L("Associate URLs to OrcaSlicer"));

    auto associate_url_prusaslicer = create_item_link_association(page, L"prusaslicer", "Printables.com");
    auto associate_url_bambustudio = create_item_link_association(page, L"bambustudio", "Makerworld.com");
    auto associate_url_cura        = create_item_link_association(page, L"cura", "Thingiverse.com");
#endif // _WIN32

    // auto title_modelmall = create_item_title(_L("Online Models"), page, _L("Online Models"));
    // auto item_backup = create_item_switch(_L("Backup switch"), page, _L("Backup switch"), "units");
    // auto item_modelmall = create_item_checkbox(_L("Show online staff-picked models on the home page"), page, _L("Show online staff-picked models on the home page"), 50, "staff_pick_switch");

    auto title_project = create_item_title(_L("Project"), page, "");
    auto item_max_recent_count = create_item_input(_L("Maximum recent projects"), "", page, _L("Maximum count of recent projects"), "max_recent_count", [](wxString value) {
        long max = 0;
        if (value.ToLong(&max))
            wxGetApp().mainframe->set_max_recent_count(max);
    });
    auto item_save_choise = create_item_button(_L("Clear my choice on the unsaved projects."), _L("Clear"), page, L"", _L("Clear my choice on the unsaved projects."), []() {
        wxGetApp().app_config->set("save_project_choise", "");
    });
    // auto item_backup = create_item_switch(_L("Backup switch"), page, _L("Backup switch"), "units");
    auto item_gcodes_warning = create_item_checkbox(_L("No warnings when loading 3MF with modified G-codes"), page,_L("No warnings when loading 3MF with modified G-codes"), 50, "no_warn_when_modified_gcodes");
    auto item_backup  = create_item_checkbox(_L("Auto-Backup"), page,_L("Backup your project periodically for restoring from the occasional crash."), 50, "backup_switch");
    auto item_backup_interval = create_item_backup_input(_L("every"), page, _L("The period of backup in seconds."), "backup_interval");

    //downloads
    auto title_downloads = create_item_title(_L("Downloads"), page, _L("Downloads"));
    auto item_downloads = create_item_downloads(page,50,"download_path");

    //dark mode
#ifdef _WIN32
    auto title_darkmode = create_item_title(_L("Dark Mode"), page, _L("Dark Mode"));
    auto item_darkmode = create_item_darkmode_checkbox(_L("Enable Dark mode"), page,_L("Enable Dark mode"), 50, "dark_color_mode");
#endif

    auto title_develop_mode = create_item_title(_L("Develop mode"), page, _L("Develop mode"));
    auto item_develop_mode  = create_item_checkbox(_L("Develop mode"), page, _L("Develop mode"), 50, "developer_mode");
    auto item_skip_ams_blacklist_check  = create_item_checkbox(_L("Skip AMS blacklist check"), page, _L("Skip AMS blacklist check"), 50, "skip_ams_blacklist_check");

    sizer_page->Add(title_general_settings, 0, wxEXPAND, 0);
    sizer_page->Add(item_language, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_region, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_currency, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_default_page, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_camera_navigation_style, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_single_instance, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_mouse_zoom_settings, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_use_free_camera_settings, 0, wxTOP, FromDIP(3));
    sizer_page->Add(reverse_mouse_zoom, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_show_splash_screen, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_hints, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_calc_in_long_retract, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_multi_machine, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_auto_arrange, 0, wxTOP, FromDIP(3));
    sizer_page->Add(title_presets, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_calc_mode, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_user_sync, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_system_sync, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_remember_printer_config, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_save_presets, 0, wxTOP, FromDIP(3));
    sizer_page->Add(title_network, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_check_stable_version_only, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_stealth_mode, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_enable_plugin, 0, wxTOP, FromDIP(3));
#ifdef _WIN32
    sizer_page->Add(title_associate_file, 0, wxTOP| wxEXPAND, FromDIP(20));
    sizer_page->Add(item_associate_3mf, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_associate_stl, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_associate_step, 0, wxTOP, FromDIP(3));
    sizer_page->Add(title_associate_url, 0, wxTOP| wxEXPAND, FromDIP(20));
    sizer_page->Add(associate_url_prusaslicer, 0, wxTOP, FromDIP(3));
    sizer_page->Add(associate_url_bambustudio, 0, wxTOP, FromDIP(3));
    sizer_page->Add(associate_url_cura, 0, wxTOP, FromDIP(3));
#endif // _WIN32
    // auto item_title_modelmall = sizer_page->Add(title_modelmall, 0, wxTOP | wxEXPAND, FromDIP(20));
    // auto item_item_modelmall = sizer_page->Add(item_modelmall, 0, wxTOP, FromDIP(3));
    // auto update_modelmall = [this, item_title_modelmall, item_item_modelmall] (wxEvent & e) {
    //     bool has_model_mall = wxGetApp().has_model_mall();
    //     item_title_modelmall->Show(has_model_mall);
    //     item_item_modelmall->Show(has_model_mall);
    //     Layout();
    //     Fit();
    // };
    // wxCommandEvent eee(wxEVT_COMBOBOX);
    // update_modelmall(eee);
    // item_region->GetItem(size_t(2))->GetWindow()->Bind(wxEVT_COMBOBOX, update_modelmall);
    sizer_page->Add(title_project, 0, wxTOP| wxEXPAND, FromDIP(20));
    sizer_page->Add(item_max_recent_count, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_save_choise, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_gcodes_warning, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_backup, 0, wxTOP,FromDIP(3));
    item_backup->Add(item_backup_interval, 0, wxLEFT, 0);

    sizer_page->Add(title_downloads, 0, wxTOP| wxEXPAND, FromDIP(20));
    sizer_page->Add(item_downloads, 0, wxEXPAND, FromDIP(3));

#ifdef _WIN32
    sizer_page->Add(title_darkmode, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_darkmode, 0, wxEXPAND, FromDIP(3));
#endif

    sizer_page->Add(title_develop_mode, 0, wxTOP | wxEXPAND, FromDIP(20));
    sizer_page->Add(item_develop_mode, 0, wxTOP, FromDIP(3));
    sizer_page->Add(item_skip_ams_blacklist_check, 0, wxTOP, FromDIP(3));

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
    return page;
}

void PreferencesDialog::create_gui_page()
{
    auto page = new wxWindow(this, wxID_ANY);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

    auto title_index_and_tip = create_item_title(_L("Home page and daily tips"), page, _L("Home page and daily tips"));
    auto item_home_page      = create_item_checkbox(_L("Show home page on startup"), page, _L("Show home page on startup"), 50, "show_home_page");
    //auto item_daily_tip      = create_item_checkbox(_L("Show daily tip on startup"), page, _L("Show daily tip on startup"), 50, "show_daily_tips");

    sizer_page->Add(title_index_and_tip, 0, wxTOP, 26);
    sizer_page->Add(item_home_page, 0, wxTOP, 6);
    //sizer_page->Add(item_daily_tip, 0, wxTOP, 6);

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
}

void PreferencesDialog::create_sync_page()
{
    auto page = new wxWindow(this, wxID_ANY);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

     auto title_sync_settingy   = create_item_title(_L("Sync settings"), page, _L("Sync settings"));
    auto item_user_sync        = create_item_checkbox(_L("User sync"), page, _L("User sync"), 50, "user_sync_switch");
    auto item_preset_sync      = create_item_checkbox(_L("Preset sync"), page, _L("Preset sync"), 50, "preset_sync_switch");
    auto item_preferences_sync = create_item_checkbox(_L("Preferences sync"), page, _L("Preferences sync"), 50, "preferences_sync_switch");

    sizer_page->Add(title_sync_settingy, 0, wxTOP, 26);
    sizer_page->Add(item_user_sync, 0, wxTOP, 6);
    sizer_page->Add(item_preset_sync, 0, wxTOP, 6);
    sizer_page->Add(item_preferences_sync, 0, wxTOP, 6);

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
}

void PreferencesDialog::create_shortcuts_page()
{
    auto page = new wxWindow(this, wxID_ANY);
    wxBoxSizer *sizer_page = new wxBoxSizer(wxVERTICAL);

    auto title_view_control = create_item_title(_L("View control settings"), page, _L("View control settings"));
    std::vector<wxString> keyboard_supported;
    Split(app_config->get("keyboard_supported"), "/", keyboard_supported);

    std::vector<wxString> mouse_supported;
    Split(app_config->get("mouse_supported"), "/", mouse_supported);

    auto item_rotate_view = create_item_multiple_combobox(_L("Rotate of view"), page, _L("Rotate of view"), 10, "rotate_view", keyboard_supported,
                                                               mouse_supported);
    auto item_move_view   = create_item_multiple_combobox(_L("Move of view"), page, _L("Move of view"), 10, "move_view", keyboard_supported, mouse_supported);
    auto item_zoom_view   = create_item_multiple_combobox(_L("Zoom of view"), page, _L("Zoom of view"), 10, "rotate_view", keyboard_supported, mouse_supported);

    auto title_other = create_item_title(_L("Other"), page, _L("Other"));
    auto item_other  = create_item_checkbox(_L("Mouse wheel reverses when zooming"), page, _L("Mouse wheel reverses when zooming"), 50, "mouse_wheel");

    sizer_page->Add(title_view_control, 0, wxTOP, 26);
    sizer_page->Add(item_rotate_view, 0, wxTOP, 8);
    sizer_page->Add(item_move_view, 0, wxTOP, 8);
    sizer_page->Add(item_zoom_view, 0, wxTOP, 8);
    // sizer_page->Add(item_precise_control, 0, wxTOP, 0);
    sizer_page->Add(title_other, 0, wxTOP, 20);
    sizer_page->Add(item_other, 0, wxTOP, 5);

    page->SetSizer(sizer_page);
    page->Layout();
    sizer_page->Fit(page);
}

wxWindow* PreferencesDialog::create_debug_page()
{
    auto page = new wxWindow(m_scrolledWindow, wxID_ANY);
    page->SetBackgroundColour(*wxWHITE);

    m_internal_developer_mode_def = app_config->get("internal_developer_mode");
    m_backup_interval_def = app_config->get("backup_interval");
    m_iot_environment_def = app_config->get("iot_environment");

    wxBoxSizer *bSizer = new wxBoxSizer(wxVERTICAL);


    auto enable_ssl_for_mqtt = create_item_checkbox(_L("Enable SSL(MQTT)"), page, _L("Enable SSL(MQTT)"), 50, "enable_ssl_for_mqtt");
    auto enable_ssl_for_ftp = create_item_checkbox(_L("Enable SSL(FTP)"), page, _L("Enable SSL(MQTT)"), 50, "enable_ssl_for_ftp");
    auto item_internal_developer = create_item_checkbox(_L("Internal developer mode"), page, _L("Internal developer mode"), 50, "internal_developer_mode");

    auto title_log_level = create_item_title(_L("Log Level"), page, _L("Log Level"));
    auto log_level_list  = std::vector<wxString>{_L("fatal"), _L("error"), _L("warning"), _L("info"), _L("debug"), _L("trace")};
    auto loglevel_combox = create_item_loglevel_combobox(_L("Log Level"), page, _L("Log Level"), log_level_list);

    auto title_host = create_item_title(_L("Host Setting"), page, _L("Host Setting"));
    auto radio1     = create_item_radiobox(_L("DEV host: api-dev.bambu-lab.com/v1"), page, wxEmptyString, 50, 1, "dev_host");
    auto radio2     = create_item_radiobox(_L("QA  host: api-qa.bambu-lab.com/v1"), page, wxEmptyString, 50, 1, "qa_host");
    auto radio3     = create_item_radiobox(_L("PRE host: api-pre.bambu-lab.com/v1"), page, wxEmptyString, 50, 1, "pre_host");
    auto radio4     = create_item_radiobox(_L("Product host"), page, wxEmptyString, 50, 1, "product_host");

    if (m_iot_environment_def == ENV_DEV_HOST) {
        on_select_radio("dev_host");
    } else if (m_iot_environment_def == ENV_QAT_HOST) {
        on_select_radio("qa_host");
    } else if (m_iot_environment_def == ENV_PRE_HOST) {
        on_select_radio("pre_host");
    } else if (m_iot_environment_def == ENV_PRODUCT_HOST) {
        on_select_radio("product_host");
    }


    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
        std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));
    StateColor btn_bd_white(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    Button* debug_button = new Button(page, _L("debug save button"));
    debug_button->SetBackgroundColor(btn_bg_white);
    debug_button->SetBorderColor(btn_bd_white);
    debug_button->SetFont(Label::Body_13);


    debug_button->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        // success message box
        MessageDialog dialog(this, _L("save debug settings"), _L("DEBUG settings have saved successfully!"), wxNO_DEFAULT | wxYES_NO | wxICON_INFORMATION);
        dialog.SetSize(400,-1);
        switch (dialog.ShowModal()) {
        case wxID_NO: {
            //if (m_developer_mode_def != app_config->get("developer_mode")) {
            //    app_config->set_bool("developer_mode", m_developer_mode_def == "true" ? true : false);
            //    m_developer_mode_ckeckbox->SetValue(m_developer_mode_def == "true" ? true : false);
            //}
            //if (m_internal_developer_mode_def != app_config->get("internal_developer_mode")) {
            //    app_config->set_bool("internal_developer_mode", m_internal_developer_mode_def == "true" ? true : false);
            //    m_internal_developer_mode_ckeckbox->SetValue(m_internal_developer_mode_def == "true" ? true : false);
            //}

            if (m_backup_interval_def != m_backup_interval_time) { m_backup_interval_textinput->GetTextCtrl()->SetValue(m_backup_interval_def); }

            if (m_iot_environment_def == ENV_DEV_HOST) {
                on_select_radio("dev_host");
            } else if (m_iot_environment_def == ENV_QAT_HOST) {
                on_select_radio("qa_host");
            } else if (m_iot_environment_def == ENV_PRE_HOST) {
                on_select_radio("pre_host");
            } else if (m_iot_environment_def == ENV_PRODUCT_HOST) {
                on_select_radio("product_host");
            }

            break;
        }

        case wxID_YES: {
            // bbs  domain changed
            auto param = get_select_radio(1);

            std::map<wxString, wxString> iot_environment_map;
            iot_environment_map["dev_host"] = ENV_DEV_HOST;
            iot_environment_map["qa_host"]  = ENV_QAT_HOST;
            iot_environment_map["pre_host"] = ENV_PRE_HOST;
            iot_environment_map["product_host"] = ENV_PRODUCT_HOST;

            //if (iot_environment_map[param] != m_iot_environment_def) {
            if (true) {
                NetworkAgent* agent = wxGetApp().getAgent();
                if (param == "dev_host") {
                    app_config->set("iot_environment", ENV_DEV_HOST);
                }
                else if (param == "qa_host") {
                    app_config->set("iot_environment", ENV_QAT_HOST);
                }
                else if (param == "pre_host") {
                    app_config->set("iot_environment", ENV_PRE_HOST);
                }
                else if (param == "product_host") {
                    app_config->set("iot_environment", ENV_PRODUCT_HOST);
                }



                AppConfig* config = GUI::wxGetApp().app_config;
                std::string country_code = config->get_country_code();
                if (agent) {
                    wxGetApp().request_user_logout();
                    agent->set_country_code(country_code);
                }
                ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Warning"), ConfirmBeforeSendDialog::ButtonStyle::ONLY_CONFIRM);
                confirm_dlg.update_text(_L("Switch cloud environment, Please login again!"));
                confirm_dlg.on_show();
            }

            // bbs  backup
            //app_config->set("backup_interval", std::string(m_backup_interval_time.mb_str()));
            app_config->save();
            Slic3r::set_backup_interval(boost::lexical_cast<long>(app_config->get("backup_interval")));

            this->Close();
            break;
        }
        }
    });


    bSizer->Add(enable_ssl_for_mqtt, 0, wxTOP, FromDIP(3));
    bSizer->Add(enable_ssl_for_ftp, 0, wxTOP, FromDIP(3));
    bSizer->Add(item_internal_developer, 0, wxTOP, FromDIP(3));
    bSizer->Add(title_log_level, 0, wxTOP| wxEXPAND, FromDIP(20));
    bSizer->Add(loglevel_combox, 0, wxTOP, FromDIP(3));
    bSizer->Add(title_host, 0, wxTOP| wxEXPAND, FromDIP(20));
    bSizer->Add(radio1, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(radio2, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(radio3, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(radio4, 0, wxEXPAND | wxTOP, FromDIP(3));
    bSizer->Add(debug_button, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(15));

    page->SetSizer(bSizer);
    page->Layout();
    bSizer->Fit(page);
    return page;
}

void PreferencesDialog::on_select_radio(std::string param)
{
    RadioSelectorList::compatibility_iterator it = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (it) {
        RadioSelector *rs = it->GetData();
        if (rs->m_param_name == param) groupid = rs->m_groupid;
        it = it->GetNext();
    }

    it = m_radio_group.GetFirst();
    while (it) {
        RadioSelector *rs = it->GetData();
        if (rs->m_groupid == groupid && rs->m_param_name == param) rs->m_radiobox->SetValue(true);
        if (rs->m_groupid == groupid && rs->m_param_name != param) rs->m_radiobox->SetValue(false);
        it = it->GetNext();
    }
}

wxString PreferencesDialog::get_select_radio(int groupid)
{
    RadioSelectorList::compatibility_iterator it = m_radio_group.GetFirst();
    while (it) {
        RadioSelector *rs = it->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetValue()) { return rs->m_param_name; }
        it = it->GetNext();
    }

    return wxEmptyString;
}

void PreferencesDialog::OnSelectRadio(wxMouseEvent &event)
{
    RadioSelectorList::compatibility_iterator it = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (it) {
        RadioSelector *rs = it->GetData();
        if (rs->m_radiobox->GetId() == event.GetId()) groupid = rs->m_groupid;
        it = it->GetNext();
    }

    it = m_radio_group.GetFirst();
    while (it) {
        RadioSelector *rs = it->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() == event.GetId()) rs->m_radiobox->SetValue(true);
        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() != event.GetId()) rs->m_radiobox->SetValue(false);
        it = it->GetNext();
    }
}


}} // namespace Slic3r::GUI
