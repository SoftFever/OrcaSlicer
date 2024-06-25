#include "SpoolmanImportDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "ExtraRenderers.hpp"
#include "MsgDialog.hpp"

#define BTN_GAP FromDIP(10)
#define BTN_SIZE wxSize(FromDIP(58), FromDIP(24))

namespace Slic3r { namespace GUI {

//-----------------------------------------
// SpoolmanViewModel
//-----------------------------------------

wxDataViewItem SpoolmanViewModel::AddSpool(SpoolmanSpoolShrPtr spool)
{
    m_top_children.emplace_back(std::make_unique<SpoolmanNode>(spool));
    wxDataViewItem item(m_top_children.back().get());
    ItemAdded(wxDataViewItem(nullptr), item);
    return item;
}

void SpoolmanViewModel::SetAllToggles(bool value)
{
    for (auto& item : m_top_children)
        if (item->set_checked(value))
            ItemChanged(wxDataViewItem(item.get()));
}

std::vector<SpoolmanSpoolShrPtr> SpoolmanViewModel::GetSelectedSpools()
{
    std::vector<SpoolmanSpoolShrPtr> spools;
    for (auto& item : m_top_children)
        if (item->get_checked())
            spools.emplace_back(item->get_spool());
    return spools;
}

wxString SpoolmanViewModel::GetColumnType(unsigned int col) const
{
    if (col == COL_CHECK)
        return "bool";
    else if (col == COL_COLOR)
        return "wxColour";
    return "string";
}

unsigned int SpoolmanViewModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    if (parent.IsOk())
        return 0;

    for (auto child : m_top_children)
        array.push_back(wxDataViewItem(child.get()));
    return m_top_children.size();
}

void SpoolmanViewModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    SpoolmanNode* node = get_node(item);
    if (!node)
        return;
    switch (col) {
    case COL_CHECK: variant = node->get_checked(); break;
    case COL_ID: variant = std::to_string(node->get_id()); break;
    case COL_COLOR: variant << wxColour(node->get_color()); break;
    case COL_VENDOR: variant = node->get_vendor_name(); break;
    case COL_NAME: variant = node->get_filament_name(); break;
    case COL_MATERIAL: variant = node->get_material(); break;
    default: wxLogError("Out of bounds column call to SpoolmanViewModel::GetValue. col = %d", col);
    }
}

bool SpoolmanViewModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    if (col == COL_CHECK) {
        get_node(item)->set_checked(variant.GetBool());
        return true;
    }
    wxLogError("Out of bounds column call to SpoolmanViewModel::SetValue. Only column 0 should be set to a value. col = %d", col);
    return false;
}

bool SpoolmanViewModel::IsEnabled(const wxDataViewItem& item, unsigned int col) const { return !get_node(item)->is_archived(); }

//-----------------------------------------
// SpoolmanViewCtrl
//-----------------------------------------

SpoolmanViewCtrl::SpoolmanViewCtrl(wxWindow* parent) : wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES)
{
    wxGetApp().UpdateDVCDarkUI(this);
#if _WIN32
    ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
#else
    SetScrollbar(wxHORIZONTAL, 0, 0, 0);
#endif

    m_model = new SpoolmanViewModel();
    this->AssociateModel(m_model);
    m_model->SetAssociatedControl(this);

    this->AppendToggleColumn(L"\u2714", COL_CHECK, wxDATAVIEW_CELL_ACTIVATABLE, 4 * EM, wxALIGN_CENTER, 0);
    this->AppendTextColumn("ID", COL_ID, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_CENTER, wxCOL_SORTABLE);
    this->AppendColumn(new wxDataViewColumn("Color", new ColorRenderer(), COL_COLOR, wxCOL_WIDTH_AUTOSIZE, wxALIGN_CENTER, 0));
    this->AppendTextColumn("Vendor", COL_VENDOR, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT, wxCOL_SORTABLE);
    this->AppendTextColumn("Name", COL_NAME, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT, wxCOL_SORTABLE);
    this->AppendTextColumn("Material", COL_MATERIAL, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT, wxCOL_SORTABLE);

    // fake column to put the expander in
    auto temp_col = this->AppendTextColumn("", 100);
    temp_col->SetHidden(true);
    this->SetExpanderColumn(temp_col);
}

//-----------------------------------------
// SpoolmanImportDialog
//-----------------------------------------

SpoolmanImportDialog::SpoolmanImportDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Import from Spoolman"), wxDefaultPosition, {-1, 45 * EM}, wxDEFAULT_DIALOG_STYLE)
{
#ifdef _WIN32
    this->SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDlgDarkUI(this);
#else
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

    if (!Spoolman::is_server_valid()) {
        show_error(parent, "Failed to get data from the Spoolman server. Make sure that the port is correct and the server is running.");
        return;
    }

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    // SpoolmanViewCtrl
    m_svc = new SpoolmanViewCtrl(this);
    main_sizer->Add(m_svc, 1, wxCENTER | wxEXPAND | wxALL, EM);

    // Base Preset Label
    auto* label = new Label(this, _L("Base Preset:"));
    wxGetApp().UpdateDarkUI(label);
    main_sizer->Add(label, 0, wxLEFT, EM);

    auto preset_sizer = new wxBoxSizer(wxHORIZONTAL);

    // PresetCombobox
    m_preset_combobox = new TabPresetComboBox(this, Preset::TYPE_FILAMENT);
    preset_sizer->Add(m_preset_combobox, 1, wxEXPAND | wxRIGHT, EM);
    m_preset_combobox->update();

    // Detach Checkbox
    m_detach_checkbox = new wxCheckBox(this, wxID_ANY, _L("Save as Detached"));
    m_detach_checkbox->SetToolTip(_L("Save as a standalone preset"));
    preset_sizer->Add(m_detach_checkbox, 0, wxALIGN_CENTER_VERTICAL);

    main_sizer->Add(preset_sizer, 0, wxEXPAND | wxALL, EM);

    // Buttons
    main_sizer->Add(create_btn_sizer(), 0, wxCENTER | wxEXPAND | wxALL, EM);

    this->SetSizer(main_sizer);

    // Load data into SVC
    for (const auto& spoolman_spool : m_spoolman->get_spoolman_spools(true))
        m_svc->get_model()->AddSpool(spoolman_spool.second);

    int colWidth = 8 * EM; // 4 EM for checkbox (width isn't calculated right), 4 EM for border
    for (int i = COL_ID; i <= COL_MATERIAL; ++i) {
#ifdef _WIN32
        colWidth += m_svc->GetColumnAt(i)->GetWidth();
#else
        colWidth += m_svc->GetColumn(i)->GetWidth();
#endif // _WIN32
    }
    this->SetSize(wxDefaultCoord, wxDefaultCoord, colWidth, wxDefaultCoord, wxSIZE_SET_CURRENT);

    this->ShowModal();
}

void SpoolmanImportDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    for (auto btn : m_button_list) {
        btn->SetMinSize(BTN_SIZE);
        btn->SetCornerRadius(FromDIP(12));
    }

    Fit();
    Refresh();
}

void SpoolmanImportDialog::on_import()
{
    const Preset* current_preset = wxGetApp().preset_bundle->filaments.find_preset(m_preset_combobox->GetStringSelection().ToUTF8().data());
    const vector<SpoolmanSpoolShrPtr>& spools = m_svc->get_model()->GetSelectedSpools();
    if (spools.empty()) {
        show_error(this, "No spools are selected");
        return;
    }

    for (const auto& spool : spools)
        if (spool->get_preset_name() == current_preset->name) {
            show_error(this, "One of the selected spools is the same as the current base preset.\n"
                             "Please deselect that spool or select a different base preset.");
            return;
        }

    bool                                                        detach = m_detach_checkbox->GetValue();
    std::vector<std::pair<SpoolmanSpoolShrPtr, SpoolmanResult>> failed_spools;

    auto create_presets = [&](const vector<SpoolmanSpoolShrPtr>& spools, bool force = false) {
        // Attempt to create the presets
        // Calculating the hash for the internal filament id takes a bit, so using multithreading to speed it up
        std::vector<boost::thread> threads;
        for (const auto& spool : spools) {
            threads.emplace_back(Slic3r::create_thread([&spool, &failed_spools, &current_preset, &force, &detach]() {
                auto res = Spoolman::create_filament_preset_from_spool(spool, current_preset, detach, force);
                if (res.has_failed())
                    failed_spools.emplace_back(spool, res);
            }));
        }

        // Join/wait for threads to finish before continuing
        for (auto& thread : threads)
            if (thread.joinable())
                thread.join();
    };

    create_presets(spools);

    // Show message with any errors
    if (!failed_spools.empty()) {
        //       message      spools with same message
        std::map<std::string, std::vector<SpoolmanSpoolShrPtr>> sorted_error_messages;

        for (const std::pair<SpoolmanSpoolShrPtr, SpoolmanResult>& failed_spool : failed_spools)
            for (const auto& msg : failed_spool.second.messages)
                sorted_error_messages[msg].emplace_back(failed_spool.first);

        std::stringstream error_message;
        for (const auto& msg_pair : sorted_error_messages) {
            for (const auto& errored_spool : msg_pair.second)
                error_message << errored_spool->get_preset_name() << ",\n";
            error_message.seekp(-2, ios_base::end);
            error_message << ":\n";
            error_message << "\t" << msg_pair.first << std::endl << std::endl;
        }

        error_message << "Would you like to ignore these issues and continue?\n"
                   "Presets with the same name will be updated and presets with conflicting IDs will be forcibly created.";

        WarningDialog dlg = WarningDialog(this, error_message.str(), wxEmptyString, wxYES | wxCANCEL);
        if (dlg.ShowModal() == wxID_YES) {
            std::vector<SpoolmanSpoolShrPtr> retry_spools;
            for (const auto& item : failed_spools)
                retry_spools.emplace_back(item.first);
            create_presets(retry_spools, true);
            this->EndModal(wxID_OK);
        }

        // Update the combobox to display any successfully added presets
        m_preset_combobox->update();
        return;
    }
    this->EndModal(wxID_OK);
}

// Orca: Apply buttons style
wxBoxSizer* SpoolmanImportDialog::create_btn_sizer()
{
    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto apply_highlighted_btn_colors = [](Button* btn) {
        btn->SetBackgroundColor(StateColor(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
                                           std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                                           std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)));

        btn->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)));

        btn->SetTextColor(StateColor(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal)));
    };

    auto apply_std_btn_colors = [](Button* btn) {
        btn->SetBackgroundColor(StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                                           std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                                           std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)));

        btn->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)));

        btn->SetTextColor(StateColor(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)));
    };

    auto style_btn = [this, apply_highlighted_btn_colors, apply_std_btn_colors](Button* btn, bool highlight) {
        btn->SetMinSize(BTN_SIZE);
        btn->SetCornerRadius(FromDIP(12));
        if (highlight)
            apply_highlighted_btn_colors(btn);
        else
            apply_std_btn_colors(btn);
    };

    Button* all_btn = new Button(this, _L("All"));
    style_btn(all_btn, false);
    all_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { m_svc->get_model()->SetAllToggles(true); });
    btn_sizer->Add(all_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_button_list.push_back(all_btn);

    Button* none_btn = new Button(this, _L("None"));
    style_btn(none_btn, false);
    none_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { m_svc->get_model()->SetAllToggles(false); });
    btn_sizer->Add(none_btn, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
    m_button_list.push_back(none_btn);

    btn_sizer->AddStretchSpacer();

    Button* import_btn = new Button(this, _L("Import"));
    style_btn(import_btn, true);
    import_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { on_import(); });
    import_btn->SetFocus();
    import_btn->SetId(wxID_OK);
    btn_sizer->Add(import_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
    m_button_list.push_back(import_btn);

    Button* cancel_btn = new Button(this, _L("Cancel"));
    style_btn(cancel_btn, false);
    cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) { this->EndModal(wxID_CANCEL); });
    cancel_btn->SetId(wxID_CANCEL);
    btn_sizer->Add(cancel_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_button_list.push_back(cancel_btn);

    return btn_sizer;
}

}} // namespace Slic3r::GUI