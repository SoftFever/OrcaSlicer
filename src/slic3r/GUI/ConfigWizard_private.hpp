#ifndef slic3r_ConfigWizard_private_hpp_
#define slic3r_ConfigWizard_private_hpp_

#include "ConfigWizard.hpp"

#include <vector>
#include <set>
#include <unordered_map>
#include <functional>
#include <boost/filesystem.hpp>

#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/listbox.h>
#include <wx/checklst.h>
#include <wx/radiobut.h>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/PresetUpdater.hpp"
#include "AppConfig.hpp"
#include "PresetBundle.hpp"
#include "BedShapeDialog.hpp"
#include "GUI.hpp"
#include "wxExtensions.hpp"


namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

enum {
    WRAP_WIDTH = 500,
    MODEL_MIN_WRAP = 150,

    DIALOG_MARGIN = 15,
    INDEX_MARGIN = 40,
    BTN_SPACING = 10,
    INDENT_SPACING = 30,
    VERTICAL_SPACING = 10,

    MAX_COLS = 4,
    ROW_SPACING = 75,
};



// Configuration data structures extensions needed for the wizard

enum Technology {
    // Bitflag equivalent of PrinterTechnology
    T_FFF = 0x1,
    T_SLA = 0x2,
    T_ANY = ~0,
};

struct Materials
{
    Technology technology;
    // use vector for the presets to purpose of save of presets sorting in the bundle
    std::vector<const Preset*> presets;
    std::set<std::string> types;

    Materials(Technology technology) : technology(technology) {}

    void push(const Preset *preset);
    void clear();
    bool containts(const Preset *preset) const {
        return std::find(presets.begin(), presets.end(), preset) != presets.end(); 
    }

    const std::string& appconfig_section() const;
    const std::string& get_type(const Preset *preset) const;
    const std::string& get_vendor(const Preset *preset) const;

    template<class F> void filter_presets(const std::string &type, const std::string &vendor, F cb) {
        for (const Preset *preset : presets) {
            if ((type.empty() || get_type(preset) == type) && (vendor.empty() || get_vendor(preset) == vendor)) {
                cb(preset);
            }
        }
    }

    static const std::string UNKNOWN;
    static const std::string& get_filament_type(const Preset *preset);
    static const std::string& get_filament_vendor(const Preset *preset);
    static const std::string& get_material_type(const Preset *preset);
    static const std::string& get_material_vendor(const Preset *preset);
};

struct Bundle
{
    std::unique_ptr<PresetBundle> preset_bundle;
    VendorProfile *vendor_profile { nullptr };
    bool is_in_resources { false };
    bool is_prusa_bundle { false };

    Bundle() = default;
    Bundle(Bundle &&other);

    // Returns false if not loaded. Reason for that is logged as boost::log error.
    bool load(fs::path source_path, bool is_in_resources, bool is_prusa_bundle = false);

    const std::string& vendor_id() const { return vendor_profile->id; }
};

struct BundleMap: std::unordered_map<std::string /* = vendor ID */, Bundle>
{
    static BundleMap load();

    Bundle& prusa_bundle();
    const Bundle& prusa_bundle() const;
};

struct PrinterPickerEvent;


// GUI elements

typedef std::function<bool(const VendorProfile::PrinterModel&)> ModelFilter;

struct PrinterPicker: wxPanel
{
    struct Checkbox : wxCheckBox
    {
        Checkbox(wxWindow *parent, const wxString &label, const std::string &model, const std::string &variant) :
            wxCheckBox(parent, wxID_ANY, label),
            model(model),
            variant(variant)
        {}

        std::string model;
        std::string variant;
    };

    const std::string vendor_id;
    std::vector<Checkbox*> cboxes;
    std::vector<Checkbox*> cboxes_alt;

    PrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig, const ModelFilter &filter);
    PrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig);

    void select_all(bool select, bool alternates = false);
    void select_one(size_t i, bool select);
    bool any_selected() const;
    std::set<std::string> get_selected_models() const ;

    int get_width() const { return width; }
    const std::vector<int>& get_button_indexes() { return m_button_indexes; }

    static const std::string PRINTER_PLACEHOLDER;
private:
    int width;
    std::vector<int> m_button_indexes;

    void on_checkbox(const Checkbox *cbox, bool checked);
};

struct ConfigWizardPage: wxPanel
{
    ConfigWizard *parent;
    const wxString shortname;
    wxBoxSizer *content;
    const unsigned indent;

    ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname, unsigned indent = 0);
    virtual ~ConfigWizardPage();

    template<class T>
    T* append(T *thing, int proportion = 0, int flag = wxEXPAND|wxTOP|wxBOTTOM, int border = 10)
    {
        content->Add(thing, proportion, flag, border);
        return thing;
    }

    wxStaticText* append_text(wxString text);
    void append_spacer(int space);

    ConfigWizard::priv *wizard_p() const { return parent->p.get(); }

    virtual void apply_custom_config(DynamicPrintConfig &config) {}
    virtual void set_run_reason(ConfigWizard::RunReason run_reason) {}
    virtual void on_activate() {}
};

struct PageWelcome: ConfigWizardPage
{
    wxStaticText *welcome_text;
    wxCheckBox *cbox_reset;

    PageWelcome(ConfigWizard *parent);

    bool reset_user_profile() const { return cbox_reset != nullptr ? cbox_reset->GetValue() : false; }

    virtual void set_run_reason(ConfigWizard::RunReason run_reason) override;
};

struct PagePrinters: ConfigWizardPage
{
    std::vector<PrinterPicker *> printer_pickers;
    Technology technology;
    bool install;

    PagePrinters(ConfigWizard *parent,
        wxString title,
        wxString shortname,
        const VendorProfile &vendor,
        unsigned indent, Technology technology);

    void select_all(bool select, bool alternates = false);
    int get_width() const;
    bool any_selected() const;
    std::set<std::string> get_selected_models();

    std::string get_vendor_id() const { return printer_pickers.empty() ? "" : printer_pickers[0]->vendor_id; }

    virtual void set_run_reason(ConfigWizard::RunReason run_reason) override;
};

// Here we extend wxListBox and wxCheckListBox
// to make the client data API much easier to use.
template<class T, class D> struct DataList : public T
{
    DataList(wxWindow *parent) : T(parent, wxID_ANY) {}

    // Note: We're _not_ using wxLB_SORT here because it doesn't do the right thing,
    // eg. "ABS" is sorted before "(All)"

    int append(const std::string &label, const D *data) {
        void *ptr = reinterpret_cast<void*>(const_cast<D*>(data));
        return this->Append(from_u8(label), ptr);
    }

    int append(const wxString &label, const D *data) {
        void *ptr = reinterpret_cast<void*>(const_cast<D*>(data));
        return this->Append(label, ptr);
    }

    const D& get_data(int n) {
        return *reinterpret_cast<const D*>(this->GetClientData(n));
    }

    int find(const D &data) {
        for (unsigned i = 0; i < this->GetCount(); i++) {
            if (get_data(i) == data) { return i; }
        }

        return wxNOT_FOUND;
    }

    int size() { return this->GetCount(); }
};

typedef DataList<wxListBox, std::string> StringList;
typedef DataList<wxCheckListBox, std::string> PresetList;

struct PageMaterials: ConfigWizardPage
{
    Materials *materials;
    StringList *list_l1, *list_l2;
    PresetList *list_l3;
    int sel1_prev, sel2_prev;
    bool presets_loaded;

    static const std::string EMPTY;

    PageMaterials(ConfigWizard *parent, Materials *materials, wxString title, wxString shortname, wxString list1name);

    void reload_presets();
    void update_lists(int sel1, int sel2);
    void select_material(int i);
    void select_all(bool select);
    void clear();

    virtual void on_activate() override;
};

struct PageCustom: ConfigWizardPage
{
    PageCustom(ConfigWizard *parent);

    bool custom_wanted() const { return cb_custom->GetValue(); }
    std::string profile_name() const { return into_u8(tc_profile_name->GetValue()); }

private:
    static const char* default_profile_name;

    wxCheckBox *cb_custom;
    wxTextCtrl *tc_profile_name;
    wxString profile_name_prev;

};

struct PageUpdate: ConfigWizardPage
{
    bool version_check;
    bool preset_update;

    PageUpdate(ConfigWizard *parent);
};

struct PageReloadFromDisk : ConfigWizardPage
{
    bool full_pathnames;

    PageReloadFromDisk(ConfigWizard* parent);
};

struct PageMode: ConfigWizardPage
{
    wxRadioButton *radio_simple;
    wxRadioButton *radio_advanced;
    wxRadioButton *radio_expert;

    PageMode(ConfigWizard *parent);

    void serialize_mode(AppConfig *app_config) const;

    virtual void on_activate();
};

struct PageVendors: ConfigWizardPage
{
    PageVendors(ConfigWizard *parent);
};

struct PageFirmware: ConfigWizardPage
{
    const ConfigOptionDef &gcode_opt;
    wxChoice *gcode_picker;

    PageFirmware(ConfigWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct PageBedShape: ConfigWizardPage
{
    BedShapePanel *shape_panel;

    PageBedShape(ConfigWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct PageDiameters: ConfigWizardPage
{
    wxSpinCtrlDouble *spin_nozzle;
    wxSpinCtrlDouble *spin_filam;

    PageDiameters(ConfigWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct PageTemperatures: ConfigWizardPage
{
    wxSpinCtrlDouble *spin_extr;
    wxSpinCtrlDouble *spin_bed;

    PageTemperatures(ConfigWizard *parent);
    virtual void apply_custom_config(DynamicPrintConfig &config);
};

// hypothetically, each vendor can has printers both of technologies (FFF and SLA)
typedef std::map<std::string /* = vendor ID */, 
                 std::pair<PagePrinters* /* = FFF page */, 
                           PagePrinters* /* = SLA page */>> Pages3rdparty;


class ConfigWizardIndex: public wxPanel
{
public:
    ConfigWizardIndex(wxWindow *parent);

    void add_page(ConfigWizardPage *page);
    void add_label(wxString label, unsigned indent = 0);

    size_t active_item() const { return item_active; }
    ConfigWizardPage* active_page() const;
    bool active_is_last() const { return item_active < items.size() && item_active == last_page; }

    void go_prev();
    void go_next();
    void go_to(size_t i);
    void go_to(const ConfigWizardPage *page);

    void clear();
    void msw_rescale();

    int em() const { return em_w; }

    static const size_t NO_ITEM = size_t(-1);
private:
    struct Item
    {
        wxString label;
        unsigned indent;
        ConfigWizardPage *page;     // nullptr page => label-only item

        bool operator==(ConfigWizardPage *page) const { return this->page == page; }
    };

    int em_w;
    int em_h;
    ScalableBitmap bg;
    ScalableBitmap bullet_black;
    ScalableBitmap bullet_blue;
    ScalableBitmap bullet_white;
    wxStaticBitmap* logo;

    std::vector<Item> items;
    size_t item_active;
    ssize_t item_hover;
    size_t last_page;

    int item_height() const { return std::max(bullet_black.bmp().GetSize().GetHeight(), em_w) + em_w; }

    void on_paint(wxPaintEvent &evt);
    void on_mouse_move(wxMouseEvent &evt);
};

wxDEFINE_EVENT(EVT_INDEX_PAGE, wxCommandEvent);



// ConfigWizard private data

typedef std::map<std::string, std::set<std::string>> PresetAliases;

struct ConfigWizard::priv
{
    ConfigWizard *q;
    ConfigWizard::RunReason run_reason = RR_USER;
    AppConfig appconfig_new;      // Backing for vendor/model/variant and material selections in the GUI
    BundleMap bundles;            // Holds all loaded config bundles, the key is the vendor names.
                                  // Materials refers to Presets in those bundles by pointers.
                                  // Also we update the is_visible flag in printer Presets according to the
                                  // PrinterPickers state.
    Materials filaments;          // Holds available filament presets and their types & vendors
    Materials sla_materials;      // Ditto for SLA materials
    PresetAliases aliases_fff;    // Map of aliase to preset names
    PresetAliases aliases_sla;    // Map of aliase to preset names
    std::unique_ptr<DynamicPrintConfig> custom_config;           // Backing for custom printer definition
    bool any_fff_selected;        // Used to decide whether to display Filaments page
    bool any_sla_selected;        // Used to decide whether to display SLA Materials page
	bool custom_printer_selected; 

    wxScrolledWindow *hscroll = nullptr;
    wxBoxSizer *hscroll_sizer = nullptr;
    wxBoxSizer *btnsizer = nullptr;
    ConfigWizardPage *page_current = nullptr;
    ConfigWizardIndex *index = nullptr;
    wxButton *btn_sel_all = nullptr;
    wxButton *btn_prev = nullptr;
    wxButton *btn_next = nullptr;
    wxButton *btn_finish = nullptr;
    wxButton *btn_cancel = nullptr;

    PageWelcome      *page_welcome = nullptr;
    PagePrinters     *page_fff = nullptr;
    PagePrinters     *page_msla = nullptr;
    PageMaterials    *page_filaments = nullptr;
    PageMaterials    *page_sla_materials = nullptr;
    PageCustom       *page_custom = nullptr;
    PageUpdate       *page_update = nullptr;
    PageReloadFromDisk *page_reload_from_disk = nullptr;
    PageMode         *page_mode = nullptr;
    PageVendors      *page_vendors = nullptr;
    Pages3rdparty     pages_3rdparty;

    // Custom setup pages
    PageFirmware     *page_firmware = nullptr;
    PageBedShape     *page_bed = nullptr;
    PageDiameters    *page_diams = nullptr;
    PageTemperatures *page_temps = nullptr;

    // Pointers to all pages (regardless or whether currently part of the ConfigWizardIndex)
    std::vector<ConfigWizardPage*> all_pages;

    priv(ConfigWizard *q)
        : q(q)
        , filaments(T_FFF)
        , sla_materials(T_SLA)
    {}

    void load_pages();
    void init_dialog_size();

    void load_vendors();
    void add_page(ConfigWizardPage *page);
    void enable_next(bool enable);
    void set_start_page(ConfigWizard::StartPage start_page);
    void create_3rdparty_pages();
    void set_run_reason(RunReason run_reason);
    void update_materials(Technology technology);

    void on_custom_setup(const bool custom_wanted);
    void on_printer_pick(PagePrinters *page, const PrinterPickerEvent &evt);
    void select_default_materials_for_printer_model(const VendorProfile::PrinterModel &printer_model, Technology technology);
    void select_default_materials_for_printer_models(Technology technology, const std::set<const VendorProfile::PrinterModel*> &printer_models);
    void on_3rdparty_install(const VendorProfile *vendor, bool install);

    bool on_bnt_finish();
    bool check_and_install_missing_materials(Technology technology, const std::string &only_for_model_id = std::string());
    void apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater);
    // #ys_FIXME_alise
    void update_presets_in_config(const std::string& section, const std::string& alias_key, bool add);

    bool check_fff_selected();        // Used to decide whether to display Filaments page
    bool check_sla_selected();        // Used to decide whether to display SLA Materials page

    int em() const { return index->em(); }
};

}
}

#endif
