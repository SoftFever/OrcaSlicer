#include "GalleryDialog.hpp"

#include <cstddef>
#include <vector>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/wupdlock.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "wxExtensions.hpp"
#include "I18N.hpp"
#include "Notebook.hpp"
#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Format/OBJ.hpp"
#include "../Utils/MacDarkMode.hpp"

namespace Slic3r {
namespace GUI {

#define BORDER_W    10
#define IMG_PX_CNT  64

namespace fs = boost::filesystem;

// Gallery::DropTarget
class GalleryDropTarget : public wxFileDropTarget
{
public:
    GalleryDropTarget(GalleryDialog* gallery_dlg) : gallery_dlg(gallery_dlg) { this->SetDefaultAction(wxDragCopy); }

    bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override;

private:
    GalleryDialog* gallery_dlg {nullptr};
};

bool GalleryDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames)
{
#ifdef WIN32
    // hides the system icon
    this->MSWUpdateDragImageOnLeave();
#endif // WIN32
    return gallery_dlg ? gallery_dlg->load_files(filenames) : false;
}


GalleryDialog::GalleryDialog(wxWindow* parent, bool modify_gallery/* = false*/) :
    DPIDialog(parent, wxID_ANY, _L("Shape Gallery"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
#ifndef _WIN32
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
    SetFont(wxGetApp().normal_font());

    wxStaticText* label_top = new wxStaticText(this, wxID_ANY, _L("Select shape from the gallery") + ":");

    m_list_ctrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(50 * wxGetApp().em_unit(), 35 * wxGetApp().em_unit()),
                                wxLC_ICON | wxSIMPLE_BORDER);
    m_list_ctrl->Bind(wxEVT_LIST_ITEM_SELECTED,     &GalleryDialog::select, this);
    m_list_ctrl->Bind(wxEVT_LIST_ITEM_DESELECTED,   &GalleryDialog::deselect, this);
    m_list_ctrl->Bind(wxEVT_LIST_KEY_DOWN,          &GalleryDialog::key_down, this);
    m_list_ctrl->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK,  &GalleryDialog::show_context_menu, this);
    m_list_ctrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        m_selected_items.clear();
        select(event);
        this->EndModal(wxID_OK);
    });
#ifdef _WIN32
    this->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        m_list_ctrl->Arrange();
    });
#endif

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK | wxCLOSE);
    wxButton* ok_btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
    ok_btn->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(!m_selected_items.empty()); });
    if (modify_gallery) {
        ok_btn->SetLabel(_L("Add to bed"));
        ok_btn->SetToolTip(_L("Add selected shape(s) to the bed"));
    }
    static_cast<wxButton*>(FindWindowById(wxID_CLOSE, this))->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ this->EndModal(wxID_CLOSE); });
    this->SetEscapeId(wxID_CLOSE);
    auto add_btn = [this, buttons]( size_t pos, int& ID, wxString title, wxString tooltip,
                                    void (GalleryDialog::* method)(wxEvent&), 
                                    std::function<bool()> enable_fn = []() {return true; }) {
        ID = NewControlId();
        wxButton* btn = new wxButton(this, ID, title);
        btn->SetToolTip(tooltip);
        btn->Bind(wxEVT_UPDATE_UI, [enable_fn](wxUpdateUIEvent& evt) { evt.Enable(enable_fn()); });
        buttons->Insert(pos, btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);
        this->Bind(wxEVT_BUTTON, method, this, ID);
    };

    size_t btn_pos = 0;
    add_btn(btn_pos++, ID_BTN_ADD_CUSTOM_SHAPE,   _L("Add"),                _L("Add one or more custom shapes"),                                                &GalleryDialog::add_custom_shapes);
    add_btn(btn_pos++, ID_BTN_DEL_CUSTOM_SHAPE,   _L("Delete"),             _L("Delete one or more custom shape. You can't delete system shapes"),              &GalleryDialog::del_custom_shapes,  [this](){ return can_delete();           });
    //add_btn(btn_pos++, ID_BTN_REPLACE_CUSTOM_PNG, _L("Change thumbnail"),   _L("Replace PNG for custom shape. You can't raplace thimbnail for system shape"),   &GalleryDialog::change_thumbnail, [this](){ return can_change_thumbnail(); });
    buttons->InsertStretchSpacer(btn_pos, 2* BORDER_W);

    load_label_icon_list();

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(m_list_ctrl, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(buttons   , 0, wxEXPAND | wxALL, BORDER_W);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    wxGetApp().UpdateDlgDarkUI(this);
    this->CenterOnScreen();

    this->SetDropTarget(new GalleryDropTarget(this));
}

GalleryDialog::~GalleryDialog()
{   
}

bool GalleryDialog::can_delete() 
{
    if (m_selected_items.empty())
        return false;
    for (const Item& item : m_selected_items)
        if (item.is_system)
            return false;
    return true;
}

bool GalleryDialog::can_change_thumbnail() 
{
    return (m_selected_items.size() == 1 && !m_selected_items[0].is_system);
}

void GalleryDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { ID_BTN_ADD_CUSTOM_SHAPE, ID_BTN_DEL_CUSTOM_SHAPE, ID_BTN_REPLACE_CUSTOM_PNG, wxID_OK, wxID_CLOSE });

    wxSize size = wxSize(50 * em, 35 * em);
    m_list_ctrl->SetMinSize(size);
    m_list_ctrl->SetSize(size);

    Fit();
    Refresh();
}

static void add_lock(wxImage& image) 
{
    int lock_sz = 22;
#ifdef __APPLE__
    lock_sz /= mac_max_scaling_factor();
#endif
    wxBitmap bmp = create_scaled_bitmap("lock", nullptr, lock_sz);

    wxImage lock_image = bmp.ConvertToImage();
    if (!lock_image.IsOk() || lock_image.GetWidth() == 0 || lock_image.GetHeight() == 0)
        return;

    auto lock_px_data = (uint8_t*)lock_image.GetData();
    auto lock_a_data = (uint8_t*)lock_image.GetAlpha();
    int lock_width  = lock_image.GetWidth();
    int lock_height = lock_image.GetHeight();
    
    auto px_data = (uint8_t*)image.GetData();
    auto a_data = (uint8_t*)image.GetAlpha();

    int width = image.GetWidth();
    int height = image.GetHeight();

    size_t beg_x = width - lock_width;
    size_t beg_y = height - lock_height;
    for (size_t x = 0; x < (size_t)lock_width; ++x) {
        for (size_t y = 0; y < (size_t)lock_height; ++y) {
            const size_t lock_idx = (x + y * lock_width);
            if (lock_a_data && lock_a_data[lock_idx] == 0)
                continue;

            const size_t idx = (beg_x + x + (beg_y + y) * width);
            if (a_data)
                a_data[idx] = lock_a_data[lock_idx];

            const size_t idx_rgb = (beg_x + x + (beg_y + y) * width) * 3;
            const size_t lock_idx_rgb = (x + y * lock_width) * 3;
            px_data[idx_rgb] = lock_px_data[lock_idx_rgb];
            px_data[idx_rgb + 1] = lock_px_data[lock_idx_rgb + 1];
            px_data[idx_rgb + 2] = lock_px_data[lock_idx_rgb + 2];
        }
    }
}

static void add_default_image(wxImageList* img_list, bool is_system)
{
    int sz = IMG_PX_CNT;
#ifdef __APPLE__
    sz /= mac_max_scaling_factor();
#endif
    wxBitmap bmp = create_scaled_bitmap("cog", nullptr, sz, true);

    if (is_system) {
        wxImage image = bmp.ConvertToImage();
        if (image.IsOk() && image.GetWidth() != 0 && image.GetHeight() != 0) {
            add_lock(image);
            bmp = wxBitmap(std::move(image));
        }
    }
    img_list->Add(bmp);
};

static fs::path get_dir(bool sys_dir)
{
    return fs::absolute(fs::path(sys_dir ? sys_shapes_dir() : custom_shapes_dir())).make_preferred();
}

static std::string get_dir_path(bool sys_dir) 
{
    fs::path dir = get_dir(sys_dir);
#ifdef __WXMSW__
    return dir.string() + "\\";
#else
    return dir.string() + "/";
#endif
}

static void generate_thumbnail_from_model(const std::string& filename)
{
    if (!boost::algorithm::iends_with(filename, ".stl") &&
        !boost::algorithm::iends_with(filename, ".obj")) {
        BOOST_LOG_TRIVIAL(error) << "Found invalid file type in generate_thumbnail_from_model() [" << filename << "]";
        return;
    }

    Model model;
    try {
        model = Model::read_from_file(filename);
    }
    catch (std::exception&) {
        BOOST_LOG_TRIVIAL(error) << "Error loading model from " << filename << " in generate_thumbnail_from_model()";
        return;
    }

    assert(model.objects.size() == 1);
    assert(model.objects[0]->volumes.size() == 1);
    assert(model.objects[0]->instances.size() == 1);

    model.objects[0]->center_around_origin(false);
    model.objects[0]->ensure_on_bed(false);

    model.center_instances_around_point(to_2d(wxGetApp().plater()->build_volume().bounding_volume().center()));

    GLVolumeCollection volumes;
    volumes.volumes.push_back(new GLVolume());
    GLVolume* volume = volumes.volumes[0];
    volume->indexed_vertex_array.load_mesh(model.mesh());
    volume->indexed_vertex_array.finalize_geometry(true);
    volume->set_instance_transformation(model.objects[0]->instances[0]->get_transformation());
    volume->set_volume_transformation(model.objects[0]->volumes[0]->get_transformation());

    ThumbnailData thumbnail_data;
    const ThumbnailsParams thumbnail_params = { {}, false, false, false, true };
    wxGetApp().plater()->canvas3D()->render_thumbnail(thumbnail_data, 256, 256, thumbnail_params, volumes, Camera::EType::Perspective);

    if (thumbnail_data.width == 0 || thumbnail_data.height == 0)
        return;

    wxImage image(thumbnail_data.width, thumbnail_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < thumbnail_data.height; ++r) {
        unsigned int rr = (thumbnail_data.height - 1 - r) * thumbnail_data.width;
        for (unsigned int c = 0; c < thumbnail_data.width; ++c) {
            unsigned char* px = (unsigned char*)thumbnail_data.pixels.data() + 4 * (rr + c);
            image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
            image.SetAlpha((int)c, (int)r, px[3]);
        }
    }

    fs::path out_path = fs::path(filename);
    out_path.replace_extension("png");
    image.SaveFile(out_path.string(), wxBITMAP_TYPE_PNG);
}

void GalleryDialog::load_label_icon_list()
{
    // load names from files
    auto add_files_from_gallery = [](std::vector<Item>& items, bool is_sys_dir, std::string& dir_path)
    {
        fs::path dir = get_dir(is_sys_dir);
        if (!fs::exists(dir))
            return;

        dir_path = get_dir_path(is_sys_dir);

        std::vector<std::string> sorted_names;
        for (auto& dir_entry : fs::directory_iterator(dir)) {
            TriangleMesh mesh;
            if ((is_gallery_file(dir_entry, ".stl") && mesh.ReadSTLFile(dir_entry.path().string().c_str())) || 
                (is_gallery_file(dir_entry, ".obj") && load_obj(dir_entry.path().string().c_str(), &mesh) )    )
                sorted_names.push_back(dir_entry.path().filename().string());
        }

        // sort the filename case insensitive
        std::sort(sorted_names.begin(), sorted_names.end(), [](const std::string& a, const std::string& b)
            { return boost::algorithm::to_lower_copy(a) < boost::algorithm::to_lower_copy(b); });

        for (const std::string& name : sorted_names)
            items.push_back(Item{ name, is_sys_dir });
    };

    wxBusyCursor busy;

    std::string m_sys_dir_path, m_cust_dir_path;
    std::vector<Item> list_items;
    add_files_from_gallery(list_items, true, m_sys_dir_path);
    add_files_from_gallery(list_items, false, m_cust_dir_path);

    // Make an image list containing large icons

    int px_cnt = (int)(em_unit() * IMG_PX_CNT * 0.1f + 0.5f);
    m_image_list = new wxImageList(px_cnt, px_cnt);

    std::string ext = ".png";

    for (const auto& item : list_items) {
        fs::path model_path = fs::path((item.is_system ? m_sys_dir_path : m_cust_dir_path) + item.name);
        std::string model_name = model_path.string();
        model_path.replace_extension("png");
        std::string img_name = model_path.string();

#if 0 // use "1" just in DEBUG mode to the generation of the thumbnails for the sistem shapes
        bool can_generate_thumbnail = true;
#else
        bool can_generate_thumbnail = !item.is_system;
#endif //DEBUG
        if (!fs::exists(img_name)) {
            if (can_generate_thumbnail)
                generate_thumbnail_from_model(model_name);
            else {
                add_default_image(m_image_list, item.is_system);
                continue;
            }
        }

        wxImage image;
        if (!image.CanRead(from_u8(img_name)) ||
            !image.LoadFile(from_u8(img_name), wxBITMAP_TYPE_PNG) ||
            image.GetWidth() == 0 || image.GetHeight() == 0) {
            add_default_image(m_image_list, item.is_system);
            continue;
        }
        image.Rescale(px_cnt, px_cnt, wxIMAGE_QUALITY_BILINEAR);

        if (item.is_system)
            add_lock(image);
        wxBitmap bmp = wxBitmap(std::move(image));
        m_image_list->Add(bmp);
    }

    m_list_ctrl->SetImageList(m_image_list, wxIMAGE_LIST_NORMAL);

    int img_cnt = m_image_list->GetImageCount();
    for (int i = 0; i < img_cnt; i++) {
        m_list_ctrl->InsertItem(i, from_u8(list_items[i].name), i);
        if (list_items[i].is_system)
            m_list_ctrl->SetItemFont(i, wxGetApp().bold_font());
    }
}

void GalleryDialog::get_input_files(wxArrayString& input_files)
{
    for (const Item& item : m_selected_items)
        input_files.Add(from_u8(get_dir_path(item.is_system) + item.name));
}

void GalleryDialog::add_custom_shapes(wxEvent& event)
{
    wxArrayString input_files;
    wxFileDialog dialog(this, _L("Choose one or more files (STL, OBJ):"),
        from_u8(wxGetApp().app_config->get_last_dir()), "",
        file_wildcards(FT_GALLERY), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);

    if (input_files.IsEmpty())
        return;

    load_files(input_files);
}

void GalleryDialog::del_custom_shapes()
{
    auto custom_dir = get_dir(false);

    auto remove_file = [custom_dir](const std::string& name) {
        if (!fs::exists(custom_dir / name))
            return;
        try {
            fs::remove(custom_dir / name);
        }
        catch (fs::filesystem_error const& e) {
            std::cerr << e.what() << '\n';
        }
    };

    for (const Item& item : m_selected_items) {
        remove_file(item.name);
        fs::path path = fs::path(item.name);
        path.replace_extension("png");
        remove_file(path.string());
    }

    update();
}

static void show_warning(const wxString& title, const std::string& error_file_type)
{
    const wxString msg_text = format_wxstr(_L("It looks like selected %1%-file has an error or is destructed.\n"
        "We can't load this file"), error_file_type);
    MessageDialog dialog(nullptr, msg_text, title, wxICON_WARNING | wxOK);
    dialog.ShowModal();
}

void GalleryDialog::change_thumbnail()
{
    if (m_selected_items.size() != 1 || m_selected_items[0].is_system)
        return;

    wxFileDialog dialog(this, _L("Choose one PNG file:"),
                        from_u8(wxGetApp().app_config->get_last_dir()), "",
                        "PNG files (*.png)|*.png;*.PNG", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    wxArrayString input_files;
    dialog.GetPaths(input_files);

    if (input_files.IsEmpty())
        return;

    if (wxImage image; !image.CanRead(input_files.Item(0))) {
        show_warning(_L("Replacing of the PNG"), "PNG");
        return;
    }

    try {
        fs::path png_path = fs::path(get_dir(false) / m_selected_items[0].name);
        png_path.replace_extension("png");

        fs::path current = fs::path(into_u8(input_files.Item(0)));
        fs::copy_file(current, png_path, fs::copy_option::overwrite_if_exists);
    }
    catch (fs::filesystem_error const& e) {
        std::cerr << e.what() << '\n';
        return;
    }

    update();
}

void GalleryDialog::select(wxListEvent& event)
{
    int idx = event.GetIndex();
    Item item { into_u8(m_list_ctrl->GetItemText(idx)), m_list_ctrl->GetItemFont(idx).GetWeight() == wxFONTWEIGHT_BOLD };

    m_selected_items.push_back(item);
}

void GalleryDialog::deselect(wxListEvent& event)
{
    if (m_list_ctrl->GetSelectedItemCount() == 0) {
        m_selected_items.clear();
        return;
    }

    std::string name = into_u8(m_list_ctrl->GetItemText(event.GetIndex()));
    m_selected_items.erase(std::remove_if(m_selected_items.begin(), m_selected_items.end(), [name](Item item) { return item.name == name; }));
}

void GalleryDialog::show_context_menu(wxListEvent& event)
{
    wxMenu* menu = new wxMenu();
    if (can_delete())
        append_menu_item(menu, wxID_ANY, _L("Delete"), "", [this](wxCommandEvent&) { del_custom_shapes(); });
    if (can_change_thumbnail())
        append_menu_item(menu, wxID_ANY, _L("Change thumbnail"), "", [this](wxCommandEvent&) { change_thumbnail(); });

    this->PopupMenu(menu);
}

void GalleryDialog::key_down(wxListEvent& event)
{
    if (can_delete() && (event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_BACK))
        del_custom_shapes();
}

void GalleryDialog::update()
{
    m_selected_items.clear();
    m_image_list->RemoveAll();
    m_list_ctrl->ClearAll();
    load_label_icon_list();
}

bool GalleryDialog::load_files(const wxArrayString& input_files)
{
    auto dest_dir = get_dir(false);

    try {
        if (!fs::exists(dest_dir))
            if (!fs::create_directory(dest_dir)) {
                std::cerr << "Unable to create destination directory" << dest_dir.string() << '\n' ;
                return false;
            }
    }
    catch (fs::filesystem_error const& e) {
        std::cerr << e.what() << '\n';
        return false;
    }

    // Iterate through the input files
    for (size_t i = 0; i < input_files.size(); ++i) {
        std::string input_file = into_u8(input_files.Item(i));

        TriangleMesh mesh; 
        if (is_gallery_file(input_file, ".stl") && !mesh.ReadSTLFile(input_file.c_str())) {
            show_warning(format_wxstr(_L("Loading of the \"%1%\""), input_file), "STL");
            continue;
        }

        if (is_gallery_file(input_file, ".obj") && !load_obj(input_file.c_str(), &mesh)) {
            show_warning(format_wxstr(_L("Loading of the \"%1%\""), input_file), "OBJ");
            continue;
        }

        try {
            fs::path current = fs::path(input_file);
            if (!fs::exists(dest_dir / current.filename()))
                fs::copy_file(current, dest_dir / current.filename());
            else {
                std::string filename = current.stem().string();

                int file_idx = 0;
                for (auto& dir_entry : fs::directory_iterator(dest_dir))
                    if (is_gallery_file(dir_entry, ".stl") || is_gallery_file(dir_entry, ".obj")) {
                        std::string name = dir_entry.path().stem().string();
                        if (filename == name) {
                            if (file_idx == 0)
                                file_idx++;
                            continue;
                        }
                        
                        if (name.find(filename) != 0 ||
                            name[filename.size()] != ' ' || name[filename.size()+1] != '(' || name[name.size()-1] != ')')
                            continue;
                        std::string idx_str = name.substr(filename.size() + 2, name.size() - filename.size() - 3);                        
                        if (int cur_idx = atoi(idx_str.c_str()); file_idx <= cur_idx)
                            file_idx = cur_idx+1;
                    }
                if (file_idx > 0) {
                    filename += " (" + std::to_string(file_idx) + ")." + (is_gallery_file(input_file, ".stl") ? "stl" : "obj");
                    fs::copy_file(current, dest_dir / filename);
                }
            }
        }
        catch (fs::filesystem_error const& e) {
            std::cerr << e.what() << '\n';
            return false;
        }
    }

    update();
    return true;
}

}}    // namespace Slic3r::GUI
