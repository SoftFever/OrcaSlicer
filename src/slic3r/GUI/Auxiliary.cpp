#include "Tab.hpp"
#include "Auxiliary.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/dataview.h>
#include <wx/tglbtn.h>

#include "wxExtensions.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "MainFrame.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_AUXILIARY_IMPORT, wxCommandEvent);
wxDEFINE_EVENT(EVT_AUXILIARY_UPDATE_COVER, wxCommandEvent);
wxDEFINE_EVENT(EVT_AUXILIARY_UPDATE_DELETE, wxCommandEvent);
wxDEFINE_EVENT(EVT_AUXILIARY_UPDATE_RENAME, wxCommandEvent);


const std::vector<std::string> license_list = {
    "BSD License",
    "Apache License",
    "GPL License",
    "LGPL License",
    "MIT License",
    "CC License"
};

AuFile::AuFile(wxWindow *parent, fs::path file_path, wxString file_name, AuxiliaryFolderType type, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
{
    wxPanel::Create(parent, id, pos, wxSize(FromDIP(300), FromDIP(340)), style);
    SetBackgroundColour(AUFILE_GREY300);
    wxBoxSizer *sizer_body = new wxBoxSizer(wxVERTICAL);

   SetSize(wxSize(FromDIP(300), FromDIP(340)));
   SetMinSize(wxSize(FromDIP(300), FromDIP(340)));
   SetMaxSize(wxSize(FromDIP(300), FromDIP(340)));

    m_type      = type;
    m_file_path = file_path;
    m_file_name = file_name;

    if (m_type == MODEL_PICTURE) {
        if (m_file_path.empty()) { return; }
        auto image = new wxImage(encode_path(m_file_path.string().c_str()));

        //constrain
        auto size = wxSize(0, 0);
        float proportion = float(image->GetSize().x) / float(image->GetSize().y);
        if (proportion >= 1) { 
            size.x = FromDIP(300);
            size.y = FromDIP(300) / proportion;
        } else {
            size.y = FromDIP(300);
            size.x = FromDIP(300) * proportion;
        }

        image->Rescale(size.x, size.y);
        m_file_bitmap = wxBitmap(*image);
    } else {
        m_bitmap_excel = create_scaled_bitmap("placeholder_excel", nullptr, 300);
        m_bitmap_pdf   = create_scaled_bitmap("placeholder_pdf", nullptr, 300);
        m_bitmap_txt   = create_scaled_bitmap("placeholder_txt", nullptr, 300);

        if (m_type == OTHERS) {m_file_bitmap = m_bitmap_txt;}
        if (m_type == BILL_OF_MATERIALS) {
            if (m_file_path.extension() == ".xls" || m_file_path.extension() == ".xlsx") {
                m_file_bitmap = m_bitmap_excel;
            }

            if (m_file_path.extension() == ".pdf") { m_file_bitmap = m_bitmap_pdf; }
            
        }
        if (m_type == ASSEMBLY_GUIDE) {m_file_bitmap = m_bitmap_pdf;}
    }
    
    cover_text_left  = _L("Set as cover");
    cover_text_right = _L("Rename");
    cover_text_cover = _L("Cover");

    m_file_cover     = create_scaled_bitmap("auxiliary_cover", this, 50);
    m_file_edit_mask = create_scaled_bitmap("auxiliary_edit_mask", this, 43);
    m_file_delete    = create_scaled_bitmap("auxiliary_delete", this, 28);

    auto m_text_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(300), FromDIP(40)), wxTAB_TRAVERSAL);
    m_text_panel->SetBackgroundColour(AUFILE_GREY300);

    wxBoxSizer *m_text_sizer = new wxBoxSizer(wxVERTICAL);
    m_text_name              = new wxStaticText(m_text_panel, wxID_ANY, m_file_name, wxDefaultPosition, wxSize(FromDIP(300), FromDIP(20)), 0);
    m_text_name->Wrap(FromDIP(290));
    m_text_name->SetFont(::Label::Body_14);

    m_input_name = new ::TextInput(m_text_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(300), FromDIP(35)), wxTE_PROCESS_ENTER);
    m_input_name->GetTextCtrl()->SetFont(::Label::Body_13);
    m_input_name->SetFont(::Label::Body_14);
    m_input_name->Hide();

    m_text_sizer->Add(0, 0, 1, wxEXPAND, 0);
    m_text_sizer->Add(m_text_name, 0, 0, 0);
    m_text_sizer->Add(m_input_name, 0, 0, 0);

    m_text_panel->SetSizer(m_text_sizer);
    m_text_panel->Layout();
    sizer_body->Add(0, 0, 0, wxTOP, FromDIP(300));
    sizer_body->Add(m_text_panel, 0, wxALIGN_CENTER, 0);

    SetSizer(sizer_body);
    Layout();
    Fit();

    Bind(wxEVT_PAINT, &AuFile::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, &AuFile::OnEraseBackground, this);
    Bind(wxEVT_ENTER_WINDOW, &AuFile::on_mouse_enter, this);
    Bind(wxEVT_LEAVE_WINDOW, &AuFile::on_mouse_leave, this);
    Bind(wxEVT_LEFT_UP, &AuFile::on_mouse_left_up, this);
    Bind(wxEVT_LEFT_DCLICK, &AuFile::on_dclick, this);
    m_input_name->Bind(wxEVT_TEXT_ENTER, &AuFile::on_input_enter, this);
}

void AuFile::enter_rename_mode()
{
    m_input_name->Show();
    m_text_name->Hide();
    Layout();
}

void AuFile::exit_rename_mode()
{
    m_input_name->Hide();
    m_text_name->Show();
    Layout();
}

void AuFile::OnPaint(wxPaintEvent &event)
{
    //wxPaintDC dc(this);
    //render(dc);
    wxBufferedPaintDC dc(this);
    PrepareDC(dc);
    PaintBackground(dc);
    PaintForeground(dc);
}

void AuFile::PaintBackground(wxDC &dc)
{
    wxColour backgroundColour = GetBackgroundColour();
    if (!backgroundColour.Ok()) backgroundColour = AUFILE_GREY300;
    dc.SetBrush(wxBrush(backgroundColour));
    dc.SetPen(wxPen(backgroundColour, 1));
    wxRect windowRect(wxPoint(0, 0), GetClientSize());

    //CalcUnscrolledPosition(windowRect.x, windowRect.y, &windowRect.x, &windowRect.y);
    dc.DrawRectangle(windowRect);


    wxSize size = wxSize(FromDIP(300), FromDIP(300));
    dc.SetPen(AUFILE_GREY200);
    dc.SetBrush(AUFILE_GREY200);
    dc.DrawRectangle(0,0,size.x,size.y);
    dc.DrawBitmap(m_file_bitmap, (size.x - m_file_bitmap.GetSize().x) / 2, (size.y - m_file_bitmap.GetSize().y) / 2);
}

void AuFile::OnEraseBackground(wxEraseEvent &evt) {}

void AuFile::PaintForeground(wxDC &dc)
{
    wxSize size = wxSize(FromDIP(300), FromDIP(300));

    if (m_hover) {
        dc.DrawBitmap(m_file_edit_mask, 0, size.y - m_file_edit_mask.GetSize().y);
        dc.SetFont(Label::Body_14);
        dc.SetTextForeground(*wxWHITE);
        if (m_type == MODEL_PICTURE) {
            // left text
            auto sizet = dc.GetTextExtent(cover_text_left);
            auto pos   = wxPoint(0, 0);
            pos.x      = (size.x / 2 - sizet.x) / 2;
            pos.y      = (size.y - (m_file_edit_mask.GetSize().y + sizet.y) / 2);
            dc.DrawText(cover_text_left, pos);

            // right text
            sizet = dc.GetTextExtent(cover_text_right);
            pos   = wxPoint(0, 0);
            pos.x = size.x / 2 + (size.x / 2 - sizet.x) / 2;
            pos.y = (size.y - (m_file_edit_mask.GetSize().y + sizet.y) / 2);
            dc.DrawText(cover_text_right, pos);

            // Split
            dc.SetPen(AUFILE_GREY700);
            dc.SetBrush(AUFILE_GREY700);
            pos   = wxPoint(0, 0);
            pos.x = size.x / 2 - 1;
            pos.y = size.y - FromDIP(30) - (m_file_edit_mask.GetSize().y - FromDIP(30)) / 2;
            dc.DrawRectangle(pos.x, pos.y, 2, FromDIP(30));
        } else {
            // right text
            auto sizet = dc.GetTextExtent(cover_text_right);
            auto pos   = wxPoint(0, 0);
            pos.x = (size.x  - sizet.x) / 2;
            pos.y = (size.y - (m_file_edit_mask.GetSize().y + sizet.y) / 2);
            dc.DrawText(cover_text_right, pos);
        }       
    }

    if (m_cover) {
        dc.SetTextForeground(*wxWHITE);
        dc.DrawBitmap(m_file_cover, size.x - m_file_cover.GetSize().x, 0);
        dc.SetFont(Label::Body_12);
        auto sizet = dc.GetTextExtent(cover_text_cover);
        auto pos   = wxPoint(0, 0);
        pos.x      = size.x - sizet.x - FromDIP(3);
        pos.y      = FromDIP(3);
        dc.DrawText(cover_text_cover, pos);
    }

    if (m_hover) { dc.DrawBitmap(m_file_delete, size.x - m_file_delete.GetSize().x - FromDIP(15), FromDIP(15)); }
}

void AuFile::on_mouse_enter(wxMouseEvent &evt)
{
    m_hover = true;
    Refresh();
}

void AuFile::on_mouse_leave(wxMouseEvent &evt)
{
    m_hover = false;
    Refresh();
}

void AuFile::on_input_enter(wxCommandEvent &evt)
{
    auto     new_file_name = into_u8(m_input_name->GetTextCtrl()->GetValue());
    auto     m_valid_type  = Valid;
    wxString info_line;

    const char *unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified(); //"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (new_file_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line    = _L("Name is invalid;") + "\n" + _L("illegal characters:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && new_file_name.find(unusable_suffix) != std::string::npos) {
        info_line    = _L("Name is invalid;") + "\n" + _L("illegal suffix:") + "\n\t" + from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    auto     existing  = false;
    auto     dir       = m_file_path.branch_path();
    auto     new_fullname = new_file_name + m_file_path.extension().string();

    
    auto new_fullname_path = dir.string() + "/" + new_fullname;
    fs::path new_dir_path(new_fullname_path.c_str());
    

    if (fs::exists(new_dir_path)) existing = true;

    if (m_valid_type == Valid && existing) {
        info_line    = from_u8((boost::format(_u8L("The name \"%1%\" already exists.")) % new_file_name).str());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.empty()) {
        info_line    = _L("The name is not allowed to be empty.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.find_first_of(' ') == 0) {
        info_line    = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.find_last_of(' ') == new_file_name.length() - 1) {
        info_line    = _L("The name is not allowed to end with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid) {
        fs::path oldPath = m_file_path;
        fs::path newPath(new_dir_path);
        fs::rename(oldPath, newPath);
    } else {
        MessageDialog msg_wingow(nullptr, info_line, wxEmptyString,
                                 wxICON_WARNING | wxOK);
        if (msg_wingow.ShowModal() == wxID_CANCEL) {
            m_input_name->GetTextCtrl()->SetValue(wxEmptyString);
            return;
        }

        return;
    }

    // post event
    auto event = wxCommandEvent(EVT_AUXILIARY_UPDATE_RENAME);
    event.SetString(wxString::Format("%s|%s|%s", s_default_folders[m_type], m_file_path.string(), new_dir_path.string()));
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);

    // update layout
    m_file_path = new_dir_path.string();
    m_file_name = new_fullname;
    m_text_name->SetLabel(new_fullname);
    exit_rename_mode();
    // evt.Skip();
}

void AuFile::on_dclick(wxMouseEvent &evt) 
{
     wxLaunchDefaultApplication(m_file_path.wstring(), 0);
}

void AuFile::on_mouse_left_up(wxMouseEvent &evt)
{
    wxSize size = wxSize(FromDIP(300), FromDIP(300));

    auto pos = evt.GetPosition();
    // set cover
    auto mask_size    = m_file_edit_mask.GetSize();
    auto cover_left   = 0;
    auto cover_top    = size.y - mask_size.y;
    auto cover_right  = mask_size.x / 2;
    auto cover_bottom = size.y;

    if (pos.x > cover_left && pos.x < cover_right && pos.y > cover_top && pos.y < cover_bottom) { on_set_cover(); }

    // rename
    auto rename_left   = mask_size.x / 2;
    auto rename_top    = size.y - mask_size.y;
    auto rename_right  = mask_size.x;
    auto rename_bottom = size.y;
    if (pos.x > rename_left && pos.x < rename_right && pos.y > rename_top && pos.y < rename_bottom) { on_set_rename(); }

    // close
    auto close_left   = size.x - m_file_delete.GetSize().x - FromDIP(15);
    auto close_top    = FromDIP(15);
    auto close_right  = size.x - FromDIP(15);
    auto close_bottom = m_file_delete.GetSize().y + FromDIP(15);
    if (pos.x > close_left && pos.x < close_right && pos.y > close_top && pos.y < close_bottom) { on_set_delete(); }
}

void AuFile::on_set_cover()
{
    if (wxGetApp().plater()->model().model_info == nullptr) { wxGetApp().plater()->model().model_info = std::make_shared<ModelInfo>(); }

    wxGetApp().plater()->model().model_info->cover_file = m_file_name.ToStdString();

    auto full_path          = m_file_path.branch_path();
    auto full_root_path         = full_path.branch_path();
    auto full_root_path_str = encode_path(full_root_path.string().c_str());
    auto dir       = wxString::Format("%s/.thumbnails", full_root_path_str);

    fs::path dir_path(dir.c_str());

    if (!fs::exists(dir_path)) {
        fs::create_directory(dir_path); 
    }

    bool result = true;
    wxImage thumbnail_img;;
    result = generate_image(m_file_path.string(), thumbnail_img, _3MF_COVER_SIZE);
    if (result) {
        auto cover_img_path = dir_path.string() + "/thumbnail_3mf.png";
        thumbnail_img.SaveFile(encode_path(cover_img_path.c_str()));
    }

    result = generate_image(m_file_path.string(), thumbnail_img, PRINTER_THUMBNAIL_SMALL_SIZE, GERNERATE_IMAGE_CROP_VERTICAL);
    if (result) {
        auto small_img_path = dir_path.string() + "/thumbnail_small.png";
        thumbnail_img.SaveFile(encode_path(small_img_path.c_str()));
    }

    result = generate_image(m_file_path.string(), thumbnail_img, PRINTER_THUMBNAIL_MIDDLE_SIZE);
    if (result) {
        auto middle_img_path = dir_path.string() + "/thumbnail_middle.png";
        thumbnail_img.SaveFile(encode_path(middle_img_path.c_str()));
    }

    auto evt = wxCommandEvent(EVT_AUXILIARY_UPDATE_COVER);
    evt.SetString(s_default_folders[m_type]);
    evt.SetEventObject(m_parent);
    wxPostEvent(m_parent, evt);
}

void AuFile::on_set_delete()
{
    fs::path bfs_path = m_file_path;
    auto     is_fine = fs::remove(bfs_path);

    if (m_cover) {
        auto full_path          = m_file_path.branch_path();
        auto full_root_path     = full_path.branch_path();
        auto full_root_path_str = encode_path(full_root_path.string().c_str());
        auto dir                = wxString::Format("%s/.thumbnails", full_root_path_str);
        fs::path dir_path(dir.c_str());

        auto cover_img_path = dir_path.string() + "/thumbnail_3mf.png";
        auto small_img_path = dir_path.string() + "/thumbnail_small.png";
        auto middle_img_path = dir_path.string() + "/thumbnail_middle.png";

        if (fs::exists(fs::path(cover_img_path))) { fs::remove(fs::path(cover_img_path));}
        if (fs::exists(fs::path(small_img_path))) { fs::remove(fs::path(small_img_path)); }
        if (fs::exists(fs::path(middle_img_path))) { fs::remove(fs::path(middle_img_path)); }
    }

    if (wxGetApp().plater()->model().model_info == nullptr) { wxGetApp().plater()->model().model_info = std::make_shared<ModelInfo>(); }
    if (wxGetApp().plater()->model().model_info->cover_file == m_file_name) { wxGetApp().plater()->model().model_info->cover_file = ""; }

    if (is_fine) {
        auto evt = wxCommandEvent(EVT_AUXILIARY_UPDATE_DELETE);
        evt.SetString(wxString::Format("%s|%s", s_default_folders[m_type], m_file_path.string()));
        evt.SetEventObject(m_parent);
        wxPostEvent(m_parent, evt);
    }
}

void AuFile::on_set_rename() { enter_rename_mode(); }

void AuFile::on_set_open() {}

void AuFile::set_cover(bool cover)
{
    m_cover = cover;
    Refresh();
}

AuFile::~AuFile() {}

void AuFile::msw_rescale() 
{ 
    m_file_cover     = create_scaled_bitmap("auxiliary_cover", this, 50);
    m_file_edit_mask = create_scaled_bitmap("auxiliary_edit_mask", this, 43);
    m_file_delete    = create_scaled_bitmap("auxiliary_delete", this, 28);

    if (m_type == MODEL_PICTURE) {
        if (m_file_path.empty()) { return;}
        auto image = new wxImage(encode_path(m_file_path.string().c_str()));
        // constrain
        auto  size       = wxSize(0, 0);
        float proportion = float(image->GetSize().x) / float(image->GetSize().y);
        if (proportion >= 1) {
            size.x = FromDIP(300);
            size.y = FromDIP(300) / proportion;
        } else {
            size.y = FromDIP(300);
            size.x = FromDIP(300) * proportion;
        }

        image->Rescale(size.x, size.y);
        m_file_bitmap = wxBitmap(*image);
    } else {
        m_bitmap_excel = create_scaled_bitmap("placeholder_excel", nullptr, 300);
        m_bitmap_pdf   = create_scaled_bitmap("placeholder_pdf", nullptr, 300);
        m_bitmap_txt   = create_scaled_bitmap("placeholder_txt", nullptr, 300);

        if (m_type == OTHERS) { m_file_bitmap = m_bitmap_txt; }
        if (m_type == BILL_OF_MATERIALS) { m_file_bitmap = m_bitmap_excel; }
        if (m_type == ASSEMBLY_GUIDE) { m_file_bitmap = m_bitmap_pdf; }
    }
    Refresh();
}

AuFolderPanel::AuFolderPanel(wxWindow *parent, AuxiliaryFolderType type, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    m_type = type;
    SetBackgroundColour(AUFILE_GREY300);
    wxBoxSizer *sizer_main = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);
    wxBoxSizer *sizer_body = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *sizer_top  = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
                            std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor btn_bd_white(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    m_button_add = new Button(m_scrolledWindow, _L("Add"), "auxiliary_add_file", 12, 12);
    m_button_add->SetBackgroundColor(btn_bg_white);
    m_button_add->SetBorderColor(btn_bd_white);
    m_button_add->SetMinSize(wxSize(FromDIP(80), FromDIP(24)));
    m_button_add->SetCornerRadius(12);
    m_button_add->SetFont(Label::Body_14);
    // m_button_add->Bind(wxEVT_LEFT_UP, &AuxiliaryPanel::on_add, this);

    /*m_button_del = new Button(m_scrolledWindow, _L("Delete"), "auxiliary_delete_file", 12, 12);
    m_button_del->SetBackgroundColor(btn_bg_white);
    m_button_del->SetBorderColor(btn_bd_white);
    m_button_del->SetMinSize(wxSize(FromDIP(80), FromDIP(24)));
    m_button_del->SetCornerRadius(12);
    m_button_del->SetFont(Label::Body_14);*/
    // m_button_del->Bind(wxEVT_LEFT_UP, &AuxiliaryPanel::on_delete, this);

    sizer_top->Add(0, 0, 0, wxLEFT, FromDIP(10));
    sizer_top->Add(m_button_add, 0, wxALL, 0);
    // sizer_top->Add(m_button_del, 0, wxALL, 0);

    m_gsizer_content = new wxGridSizer(0, 3, FromDIP(18), FromDIP(18));
    sizer_body->Add(sizer_top, 0, wxEXPAND | wxTOP, FromDIP(35));
    sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(30));
    sizer_body->Add(m_gsizer_content, 0, 0, 0);
    m_scrolledWindow->SetSizer(sizer_body);
    m_scrolledWindow->Layout();
    sizer_main->Add(m_scrolledWindow, 1, wxEXPAND | wxLEFT, FromDIP(40));

    this->SetSizer(sizer_main);
    this->Layout();

    m_button_add->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AuFolderPanel::on_add), NULL, this);
    // m_button_del->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AuFolderPanel::on_delete), NULL, this);
}

void AuFolderPanel::clear()
{
    for (auto i = 0; i < m_aufiles_list.GetCount(); i++) {
        AuFiles *aufile = m_aufiles_list[i];
        if (aufile->file != NULL) { aufile->file->Destroy(); }
    }
    m_aufiles_list.clear();
    m_gsizer_content->Layout();
}

void AuFolderPanel::update(std::vector<fs::path> paths)
{
    clear();
    for (auto i = 0; i < paths.size(); i++) {
        std::string temp_name   = fs::path(paths[i].c_str()).filename().string();
        auto name             = encode_path(temp_name.c_str());

        auto        aufile = new AuFile(m_scrolledWindow, paths[i], name, m_type, wxID_ANY);
        m_gsizer_content->Add(aufile, 0, 0, 0);
        auto af  = new AuFiles;
        af->path = paths[i].string();
        af->file = aufile;
        m_aufiles_list.push_back(af);
    }
    m_gsizer_content->Layout();
    Layout();
}

void AuFolderPanel::msw_rescale() 
{
    m_button_add->SetMinSize(wxSize(FromDIP(80), FromDIP(24)));
    for (auto i = 0; i < m_aufiles_list.GetCount(); i++) {
        AuFiles *aufile = m_aufiles_list[i];
        aufile->file->msw_rescale();
    }
}

void AuFolderPanel::on_add(wxCommandEvent &event)
{
    auto evt = wxCommandEvent(EVT_AUXILIARY_IMPORT);
    evt.SetString(s_default_folders[m_type]);
    evt.SetEventObject(m_parent);
    wxPostEvent(m_parent, evt);
}

void AuFolderPanel::on_delete(wxCommandEvent &event) { clear(); }

AuFolderPanel::~AuFolderPanel()
{
    m_button_add->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AuFolderPanel::on_add), NULL, this);
    // m_button_del->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AuFolderPanel::on_delete), NULL, this);
}

void AuFolderPanel::update_cover()
{
    if (wxGetApp().plater()->model().model_info != nullptr) {
        for (auto i = 0; i < m_aufiles_list.GetCount(); i++) {
            AuFiles *aufile = m_aufiles_list[i];
            if (wxGetApp().plater()->model().model_info->cover_file == aufile->file->m_file_name) {
                aufile->file->set_cover(true);
            } else {
                aufile->file->set_cover(false);
            }
        }
    }
}

AuxiliaryPanel::AuxiliaryPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style) : wxPanel(parent, id, pos, size, style)
{
    init_tabpanel();
    // init_auxiliary();

    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxLEFT, 0);
    SetSizerAndFit(m_main_sizer);
    Bind(wxEVT_SIZE, &AuxiliaryPanel::on_size, this);
    Bind(EVT_AUXILIARY_IMPORT, &AuxiliaryPanel::on_import_file, this);

    // cover -event
    Bind(EVT_AUXILIARY_UPDATE_COVER, [this](wxCommandEvent &e) { update_all_cover(); });

    // delete event
    Bind(EVT_AUXILIARY_UPDATE_DELETE, [this](wxCommandEvent &e) {
        auto info_str = e.GetString();
        auto parems   = std::vector<std::string>{};
        Split(info_str.ToStdString(), "|", parems);
        auto model = parems[0];
        auto name  = parems[1];

        auto iter = m_paths_list.find(model);
        if (iter != m_paths_list.end()) {
            auto list = iter->second;
            for (auto i = 0; i < list.size(); i++) {
                if (list[i] == name) {
                    list.erase(std::begin(list) + i);
                    break;
                }
            }

            m_paths_list[model] = list;
            update_all_panel();
            update_all_cover();
        }
    });

    // rename event
    Bind(EVT_AUXILIARY_UPDATE_RENAME, [this](wxCommandEvent &e) {
        auto info_str = e.GetString();

        auto parems = std::vector<std::string>{};
        Split(info_str.ToStdString(), "|", parems);

        auto model    = parems[0];
        auto old_name = parems[1];
        auto new_name = parems[2];

        auto iter = m_paths_list.find(model);
        if (iter != m_paths_list.end()) {
            auto list = iter->second;
            for (auto i = 0; i < list.size(); i++) {
                if (list[i] == old_name) {
                    list[i] = new_name;
                    break;
                }
            }

            m_paths_list[model] = list;
        }
    });
}

void AuxiliaryPanel::Split(const std::string &src, const std::string &separator, std::vector<std::string> &dest)
{
    std::string            str = src;
    std::string            substring;
    std::string::size_type start = 0, index;
    dest.clear();
    index = str.find_first_of(separator, start);
    do {
        if (index != string::npos) {
            substring = str.substr(start, index - start);
            dest.push_back(substring);
            start = index + separator.size();
            index = str.find(separator, start);
            if (start == string::npos) break;
        }
    } while (index != string::npos);

    // the last part
    substring = str.substr(start);
    dest.push_back(substring);
}

AuxiliaryPanel::~AuxiliaryPanel() {}

void AuxiliaryPanel::init_bitmap()
{
    /*m_signal_strong_img = create_scaled_bitmap("monitor_signal_strong", nullptr, 24);
    m_signal_middle_img = create_scaled_bitmap("monitor_signal_middle", nullptr, 24);
    m_signal_weak_img   = create_scaled_bitmap("monitor_signal_weak", nullptr, 24);
    m_signal_no_img     = create_scaled_bitmap("monitor_signal_no", nullptr, 24);
    m_printer_img       = create_scaled_bitmap("monitor_printer", nullptr, 26);
    m_arrow_img         = create_scaled_bitmap("monitor_arrow", nullptr, 14);*/
}

void AuxiliaryPanel::init_tabpanel()
{
    auto        m_side_tools     = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(220), FromDIP(18)));
    wxBoxSizer *sizer_side_tools = new wxBoxSizer(wxVERTICAL);
    sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);
    m_tabpanel = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->SetBackgroundColour(*wxWHITE);
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent &e) { ; });

#if !BBL_RELEASE_TO_PUBLIC
    m_designer_panel = new DesignerPanel(m_tabpanel, AuxiliaryFolderType::DESIGNER);
#endif

    m_pictures_panel          = new AuFolderPanel(m_tabpanel, AuxiliaryFolderType::MODEL_PICTURE);
    m_bill_of_materials_panel = new AuFolderPanel(m_tabpanel, AuxiliaryFolderType::BILL_OF_MATERIALS);
    m_assembly_panel          = new AuFolderPanel(m_tabpanel, AuxiliaryFolderType::ASSEMBLY_GUIDE);
    m_others_panel            = new AuFolderPanel(m_tabpanel, AuxiliaryFolderType::OTHERS);

#if !BBL_RELEASE_TO_PUBLIC
    m_tabpanel->AddPage(m_designer_panel, _L("Basic Info"), "", true);
    m_tabpanel->AddPage(m_pictures_panel, _L("Pictures"), "", false);
#else
    m_tabpanel->AddPage(m_pictures_panel, _L("Pictures"), "", true);
#endif
    m_tabpanel->AddPage(m_bill_of_materials_panel, _L("Bill of Materials"), "", false);
    m_tabpanel->AddPage(m_assembly_panel, _L("Assembly Guide"), "", false);
    m_tabpanel->AddPage(m_others_panel, _L("Others"), "", false);
}

wxWindow *AuxiliaryPanel::create_side_tools()
{
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    auto        panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(0, FromDIP(50)));
    panel->SetBackgroundColour(wxColour(135, 206, 250));
    panel->SetSizer(sizer);
    sizer->Layout();
    panel->Fit();
    return panel;
}

void AuxiliaryPanel::msw_rescale() { 
    m_pictures_panel->msw_rescale();
    m_bill_of_materials_panel->msw_rescale();
    m_assembly_panel->msw_rescale();
    m_others_panel->msw_rescale();
#if !BBL_RELEASE_TO_PUBLIC
    m_designer_panel->msw_rescale();
#endif
}

void AuxiliaryPanel::on_size(wxSizeEvent &event)
{
    if (!wxGetApp().mainframe) return;
    Layout();
    Refresh();
}

bool AuxiliaryPanel::Show(bool show) { return wxPanel::Show(show); }

// core logic
void AuxiliaryPanel::init_auxiliary()
{
    Model &model = wxGetApp().plater()->model();
    m_root_dir   = encode_path(model.get_auxiliary_file_temp_path().c_str());
    if (wxDirExists(m_root_dir)) {
        fs::path path_to_del(m_root_dir.ToStdWstring());
        try {
            fs::remove_all(path_to_del);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing the auxiliary directory " << m_root_dir.c_str();
        }
    }

    fs::path top_dir_path(m_root_dir.ToStdWstring());
    fs::create_directory(top_dir_path);

    for (auto folder : s_default_folders) create_folder(folder);
}

void AuxiliaryPanel::on_import_file(wxCommandEvent &event)
{
    auto file_model = event.GetString();

    wxString     src_path;
    wxString     dst_path;

     wxString wildcard = wxFileSelectorDefaultWildcardStr;

    if (file_model == s_default_folders[MODEL_PICTURE]) {
        //wildcard = wxT("JPEG files (*.jpeg)|*.jpeg|BMP files (*.bmp)|*.bmp|GIF files (*.gif)|*.gif|PNG files (*.png)|*.png|JPG files (*.jpg)|*.jpg");
        wildcard = wxT("files (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp");
    } 

    if (file_model == s_default_folders[OTHERS]) {  wildcard = wxT("TXT files (*.txt)|*.txt"); }
    if (file_model == s_default_folders[BILL_OF_MATERIALS]){ wildcard = wxT("EXCEL files (*.xls)|*.xls|EXCEL files (*.xlsx)|*.xlsx|PDF files (*.pdf)|*.pdf"); }
    if (file_model == s_default_folders[ASSEMBLY_GUIDE]) { wildcard = wxT("PDF files (*.pdf)|*.pdf"); }

    wxFileDialog dialog(this, _L("Choose files"), wxEmptyString, wxEmptyString, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
    if (dialog.ShowModal() == wxID_OK) {
        wxArrayString file_paths;
        dialog.GetPaths(file_paths);

        for (wxString file_path : file_paths) {
            // Copy imported file to project temp directory
            fs::path src_bfs_path(file_path.ToStdWstring());
            wxString dir_path = m_root_dir;
            dir_path += "/" + file_model;
            


            auto is_exist = false;
            auto iter = m_paths_list.find(file_model.ToStdString());
            if (iter != m_paths_list.end()) {
                std::vector<fs::path> list = iter->second;
                for (auto i = 0; i < list.size(); i++) {
                    if (src_bfs_path.filename() == list[i].filename()) { 
                        is_exist = true;
                        break;
                    }
                }
            }
            
            if (!is_exist) {
                dir_path += "/" + src_bfs_path.filename().generic_wstring();
            } else {
                time_t t1 = time(0);
                char   ch1[64];
                strftime(ch1, sizeof(ch1), "%T", localtime(&t1));
                std::string time_text = ch1;

                auto before_name = replaceSpace(src_bfs_path.filename().string(), src_bfs_path.extension().string(), "");
                time_text = replaceSpace(time_text, ":", "_");
                dir_path += "/" + before_name + "_" + time_text + src_bfs_path.extension().wstring();
            }
           

            boost::system::error_code ec;
            if (!fs::copy_file(src_bfs_path, fs::path(dir_path.ToStdWstring()), fs::copy_option::overwrite_if_exists, ec)) continue;
            Slic3r::put_other_changes();

            // add in file list
            iter = m_paths_list.find(file_model.ToStdString());
            auto file_fs_path = fs::path(dir_path.c_str());
            if (iter != m_paths_list.end()) {
                m_paths_list[file_model.ToStdString()].push_back(file_fs_path);
            } else {
                m_paths_list[file_model.ToStdString()] = std::vector<fs::path>{file_fs_path};
            }
        }
        update_all_panel();
        update_all_cover();
    }
}

void AuxiliaryPanel::create_folder(wxString name)
{
    wxString folder_name = name;

    // Create folder in file system
    fs::path bfs_path((m_root_dir + "/" + folder_name).ToStdWstring());
    if (fs::exists(bfs_path)) {
        try {
            bool is_done = fs::remove_all(bfs_path);
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "Failed  removing the auxiliary directory " << m_root_dir.c_str();
        }
    }
    fs::create_directory(bfs_path);
}

std::string AuxiliaryPanel::replaceSpace(std::string s, std::string ts, std::string ns)
{
    int    index   = -1;
    while ((index = s.find(ts.c_str())) >= 0) { s = s.replace(index, ts.length(), ns.c_str()); }
    return s;
}

void AuxiliaryPanel::Reload(wxString aux_path)
{
    fs::path new_aux_path(aux_path.ToStdWstring());

    try {
        fs::remove_all(fs::path(m_root_dir.ToStdWstring()));
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Failed  removing the auxiliary directory " << m_root_dir.c_str();
    }

    m_root_dir = aux_path;
    m_paths_list.clear();
    // Check new path. If not exist, create a new one.
    if (!fs::exists(new_aux_path)) {
        fs::create_directory(new_aux_path);
        // Create default folders if they are not loaded
        for (auto folder : s_default_folders) {
            wxString folder_path = aux_path + "/" + folder;
            if (fs::exists(folder_path.ToStdWstring())) continue;
            fs::create_directory(folder_path.ToStdWstring());
        }
        update_all_panel();
        #if !BBL_RELEASE_TO_PUBLIC
        m_designer_panel->update_info();
        #endif
        return;
    }

    // Load from new path
    std::vector<fs::path>  dir_cache;
    fs::directory_iterator iter_end;

    for (fs::directory_iterator iter(new_aux_path); iter != iter_end; iter++) {
        wxString path = iter->path().generic_wstring();
        dir_cache.push_back(iter->path());
    }

    for (auto dir : dir_cache) {
        for (fs::directory_iterator iter(dir); iter != iter_end; iter++) {
            if (fs::is_directory(iter->path())) continue;
            wxString file_path     = iter->path().generic_wstring();
            //auto     file_path_str = encode_path(file_path.c_str());

            for (auto folder : s_default_folders) {
                auto idx = file_path.find(folder.ToStdString());
                if (idx != std::string::npos) {
                    auto iter = m_paths_list.find(folder.ToStdString());
                    auto     file_path_str = fs::path(file_path.c_str());


                    if (iter != m_paths_list.end()) {
                        m_paths_list[folder.ToStdString()].push_back(file_path_str);
                        break;
                    } else {
                        m_paths_list[folder.ToStdString()] = std::vector<fs::path>{file_path_str};
                        break;
                    }
                }
            }
        }
    }

    // Create default folders if they are not loaded
    wxDataViewItemArray default_items;
    for (auto folder : s_default_folders) {
        wxString folder_path = aux_path + "/" + folder;
        if (fs::exists(folder_path.ToStdWstring())) continue;
        fs::create_directory(folder_path.ToStdWstring());
    }

    update_all_panel();
    update_all_cover();
    #if !BBL_RELEASE_TO_PUBLIC
    m_designer_panel->update_info();
    #endif
}

void AuxiliaryPanel::update_all_panel()
{
    std::map<std::string, std::vector<fs::path>>::iterator mit;

    m_pictures_panel->clear();
    m_bill_of_materials_panel->clear();
    m_assembly_panel->clear();
    m_others_panel->clear();

    for (mit = m_paths_list.begin(); mit != m_paths_list.end(); mit++) {
        if (mit->first == "Model Pictures") { m_pictures_panel->update(mit->second); }
        if (mit->first == "Bill of Materials") { m_bill_of_materials_panel->update(mit->second); }
        if (mit->first == "Assembly Guide") { m_assembly_panel->update(mit->second); }
        if (mit->first == "Others") { m_others_panel->update(mit->second); }
    }
}

void AuxiliaryPanel::update_all_cover()
{
    std::map<std::string, std::vector<fs::path>>::iterator mit;
    for (mit = m_paths_list.begin(); mit != m_paths_list.end(); mit++) {
        if (mit->first == "Model Pictures") { m_pictures_panel->update_cover(); }
    }
}


 DesignerPanel::DesignerPanel(wxWindow *          parent,
                             AuxiliaryFolderType type,
                             wxWindowID          id /*= wxID_ANY*/,
                             const wxPoint &     pos /*= wxDefaultPosition*/,
                             const wxSize &      size /*= wxDefaultSize*/,
                             long                style /*= wxTAB_TRAVERSAL*/)
     : wxPanel(parent, id, pos, size, style)
{
     SetBackgroundColour(AUFILE_GREY300);
     wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
     wxBoxSizer *m_sizer_designer = new wxBoxSizer(wxHORIZONTAL);

     auto m_text_designer = new wxStaticText(this, wxID_ANY, _L("Designer"), wxDefaultPosition, wxSize(120, -1), 0);
     m_text_designer->Wrap(-1);
     m_sizer_designer->Add(m_text_designer, 0, wxALIGN_CENTER, 0);

     m_input_designer =  new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(450), FromDIP(30)), wxTE_PROCESS_ENTER);
     m_input_designer->GetTextCtrl()->SetFont(::Label::Body_14);
     m_input_designer->GetTextCtrl()->SetSize(wxSize(FromDIP(450), FromDIP(22)));
     m_sizer_designer->Add(m_input_designer, 0, wxALIGN_CENTER, 0);

     wxBoxSizer *m_sizer_model_name = new wxBoxSizer(wxHORIZONTAL);

     auto m_text_model_name = new wxStaticText(this, wxID_ANY, _L("Model Name"), wxDefaultPosition, wxSize(120, -1), 0);
     m_text_model_name->Wrap(-1);
     m_sizer_model_name->Add(m_text_model_name, 0, wxALIGN_CENTER, 0);

     m_imput_model_name =  new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition,wxSize(FromDIP(450),FromDIP(30)), wxTE_PROCESS_ENTER);
     m_imput_model_name->GetTextCtrl()->SetFont(::Label::Body_14);
     m_imput_model_name->GetTextCtrl()->SetSize(wxSize(FromDIP(450), FromDIP(22)));
     m_sizer_model_name->Add(m_imput_model_name, 0, wxALIGN_CENTER, 0);

     /*
     wxBoxSizer *m_sizer_license = new wxBoxSizer(wxHORIZONTAL);
     auto m_text_license = new wxStaticText(this, wxID_ANY, _L("License"), wxDefaultPosition, wxSize(120, -1), 0);
     m_text_license->Wrap(-1);
     m_sizer_license->Add(m_text_license, 0, wxALIGN_CENTER, 0);

     m_combo_license = new wxComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(450, -1), 0, NULL, wxCB_READONLY);
     m_sizer_license->Add(m_combo_license, 0, wxALIGN_CENTER, 0);
     */
     m_sizer_body->Add( 0, 0, 0, wxTOP, FromDIP(50) );
     m_sizer_body->Add(m_sizer_designer, 0, wxLEFT, FromDIP(50));
     m_sizer_body->Add( 0, 0, 0, wxTOP, FromDIP(20));
     m_sizer_body->Add(m_sizer_model_name, 0, wxLEFT, FromDIP(50));
     //m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(20));
     //m_sizer_body->Add(m_sizer_license, 0, wxLEFT, FromDIP(50));
     //init_license_list();

     SetSizer(m_sizer_body);
     Layout();
     Fit();

     m_input_designer->Bind(wxEVT_TEXT, &DesignerPanel::on_input_enter_designer, this);
     m_imput_model_name->Bind(wxEVT_TEXT, &DesignerPanel::on_input_enter_model, this);
     //m_combo_license->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(DesignerPanel::on_select_license), NULL, this);
}

 DesignerPanel::~DesignerPanel()
 {
     //m_combo_license->Disconnect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(DesignerPanel::on_select_license), NULL, this);
 }

 void DesignerPanel::init_license_list()
 {
     /*
     wxArrayString text_licese;
     for (int i = 0; i < license_list.size(); i++) {
         text_licese.Add(license_list[i]);
     }
     m_combo_license->Set(text_licese);
     */
 }

 void DesignerPanel::on_select_license(wxCommandEvent&evt)
 {
     int selected = evt.GetInt();
     if (selected >= 0 && selected < license_list.size()) {
         if (wxGetApp().plater()->model().model_info == nullptr) {
             wxGetApp().plater()->model().model_info = std::make_shared<ModelInfo>();
         }
         if (wxGetApp().plater()->model().model_info != nullptr) {
             wxGetApp().plater()->model().model_info->license = license_list[selected];
         }
     }
 }

bool DesignerPanel::Show(bool show) 
{
    if ( wxGetApp().plater()->model().design_info != nullptr) {
        wxString text = wxString::FromUTF8(wxGetApp().plater()->model().design_info->Designer);
        m_input_designer->GetTextCtrl()->SetValue(text);
    }

     if (wxGetApp().plater()->model().model_info != nullptr) { 
         wxString text = wxString::FromUTF8(wxGetApp().plater()->model().model_info->model_name);
         m_imput_model_name->GetTextCtrl()->SetValue(text);
     }
    
    return wxPanel::Show(show);
}

void DesignerPanel::on_input_enter_designer(wxCommandEvent &evt) 
{ 
    auto text  = evt.GetString();
    wxGetApp().plater()->model().SetDesigner(std::string(text.ToUTF8().data()), "");
}

void DesignerPanel::on_input_enter_model(wxCommandEvent &evt) 
{
    auto text   = evt.GetString();
    if (wxGetApp().plater()->model().model_info) {
        wxGetApp().plater()->model().model_info->model_name = std::string(text.ToUTF8().data());
    }
}


void DesignerPanel::update_info() 
{
    if (wxGetApp().plater()->model().design_info != nullptr) {
        wxString text = wxString::FromUTF8(wxGetApp().plater()->model().design_info->Designer);
        m_input_designer->GetTextCtrl()->SetValue(text);
    } else {
        m_input_designer->GetTextCtrl()->SetValue(wxEmptyString);
    }

    if (wxGetApp().plater()->model().model_info != nullptr) {
        wxString text = wxString::FromUTF8(wxGetApp().plater()->model().model_info->model_name);
        m_imput_model_name->GetTextCtrl()->SetValue(text);
    } else {
         m_imput_model_name->GetTextCtrl()->SetValue(wxEmptyString);
    }
}

void DesignerPanel::msw_rescale()
{
    m_input_designer->GetTextCtrl()->SetSize(wxSize(FromDIP(450), FromDIP(22)));
    m_imput_model_name->GetTextCtrl()->SetSize(wxSize(FromDIP(450), FromDIP(22)));
}

}} // namespace Slic3r::GUI
