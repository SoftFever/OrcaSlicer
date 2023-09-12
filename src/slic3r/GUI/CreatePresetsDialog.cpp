#include "CreatePresetsDialog.hpp"
#include <regex>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "libslic3r/PresetBundle.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include <openssl/md5.h>
#include <openssl/evp.h>

#define NAME_OPTION_COMBOBOX_SIZE wxSize(FromDIP(200), FromDIP(24))
#define FILAMENT_PRESET_COMBOBOX_SIZE wxSize(FromDIP(300), FromDIP(24))
#define OPTION_SIZE wxSize(FromDIP(100), FromDIP(24))
#define PRINTER_LIST_SIZE wxSize(-1, FromDIP(100))
#define FILAMENT_LIST_SIZE wxSize(FromDIP(560), FromDIP(100))
#define FILAMENT_OPTION_SIZE wxSize(FromDIP(-1), FromDIP(30))
#define PRESET_TEMPLATE_SIZE wxSize(FromDIP(-1), FromDIP(100))
#define PRINTER_SPACE_SIZE wxSize(FromDIP(80), FromDIP(24))
#define ORIGIN_TEXT_SIZE wxSize(FromDIP(10), FromDIP(24))
#define PRINTER_PRESET_VENDOR_SIZE wxSize(FromDIP(150), FromDIP(24))
#define PRINTER_PRESET_MODEL_SIZE wxSize(FromDIP(280), FromDIP(24))
#define STATIC_TEXT_COLOUR wxColour("#363636")
#define PRINTER_LIST_COLOUR wxColour("#EEEEEE")
#define FILAMENT_OPTION_COLOUR wxColour("#D9D9D9")
#define SELECT_ALL_OPTION_COLOUR wxColour("#00AE42")
#define DEFAULT_PROMPT_TEXT_COLOUR wxColour("#ACACAC")

namespace Slic3r { 
namespace GUI {

static const std::vector<std::string> filament_vendors =
    {"Made for Prusa", "Prusa",       "Prusa Polymers", "123-3D",        "3DJAKE",       "AmazonBasics", "AMOLEN",        "Atoraie Filarmetl",
     "AzureFilm",      "BIBO",        "ColorFabb",      "Creality",      "Das Filament", "Devil Design", "E3D",           "Eolas Prints",
     "Esun",           "Extrudr",     "Fiberlogy",      "Filament PM",   "Filatech",     "Fillamentum",  "FormFutura",    "Geeetech",
     "Generic",        "Hatchbox",    "Infinity3D",     "Inland",        "KVP",          "MakerGear",    "MatterHackers", "Overture",
     "Polymaker",      "PrimaSelect", "Print4Taste",    "Printed Solid", "Proto-pasta",  "ProtoPasta",   "Push Plastic",  "Real Filament",
     "SainSmart",      "Smartfil",    "Snapmaker",      "Solutech",      "Velleman",     "Verbatim",     "VOXELPLA"};

static const std::vector<std::string> filament_types = {"PLA",    "PLA+",  "PLA Tough", "PETG",  "ABS",    "ASA",    "FLEX",        "HIPS",   "PA",     "PACF",
                                                        "NYLON",  "PVA",   "PC",        "PCABS", "PCTG",   "PCCF",   "PP",          "PEI",    "PET",    "PETG",
                                                        "PETGCF", "PTBA",  "PTBA90A",   "PEEK",  "TPU93A", "TPU75D", "TPU",         "TPU92A", "TPU98A", "Misc",
                                                        "TPE",    "GLAZE", "Nylon",     "CPE",   "METAL",  "ABST",   "Carbon Fiber"};

static const std::vector<std::string> system_filament_types = {"PLA",    "ABS",  "TPU", "PC","ASA",  "PA-CF","PET-CF",    "PETG",    "PETG-CF",        "PLA-AERO",   "PLA-CF",     "PA",
                                                        "HIPS",  "PPS",   "PVA"};

static const std::vector<std::string> printer_vendors = {"AnkerMake", "Anycubic",  "Artillery", "BIBO",        "BIQU",    "Creality ENDER", "Creality CR", "Creality SERMOON",
                                                         "Elegoo",    "FLSun",     "gCreate",   "Geeetech",    "INAT",    "Infinity3D",     "Jubilee",     "LNL3D",
                                                         "LulzBot",   "MakerGear", "Papapiu",   "Print4Taste", "RatRig",  "Rigid3D",        "Snapmaker",   "Sovol",
                                                         "TriLAB",    "Trimaker",  "Ultimaker", "Voron",       "Zonestar"};

static const std::unordered_map<std::string, std::vector<std::string>> printer_model_map =
    {{"AnkerMake",      {"M5"}},
     {"Anycubic",       {"Kossel Linear Plus", "Kossel Pulley(Linear)", "Mega Zero", "i3 Mega", "i3 Mega S", "4Max Pro 2.0", "Predator"}},
     {"Artillery",      {"sidewinder X1",   "Genius", "Hornet"}},
     {"BIBO",           {"BIBO2 Touch"}},
     {"BIQU",           {"BX"}},
     {"Creality ENDER", {"Ender-3",         "Ender-3 BLTouch",  "Ender-3 Pro",      "Ender-3 Neo",      "Ender-3 V2",
                        "Ender-3 V2 Neo",   "Ender-3 S1",       "Ender-3 S1 Pro",   "Ender-3 S1 Plus",  "Ender-3 Max", "Ender-3 Max Neo",
                        "Ender-4",          "Ender-5",          "Ender-5 Pro",      "Ender-5 Pro",      "Ender-5 S1",  "Ender-6",
                        "Ender-7",          "Ender-2",          "Ender-2 Pro"}},
     {"Creality CR",    {"CR-5 Pro",        "CR-5 Pro H",       "CR-6 SE",          "CR-6 Max",         "CR-10 SMART", "CR-10 SMART Pro",   "CR-10 Mini",
                        "CR-10 Max",        "CR-10",            "CR-10 v2",         "CR-10 v3",         "CR-10 S",     "CR-10 v2",          "CR-10 v2",
                        "CR-10 S Pro",      "CR-10 S Pro v2",   "CR-10 S4",         "CR-10 S5",         "CR-20",       "CR-20 Pro",         "CR-200B",
                        "CR-8"}},
     {"Creality SERMOON",{"Sermoon-D1",     "Sermoon-V1",       "Sermoon-V1 Pro"}},
     {"Elegoo",         {"Neptune-1",       "Neptune-2",        "Neptune-2D",       "Neptune-2s",       "Neptune-3",    "Neptune-3 Max",    "Neptune-3 Plus",
                        "Neptune-3 Pro",    "Neptune-x"}},
     {"FLSun",          {"FLSun QQs Pro",   "FLSun Q5"}},
     {"gCreate",        {"gMax 1.5XT Plus", "gMax 2",           "gMax 2 Pro",       "gMax 2 Dual 2in1", "gMax 2 Dual Chimera"}},
     {"Geeetech",       {"Thunder",         "Thunder Pro",      "Mizar s",          "Mizar Pro",        "Mizar",        "Mizar Max",
                        "Mizar M",          "A10 Pro",          "A10 M",            "A10 T",            "A20",          "A20 M",
                        "A20T",             "A30 Pro",          "A30 M",            "A30 T",            "E180",         "Me Ducer",
                        "Me creator",       "Me Creator2",      "GiantArmD200",     "l3 ProB",          "l3 Prow",      "l3 ProC"}},
     {"INAT",           {"Proton X Rail",   "Proton x Rod",     "Proton XE-750"}},
     {"Infinity3D",     {"DEV-200",         "DEV-350"}},
     {"Jubilee",        {"Jubilee"}},
     {"LNL3D",          {"D3 v2",           "D3 Vulcan",        "D5",               "D6"}},
     {"LulzBot",        {"Mini Aero",       "Taz6 Aero"}},
     {"MakerGear",      {"Micro",           "M2(V4 Hotend)",    "M2 Dual",          "M3-single Extruder", "M3-Independent Dual Rev.0", "M3-Independent Dual Rev.0(Duplication Mode)",
                        "M3-Independent Dual Rev.1",            "M3-Independent Dual Rev.1(Duplication Mode)", "ultra One", "Ultra One (DuplicationMode)"}},
     {"Papapiu",        {"N1s"}},
     {"Print4Taste",    {"mycusini 2.0"}},
     {"RatRig",         {"V-core-3 300mm",  "V-Core-3 400mm",   "V-Core-3 500mm", "V-Minion"}},
     {"Rigid3D",        {"Zero2",           "Zero3"}},
     {"Snapmaker",      {"A250",            "A350"}},
     {"Sovol",          {"SV06",            "SV06 PLUS",        "SV05",             "SV04",             "SV03 / SV03 BLTOUCH",      "SVO2 / SV02 BLTOUCH",      "SVO1 / SV01 BLToUCH", "SV01 PRO"}},
     {"TriLAB",         {"AzteQ Industrial","AzteQ Dynamic",    "DeltiQ 2",         "DeltiQ 2 Plus",    "DeltiQ 2 + FlexPrint 2",   "DeltiQ 2 Plus + FlexPrint 2", "DeltiQ 2 +FlexPrint",
                        "DeltiQ 2 Plus + FlexPrint",            "DeltiQ M",         "DeltiQ L",         "DeltiQ XL"}},
     {"Trimaker",       {"Nebula cloud",    "Nebula",           "Cosmos ll"}},
     {"Ultimaker",      {"Ultimaker 2"}},
     {"Voron",          {"v2 250mm3",       "v2 300mm3",        "v2 350mm3",        "v2 250mm3",        "v2 300mm3",        "v2 350mm3",        "v1 250mm3",        "v1 300mm3", "v1 350mm3",
                        "Zero 120mm3",      "Switchwire"}},
     {"Zonestar",       {"Z5",              "Z6",               "Z5x",              "Z8",               "Z9"}}};

static const std::vector<std::string> nozzle_diameter = {"0.2","0.25", "0.3","0.35", "0.4", "0.5", "0.6","0.75", "0.8", "1.0", "1.2"};

static std::string get_curr_time()
{
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

    std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm            local_time = *std::localtime(&time);
    std::ostringstream time_stream;
    time_stream << std::put_time(&local_time, "%Y_%m_%d_%H_%M_%S");

    std::string current_time = time_stream.str();
    return current_time;
}

static std::string get_curr_timestmp()
{
    std::time_t currentTime = std::time(nullptr);
    std::ostringstream oss;
    oss << currentTime;
    std::string timestampString = oss.str();
    return timestampString;
}

static wxBoxSizer* create_checkbox(wxWindow* parent, Preset* preset, std::string& preset_name, std::vector<std::pair<CheckBox*, Preset*>>& preset_checkbox)
{
    wxBoxSizer *sizer    = new wxBoxSizer(wxHORIZONTAL);
    CheckBox *  checkbox = new CheckBox(parent);
    sizer->Add(checkbox, 0, 0, 0);
    preset_checkbox.push_back(std::make_pair(checkbox, preset));
    wxStaticText *preset_name_str = new wxStaticText(parent, wxID_ANY, preset_name);
    sizer->Add(preset_name_str, 0, wxLEFT, 5);
    return sizer;
}

static wxBoxSizer *create_checkbox(wxWindow *parent, std::string &preset_name, std::vector<std::pair<CheckBox *, std::string>> &preset_checkbox)
{
    wxBoxSizer *sizer    = new wxBoxSizer(wxHORIZONTAL);
    CheckBox *  checkbox = new CheckBox(parent);
    sizer->Add(checkbox, 0, 0, 0);
    preset_checkbox.push_back(std::make_pair(checkbox, preset_name));
    wxStaticText *preset_name_str = new wxStaticText(parent, wxID_ANY, preset_name);
    sizer->Add(preset_name_str, 0, wxLEFT, 5);
    return sizer;
}

static wxArrayString get_exist_vendor_choices(VendorMap& vendors)
{
    wxArrayString                                      choices;
    PresetBundle                                       temp_preset_bundle;
    std::pair<PresetsConfigSubstitutions, std::string> system_models = temp_preset_bundle.load_system_models_from_json(ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    PresetBundle *                                     preset_bundle = wxGetApp().preset_bundle;
    VendorProfile                                      users_models  = preset_bundle->get_custom_vendor_models();

    vendors = temp_preset_bundle.vendors;

    if (!users_models.models.empty()) {
        vendors[users_models.name] = users_models;
    }

    for (const pair<std::string, VendorProfile> &vendor : vendors) {
        if (vendor.second.models.empty() || vendor.second.id.empty()) continue;
        choices.Add(vendor.first);
    }
    return choices;
}

static std::string get_machine_name(const std::string &preset_name)
{
    size_t index_at = preset_name.find("@");
    if (std::string::npos == index_at) {
        return "";
    } else {
        return preset_name.substr(index_at + 1);
    }
}

static std::string get_filament_name(std::string &preset_name)
{
    size_t index_at = preset_name.find("@");
    if (std::string::npos == index_at) {
        return preset_name;
    } else {
        return preset_name.substr(0, index_at - 1);
    }
}

static std::string get_vendor_name(std::string& preset_name)
{
    if (preset_name.empty()) return "";
    std::string vendor_name = preset_name.substr(preset_name.find_first_not_of(' '));
    size_t index_at = vendor_name.find(" ");
    if (std::string::npos == index_at) {
        return vendor_name;
    } else {
        vendor_name = vendor_name.substr(0, index_at);
        return vendor_name;
    }
}

static wxBoxSizer *create_select_filament_preset_checkbox(wxWindow *                                    parent,
                                                          std::string &                                 machine_name,
                                                          std::vector<Preset *>                         presets,
                                                          std::unordered_map<CheckBox *, Preset *> &machine_filament_preset)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *checkbox_sizer   = new wxBoxSizer(wxVERTICAL);
    CheckBox *  checkbox         = new CheckBox(parent);
    checkbox_sizer->Add(checkbox, 0, wxEXPAND | wxRIGHT, 5);

    wxBoxSizer *combobox_sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *machine_name_str = new wxStaticText(parent, wxID_ANY, machine_name);
    ComboBox *    combobox        = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(200, 24), 0, nullptr, wxCB_READONLY);
    combobox->SetBackgroundColor(PRINTER_LIST_COLOUR);
    combobox->SetLabel(_L("Select filament preset"));
    combobox->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    combobox->Bind(wxEVT_COMBOBOX, [combobox, checkbox, presets, &machine_filament_preset](wxCommandEvent &e) {
        combobox->SetLabelColor(*wxBLACK);
        std::string preset_name = into_u8(combobox->GetLabel());
        for (Preset *preset : presets) {
            if (preset_name == preset->name) {
                machine_filament_preset[checkbox] = preset;
            }
        }
        e.Skip();
    });
    combobox_sizer->Add(machine_name_str, 0, wxEXPAND, 0);
    combobox_sizer->Add(combobox, 0, wxEXPAND | wxTOP, 5);

    wxArrayString choices;
    for (Preset *preset : presets) { 
        choices.Add(preset->name);
    }
    combobox->Set(choices);

    horizontal_sizer->Add(checkbox_sizer);
    horizontal_sizer->Add(combobox_sizer);
    return horizontal_sizer;
}

static wxString get_curr_radio_type(std::vector<std::pair<RadioBox *, wxString>> &radio_btns)
{
    for (std::pair<RadioBox *, wxString> radio_string : radio_btns) {
        if (radio_string.first->GetValue()) {
            return radio_string.second;
        }
    }
    return "";
}

static std::string calculate_md5(const std::string &input)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    std::string   md5;

    EVP_MD_CTX *mdContext = EVP_MD_CTX_new();
    EVP_DigestInit(mdContext, EVP_md5());
    EVP_DigestUpdate(mdContext, input.c_str(), input.length());
    EVP_DigestFinal(mdContext, digest, nullptr);
    EVP_MD_CTX_free(mdContext);

    char hexDigest[MD5_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) { sprintf(hexDigest + (i * 2), "%02x", digest[i]); }
    hexDigest[MD5_DIGEST_LENGTH * 2] = '\0';

    md5 = std::string(hexDigest);
    return md5;
}

static std::string get_filament_id(std::string vendor_typr_serial)
{
    std::string   user_filament_id = "P" + calculate_md5(vendor_typr_serial).substr(0, 7);

    std::unordered_map<std::string,std::vector<std::string>> filament_id_to_filament_name;

    // temp filament presets
    PresetBundle temp_preset_bundle;
    temp_preset_bundle.load_system_filaments_json(Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
    std::string dir_user_presets = wxGetApp().app_config->get("preset_folder");
    if (dir_user_presets.empty()) {
        temp_preset_bundle.load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::EnableSilent);
    } else {
        temp_preset_bundle.load_user_presets(dir_user_presets, ForwardCompatibilitySubstitutionRule::EnableSilent);
    }
    const std::deque<Preset> &filament_presets = temp_preset_bundle.filaments.get_presets();

    for (const Preset &preset : filament_presets) {
        std::string preset_name = preset.name;
        filament_id_to_filament_name[preset.filament_id].push_back(get_filament_name(preset_name));
    }
    // global filament presets
    PresetBundle *                                     preset_bundle               = wxGetApp().preset_bundle;
    std::map<std::string, std::vector<Preset const *>> temp_filament_id_to_presets = preset_bundle->filaments.get_filament_presets();
    for (std::pair<std::string, std::vector<Preset const *>> filament_id_to_presets : temp_filament_id_to_presets) {
        if (filament_id_to_presets.first.empty()) continue;
        for (const Preset *preset : filament_id_to_presets.second) {
            std::string preset_name = preset->name;
            filament_id_to_filament_name[preset->filament_id].push_back(get_filament_name(preset_name));
        }
    }
    while (filament_id_to_filament_name.find(user_filament_id) != filament_id_to_filament_name.end()) {//find same filament id
        bool have_same_filament_name = false;
        for (const std::string &name : filament_id_to_filament_name.find(user_filament_id)->second) {
            if (name == vendor_typr_serial) {
                have_same_filament_name = true;
                break;
            }
        }
        if (have_same_filament_name) {
            break;
        }
        else {
            user_filament_id = "P" + calculate_md5(vendor_typr_serial + get_curr_time()).substr(0, 7);
        }
    }


    return user_filament_id;
}

static json get_config_json(const Preset* preset) {
    json j;
    // record the headers
    j[BBL_JSON_KEY_VERSION] = preset->version.to_string();
    j[BBL_JSON_KEY_NAME]    = preset->name;
    j[BBL_JSON_KEY_FROM]    = "";

   DynamicPrintConfig config = preset->config;

    // record all the key-values
    for (const std::string &opt_key : config.keys()) {
        const ConfigOption *opt = config.option(opt_key);
        if (opt->is_scalar()) {
            if (opt->type() == coString)
                // keep \n, \r, \t
                j[opt_key] = (dynamic_cast<const ConfigOptionString *>(opt))->value;
            else
                j[opt_key] = opt->serialize();
        } else {
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase *>(opt);
            std::vector<std::string> string_values = vec->vserialize();

            json j_array(string_values);
            j[opt_key] = j_array;
        }
    }

    return j;
}

static char* read_json_file(const std::string &preset_path)
{
    FILE *json_file = boost::nowide::fopen(boost::filesystem::path(preset_path).make_preferred().string().c_str(), "rb");
    if (json_file == NULL) {
        BOOST_LOG_TRIVIAL(info) << "Failed to open JSON file: " << preset_path;
        return NULL;
    }
    fseek(json_file, 0, SEEK_END);     // seek to end
    long file_size = ftell(json_file); // get file size
    fseek(json_file, 0, SEEK_SET);     // seek to start

    char * json_contents = (char *) malloc(file_size);
    if (json_contents == NULL) {
        BOOST_LOG_TRIVIAL(info) << "Failed to allocate memory for JSON file ";
        fclose(json_file);
        return NULL;
    }

    fread(json_contents, 1, file_size, json_file);
    fclose(json_file);

    return json_contents;
}

CreateFilamentPresetDialog::CreateFilamentPresetDialog(wxWindow *parent) 
	: DPIDialog(parent ? parent : nullptr, wxID_ANY, _L("Creat Filament"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX) {
	
    m_create_type.base_filament = _L("Create based on current filamet");
    m_create_type.base_filament_preset = _L("Copy current filament preset ");

	this->SetBackgroundColour(*wxWHITE);
    this->SetSize(wxSize(FromDIP(600), FromDIP(480)));

    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

	wxBoxSizer *m_main_sizer = new wxBoxSizer(wxVERTICAL);
    // top line
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_main_sizer->Add(m_line_top, 0, wxEXPAND, 0);
    m_main_sizer->Add(0, 0, 0, wxTOP, FromDIP(5));

    m_main_sizer->Add(create_item(FilamentOptionType::VENDOR), 0, wxEXPAND | wxALL, FromDIP(5));
    m_main_sizer->Add(create_item(FilamentOptionType::TYPE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    m_main_sizer->Add(create_item(FilamentOptionType::SERIAL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    m_main_sizer->Add(create_item(FilamentOptionType::FILAMENT_PRESET), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    m_main_sizer->Add(create_item(FilamentOptionType::PRESET_FOR_PRINTER), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    m_main_sizer->Add(create_button_item(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));

    get_all_filament_presets();
    select_curr_radiobox(m_create_type_btns, 0);

    this->SetSizer(m_main_sizer);

    Layout();
    Fit();

	wxGetApp().UpdateDlgDarkUI(this);
}

CreateFilamentPresetDialog::~CreateFilamentPresetDialog()
{
    for (std::pair<std::string, Preset *> preset : m_all_presets_map) {
        Preset *p = preset.second;
        if (p) {
            delete p;
            p = nullptr;
        }
    }
}

void CreateFilamentPresetDialog::on_dpi_changed(const wxRect &suggested_rect) {}

wxBoxSizer *CreateFilamentPresetDialog::create_item(FilamentOptionType option_type)
{

    wxSizer *item = nullptr;
    switch (option_type) {
        case VENDOR:             return create_vendor_item();
        case TYPE:               return create_type_item();
        case SERIAL:             return create_serial_item();
        case FILAMENT_PRESET:    return create_filament_preset_item();
        case PRESET_FOR_PRINTER: return create_filament_preset_for_printer_item();
        default:                 return nullptr;
    }
}
wxBoxSizer *CreateFilamentPresetDialog::create_vendor_item()
{ 
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL); 

    wxBoxSizer *  optionSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_vendor_text = new wxStaticText(this, wxID_ANY, _L("Vendor"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_vendor_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10)); 

    wxArrayString choices;
    for (const wxString &vendor : filament_vendors) {
        choices.push_back(vendor);
    }
    wxString no_vendor_choice = _L("No vendor I want");
    choices.push_back(no_vendor_choice);

    wxBoxSizer *comboBoxSizer = new wxBoxSizer(wxVERTICAL);
    m_filament_vendor_combobox = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    m_filament_vendor_combobox->SetLabel(_L("Select Vendor"));
    m_filament_vendor_combobox->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    m_filament_vendor_combobox->Set(choices);
    comboBoxSizer->Add(m_filament_vendor_combobox, 0, wxEXPAND | wxALL, 0);
    m_filament_vendor_combobox->Bind(wxEVT_COMBOBOX, [this, no_vendor_choice](wxCommandEvent &e) { 
        m_filament_vendor_combobox->SetLabelColor(*wxBLACK);
        wxString vendor_name = m_filament_vendor_combobox->GetStringSelection();
        if (vendor_name == no_vendor_choice) { 
            m_filament_custom_vendor_input->Show();
        } else {
            m_filament_custom_vendor_input->Hide();
        }
        Layout();
        Fit();
    });
    horizontal_sizer->Add(comboBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *textInputSizer = new wxBoxSizer(wxVERTICAL);
    m_filament_custom_vendor_input = new TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, wxTE_PROCESS_ENTER);
    m_filament_custom_vendor_input->SetSize(NAME_OPTION_COMBOBOX_SIZE);
    textInputSizer->Add(m_filament_custom_vendor_input, 0, wxEXPAND | wxALL, 0);
    m_filament_custom_vendor_input->GetTextCtrl()->SetHint(_L("Input custom vendor"));
    m_filament_custom_vendor_input->Hide();
    horizontal_sizer->Add(textInputSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;

}

wxBoxSizer *CreateFilamentPresetDialog::create_type_item()
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(this, wxID_ANY, _L("Type"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxArrayString filament_type;
    for (const wxString &filament : system_filament_types) { 
        filament_type.Add(filament);
    }

    wxBoxSizer *comboBoxSizer = new wxBoxSizer(wxVERTICAL);
    m_filament_type_combobox  = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    m_filament_type_combobox->SetLabel(_L("Select Type"));
    m_filament_type_combobox->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    m_filament_type_combobox->Set(filament_type);
    comboBoxSizer->Add(m_filament_type_combobox, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(comboBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    m_filament_type_combobox->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) { 
        m_filament_type_combobox->SetLabelColor(*wxBLACK);
        const wxString &curr_create_type = curr_create_filament_type();
        clear_filament_preset_map();
        if (curr_create_type == m_create_type.base_filament) {
            wxArrayString filament_preset_choice = get_filament_preset_choices();
            m_filament_preset_combobox->Set(filament_preset_choice);
            m_filament_preset_combobox->SetLabel(_L("Select Filament Preset"));
            m_filament_preset_combobox->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
            
        } else if (curr_create_type == m_create_type.base_filament_preset) {
            get_filament_presets_by_machine();
        }
        Layout();
        Fit();
        e.Skip();
    });

    return horizontal_sizer;
}

wxBoxSizer *CreateFilamentPresetDialog::create_serial_item()
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_serial_text = new wxStaticText(this, wxID_ANY, _L("Serial"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_serial_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *comboBoxSizer = new wxBoxSizer(wxVERTICAL);
    m_filament_serial_input   = new TextInput(this, "", "", "", wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, wxTE_PROCESS_ENTER);
    comboBoxSizer->Add(m_filament_serial_input, 0, wxEXPAND | wxALL, 0);
    wxStaticText *static_eg_text = new wxStaticText(this, wxID_ANY, _L("e.g. Basic, Matte, Silk, Marble"), wxDefaultPosition, wxDefaultSize);
    static_eg_text->SetForegroundColour(wxColour("#6B6B6B"));
    static_eg_text->SetFont(::Label::Body_12);
    comboBoxSizer->Add(static_eg_text, 0, wxEXPAND | wxTOP, FromDIP(5));
    horizontal_sizer->Add(comboBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *CreateFilamentPresetDialog::create_filament_preset_item()
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_filament_preset_text = new wxStaticText(this, wxID_ANY, _L("Filament preset"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_filament_preset_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *  comboBoxSizer  = new wxBoxSizer(wxVERTICAL);
    comboBoxSizer->Add(create_radio_item(m_create_type.base_filament, this, wxEmptyString, m_create_type_btns), 0, wxEXPAND | wxALL, 0);
    
    m_filament_preset_combobox = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FILAMENT_PRESET_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    m_filament_preset_combobox->SetLabel(_L("Select Filament Preset"));
    m_filament_preset_combobox->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);

    
    m_filament_preset_combobox->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        m_filament_preset_combobox->SetLabelColor(*wxBLACK);
        std::string filament_type = into_u8(m_filament_preset_combobox->GetStringSelection());
        std::unordered_map<std::string, std::vector<Preset *>>::iterator iter          = m_filament_choice_map.find(m_public_name_to_filament_id_map[filament_type]);
        
        m_filament_preset_panel->Freeze();
        m_filament_presets_sizer->Clear(true);
        m_filament_preset_panel->Thaw();
        m_filament_preset.clear();
        if (iter != m_filament_choice_map.end()) {
            for (Preset* preset : iter->second) { 
                std::string preset_name = wxString::FromUTF8(preset->name).ToStdString();
                size_t      index_at    = preset_name.find("@");
                if (std::string::npos != index_at) {
                    std::string machine_name = preset_name.substr(index_at + 1);
                    m_filament_presets_sizer->Add(create_checkbox(m_filament_preset_panel, preset, machine_name, m_filament_preset), 0, wxEXPAND | wxTOP | wxLEFT, FromDIP(5));
                }
            }
        } else {

        }

        Layout();
        Fit();
        Refresh();

        e.Skip();
    });

    comboBoxSizer->Add(m_filament_preset_combobox, 0, wxEXPAND | wxTOP, FromDIP(5));

    comboBoxSizer->Add(create_radio_item(m_create_type.base_filament_preset, this, wxEmptyString, m_create_type_btns), 0, wxEXPAND | wxTOP, FromDIP(10));
    
    horizontal_sizer->Add(comboBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    horizontal_sizer->Add(0, 0, 0, wxLEFT, FromDIP(30));

    return horizontal_sizer;

}

wxBoxSizer *CreateFilamentPresetDialog::create_filament_preset_for_printer_item()
{
    wxBoxSizer *vertical_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *  optionSizer                 = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_filament_preset_text = new wxStaticText(this, wxID_ANY, _L("We could create the filament presets for your following printer:"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_filament_preset_text, 0, wxEXPAND | wxALL, 0);

    m_filament_preset_panel = new wxPanel(this, wxID_ANY);
    m_filament_preset_panel->SetBackgroundColour(PRINTER_LIST_COLOUR);
    //m_filament_preset_panel->SetMinSize(PRINTER_LIST_SIZE);
    m_filament_preset_panel->SetSize(PRINTER_LIST_SIZE);
    m_filament_presets_sizer = new wxGridSizer(3, FromDIP(5), FromDIP(5));
    m_filament_preset_panel->SetSizer(m_filament_presets_sizer);
    optionSizer->Add(m_filament_preset_panel, 0, wxEXPAND | wxTOP | wxALIGN_CENTER_HORIZONTAL, FromDIP(5));

    vertical_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return vertical_sizer;
}

wxBoxSizer *CreateFilamentPresetDialog::create_button_item()
{
    wxBoxSizer *bSizer_button = new wxBoxSizer(wxHORIZONTAL);
    bSizer_button->Add(0, 0, 1, wxEXPAND, 0);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_create = new Button(this, _L("Create"));
    m_button_create->SetBackgroundColor(btn_bg_green);
    m_button_create->SetBorderColor(*wxWHITE);
    m_button_create->SetTextColor(wxColour(0xFFFFFE));
    m_button_create->SetFont(Label::Body_12);
    m_button_create->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_create->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_create->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_create, 0, wxRIGHT, FromDIP(10));

    m_button_create->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { 
        //get vendor name
        wxString vendor_str = m_filament_vendor_combobox->GetLabel();
        std::string vendor_name;
        if (_L("Select Vendor") == vendor_str) {
            MessageDialog dlg(this, _L("Vendor is not selected, please reselect vendor."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
            dlg.ShowModal();
            return;
        } else if (_L("No vendor I want") == vendor_str) {
            if (m_filament_custom_vendor_input->GetTextCtrl()->GetValue().empty()) {
                MessageDialog dlg(this, _L("Custom vendor is not input, please input custom vendor."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                  wxYES | wxYES_DEFAULT | wxCENTRE);
                dlg.ShowModal();
                return;
            } else {
                vendor_name = into_u8(m_filament_custom_vendor_input->GetTextCtrl()->GetValue());
            }
        } else {
            vendor_name = into_u8(vendor_str);
        }

        //get fialment type name
        wxString type_str = m_filament_type_combobox->GetLabel();
        std::string type_name;
        if (_L("Select Type") == type_str) {
            MessageDialog dlg(this, _L("Filament type is not selected, please reselect type."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
            dlg.ShowModal();
            return;
        } else {
            type_name = into_u8(type_str);
        }
        //get filament serial
        wxString    serial_str = m_filament_serial_input->GetTextCtrl()->GetValue();
        std::string serial_name;
        if (serial_str.empty()) {
            MessageDialog dlg(this, _L("Filament serial is not inputed, please input serial."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                              wxYES | wxYES_DEFAULT | wxCENTRE);
            dlg.ShowModal();
            return;
        } else {
            serial_name = into_u8(serial_str);
        }

        std::string filament_preset_name = vendor_name + " " + type_name + " " + serial_name;
        std::string user_filament_id     = get_filament_id(filament_preset_name);

        const wxString &curr_create_type = curr_create_filament_type();
        PresetBundle *  preset_bundle    = wxGetApp().preset_bundle;
        std::vector<Preset const *> filament_presets;
        if (curr_create_type == m_create_type.base_filament) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":clone filament  create type  filament ";
            for (const std::pair<CheckBox *, Preset *> &preset : m_filament_preset) {
                if (preset.first->GetValue()) { 
                    filament_presets.push_back(preset.second);
                }
            }
            if (!filament_presets.empty()) {
                std::vector<std::string> failures;
                bool                     res = preset_bundle->filaments.clone_presets_for_filament(filament_presets, failures, filament_preset_name, user_filament_id);
                if (!res) {
                    std::string   failure_names;
                    for (std::string &failure : failures) {
                        failure_names += failure + "\n";
                    }
                    MessageDialog dlg(this, _L("Some existing presets have failed to be created, as follows:\n") + failure_names + _L("\nDo you want to rewrite it?"),
                                      wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
                    if (dlg.ShowModal() == wxID_YES) {
                        res = preset_bundle->filaments.clone_presets_for_filament(filament_presets, failures, filament_preset_name, user_filament_id, true);
                        BOOST_LOG_TRIVIAL(info) << "clone filament  have failures  rewritten  is successful? "<< res;
                    } else {
                        std::vector<Preset const *> temp_filament_presets;
                        for (const Preset* preset : filament_presets) { 
                            for (const std::string &exist_name : failures) {
                                if (exist_name == preset->name) { 
                                    continue;
                                }
                                temp_filament_presets.push_back(preset);
                            }
                        }
                        preset_bundle->filaments.clone_presets_for_filament(temp_filament_presets, failures, filament_preset_name, user_filament_id);
                        BOOST_LOG_TRIVIAL(info) << "clone filament  have failures  not rewritten  is successful? " << res;
                    }
                }
                BOOST_LOG_TRIVIAL(info) << "clone filament  no failures  is successful? " << res;
            } else {
                MessageDialog dlg(this, _L("You have not selected a filament preset, please choose."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                  wxYES | wxYES_DEFAULT | wxCENTRE);
                dlg.ShowModal();
                return;
            }
        } else if (curr_create_type == m_create_type.base_filament_preset) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":clone filament presets  create type  filament preset";
            for (const std::pair<CheckBox *, Preset *> &checkbox_preset : m_machint_filament_preset) {
                if (checkbox_preset.first->GetValue()) {
                    filament_presets.push_back(checkbox_preset.second);
                }
            }
            if (!filament_presets.empty()) {
                std::vector<std::string> failures;
                bool                     res = preset_bundle->filaments.clone_presets_for_filament(filament_presets, failures, filament_preset_name, user_filament_id);
                if (!res) {
                    std::string failure_names;
                    for (std::string &failure : failures) { failure_names += failure + "\n"; }
                    MessageDialog dlg(this, _L("Some existing presets have failed to be created, as follows:\n") + failure_names,
                                      wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
                    if (wxID_YES == dlg.ShowModal()) {
                        res = preset_bundle->filaments.clone_presets_for_filament(filament_presets, failures, filament_preset_name, user_filament_id, true);
                        BOOST_LOG_TRIVIAL(info) << "clone filament presets  have failures  rewritten  is successful? " << res;
                    } else {
                        std::vector<Preset const *> temp_filament_presets;
                        for (const Preset *preset : filament_presets) {
                            for (const std::string &exist_name : failures) {
                                if (exist_name == preset->name) { continue; }
                                temp_filament_presets.push_back(preset);
                            }
                        }
                        preset_bundle->filaments.clone_presets_for_filament(temp_filament_presets, failures, filament_preset_name, user_filament_id);
                        BOOST_LOG_TRIVIAL(info) << "clone filament  have failures  not rewritten  is successful? " << res;
                    }
                }
                BOOST_LOG_TRIVIAL(info) << "clone filament presets  no failures  is successful? " << res;
            } else {
                MessageDialog dlg(this, _L("You have not selected a filament preset, please choose."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                  wxYES | wxYES_DEFAULT | wxCENTRE);
                dlg.ShowModal();
                return;
            }
        }

        EndModal(wxID_OK); 
        });

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_cancel, 0, wxRIGHT, FromDIP(10));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { 
        EndModal(wxID_CANCEL); 
        });

    return bSizer_button;
}

wxArrayString CreateFilamentPresetDialog::get_filament_preset_choices()
{ 
    wxArrayString choices;
    // get fialment type name
    wxString    type_str = m_filament_type_combobox->GetLabel();
    std::string type_name;
    if (_L("Select Type") == type_str) {
        /*MessageDialog dlg(this, _L("Filament type is not selected, please reselect type."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();*/
        return choices;
    } else {
        type_name = into_u8(type_str);
    }

    for (std::pair<std::string, Preset*> filament_presets : m_all_presets_map) {
        Preset *preset = filament_presets.second;
        if (std::string::npos == filament_presets.first.find(type_name)) continue;
        m_filament_choice_map[preset->filament_id].push_back(preset);
    }
    
    int suffix = 0;
    for (const pair<std::string, std::vector<Preset *>> &preset : m_filament_choice_map) { 
        if (preset.second.empty()) continue;
        std::set<std::string> preset_name_set;
        for (Preset* filament_preset : preset.second) { 
            std::string preset_name = filament_preset->name;
            size_t      index_at    = preset_name.find("@");
            if (std::string::npos != index_at) {
                std::string cur_preset_name = preset_name.substr(0, index_at - 1);
                preset_name_set.insert(cur_preset_name);
            }
        }
        assert(1 == preset_name_set.size());
        for (const std::string& public_name : preset_name_set) {
            if (m_public_name_to_filament_id_map.find(public_name) != m_public_name_to_filament_id_map.end()) {
                suffix++;
                m_public_name_to_filament_id_map[public_name + "_" + std::to_string(suffix)] = preset.first;
                choices.Add(public_name + "_" + std::to_string(suffix));
            } else {
                m_public_name_to_filament_id_map[public_name] = preset.first;
                choices.Add(public_name);
            }
        }
    }

    return choices;
}

wxBoxSizer *CreateFilamentPresetDialog::create_radio_item(wxString title, wxWindow *parent, wxString tooltip, std::vector<std::pair<RadioBox *, wxString>> &radiobox_list)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);
    RadioBox *  radiobox         = new RadioBox(parent);
    horizontal_sizer->Add(radiobox, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(5));
    radiobox_list.push_back(std::make_pair(radiobox, title));
    int btn_idx = radiobox_list.size() - 1;
    radiobox->Bind(wxEVT_LEFT_DOWN, [this, &radiobox_list, btn_idx](wxMouseEvent &e) { select_curr_radiobox(radiobox_list, btn_idx); });

    wxStaticText *text = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->Bind(wxEVT_LEFT_DOWN, [this, &radiobox_list, btn_idx](wxMouseEvent &e) { select_curr_radiobox(radiobox_list, btn_idx); });
    horizontal_sizer->Add(text, 0, wxEXPAND | wxLEFT, 0);

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return horizontal_sizer;
}

void CreateFilamentPresetDialog::select_curr_radiobox(std::vector<std::pair<RadioBox *, wxString>> &radiobox_list, int btn_idx)
{
    int len = radiobox_list.size();
    for (int i = 0; i < len; ++i) {
        if (i == btn_idx) {
            radiobox_list[i].first->SetValue(true);
            const wxString &curr_selected_type = radiobox_list[i].second;
            if (curr_selected_type == m_create_type.base_filament) {
                m_filament_preset_combobox->Show();
                if (_L("Select Type") != m_filament_type_combobox->GetLabel()) {
                    clear_filament_preset_map();
                    wxArrayString filament_preset_choice = get_filament_preset_choices();
                    m_filament_preset_combobox->Set(filament_preset_choice);
                    m_filament_preset_combobox->SetLabel(_L("Select Filament Preset"));
                    m_filament_preset_combobox->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
                }
            } else if (curr_selected_type == m_create_type.base_filament_preset) {
                m_filament_preset_combobox->Hide();
                if (_L("Select Type") != m_filament_type_combobox->GetLabel()) {
                    clear_filament_preset_map();
                    get_filament_presets_by_machine();
                }
            }
            Fit();
            Layout();
            Refresh();
        } else {
            radiobox_list[i].first->SetValue(false);
        }
    }
}

wxString CreateFilamentPresetDialog::curr_create_filament_type()
{
    wxString curr_filament_type;
    for (const std::pair<RadioBox *, wxString> &printer_radio : m_create_type_btns) {
        if (printer_radio.first->GetValue()) {
            curr_filament_type = printer_radio.second;
        }
    }
    return curr_filament_type;
}

void CreateFilamentPresetDialog::get_filament_presets_by_machine()
{
    wxArrayString choices;
    // get fialment type name
    wxString    type_str = m_filament_type_combobox->GetLabel();
    std::string type_name;
    if (_L("Select Type") == type_str) {
        /*MessageDialog dlg(this, _L("Filament type is not selected, please reselect type."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT |
        wxCENTRE); dlg.ShowModal();*/
        return;
    } else {
        type_name = into_u8(type_str);
    }
    
    std::unordered_map<std::string, std::vector<Preset *>> machine_name_to_presets;
    for (std::pair<std::string, Preset*> filament_preset : m_all_presets_map) { 
        std::string preset_name = filament_preset.first;
        if (std::string::npos == preset_name.find(type_name)) continue;
        size_t index_at = preset_name.find("@");
        if (std::string::npos == index_at) continue;
        else {
            std::string machine_name = preset_name.substr(index_at + 1);
            machine_name_to_presets[machine_name].push_back(filament_preset.second);
        }
    }

    for (std::pair<std::string, std::vector<Preset *>> machine_filament_presets : machine_name_to_presets) {
        std::string machine_name = machine_filament_presets.first;
        std::vector<Preset *> &presets      = machine_filament_presets.second;
        m_filament_presets_sizer->Add(create_select_filament_preset_checkbox(m_filament_preset_panel, machine_name, presets, m_machint_filament_preset), 0, wxEXPAND | wxALL, FromDIP(5));
    }

}

void CreateFilamentPresetDialog::get_all_filament_presets()
{
    // temp filament presets
    PresetBundle temp_preset_bundle;
    //temp_preset_bundle.load_system_filaments_json(Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
    std::string dir_user_presets = wxGetApp().app_config->get("preset_folder");
    if (dir_user_presets.empty()) {
        temp_preset_bundle.load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::EnableSilent);
    } else {
        temp_preset_bundle.load_user_presets(dir_user_presets, ForwardCompatibilitySubstitutionRule::EnableSilent);
    }
    const std::deque<Preset> &filament_presets = temp_preset_bundle.filaments.get_presets();

    for (const Preset &preset : filament_presets) {
        if (preset.filament_id.empty()) continue; // to do: empty filament id is user preset
        std::string filament_preset_name        = wxString::FromUTF8(preset.name).ToStdString();
        Preset *filament_preset = new Preset(preset);
        m_all_presets_map[filament_preset_name] = filament_preset;
    }
    // global filament presets
    PresetBundle *                                     preset_bundle               = wxGetApp().preset_bundle;
    std::map<std::string, std::vector<Preset const *>> temp_filament_id_to_presets = preset_bundle->filaments.get_filament_presets();
    for (std::pair<std::string, std::vector<Preset const *>> filament_id_to_presets : temp_filament_id_to_presets) {
        if (filament_id_to_presets.first.empty()) continue;
        for (const Preset *preset : filament_id_to_presets.second) {
            std::string filament_preset_name = wxString::FromUTF8(preset->name).ToStdString();
            if (!preset->is_visible) continue;
            Preset *filament_preset = new Preset(*preset);
            m_all_presets_map[filament_preset_name] = filament_preset;
        }
    }
}

void CreateFilamentPresetDialog::clear_filament_preset_map()
{
    m_filament_choice_map.clear();
    m_filament_preset.clear();
    m_machint_filament_preset.clear();
    m_public_name_to_filament_id_map.clear();
    m_filament_preset_panel->Freeze();
    m_filament_presets_sizer->Clear(true);
    m_filament_preset_panel->Thaw();
}

CreatePrinterPresetDialog::CreatePrinterPresetDialog(wxWindow *parent) 
: DPIDialog(parent ? parent : nullptr, wxID_ANY, _L("Create Printer/Nozzle"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    m_create_presets_type = {_L("Create from template"), _L("Create based on current printer")};
    m_create_printer_type = {_L("Create Printer"), _L("Create Nozzle for existing printer")};

    this->SetBackgroundColour(*wxWHITE);
    this->SetSize(wxSize(FromDIP(600), FromDIP(600)));

    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    wxBoxSizer *m_main_sizer = new wxBoxSizer(wxVERTICAL);
    // top line
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 2), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_main_sizer->Add(m_line_top, 0, wxEXPAND, 0);
    m_main_sizer->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_main_sizer->Add(create_step_switch_item(), 0, wxEXPAND | wxALL, FromDIP(5));

    wxBoxSizer *page_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_page1 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_page1->SetBackgroundColour(*wxWHITE);
    m_page2 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_page2->SetBackgroundColour(*wxWHITE);

    create_printer_page1(m_page1);
    create_printer_page2(m_page2);
    m_page2->Hide();

    page_sizer->Add(m_page1, 1, wxEXPAND, 0);
    page_sizer->Add(m_page2, 1, wxEXPAND, 0);
    m_main_sizer->Add(page_sizer, 0, wxEXPAND | wxRIGHT, FromDIP(10));
    select_curr_radiobox(m_create_type_btns, 0);
    select_curr_radiobox(m_create_presets_btns, 0);

    m_main_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));

    this->SetSizer(m_main_sizer);

    Layout();
    Fit();
    
    wxGetApp().UpdateDlgDarkUI(this);
}

CreatePrinterPresetDialog::~CreatePrinterPresetDialog()
{
    clear_preset_combobox();
}

void CreatePrinterPresetDialog::on_dpi_changed(const wxRect &suggested_rect) {}

wxBoxSizer *CreatePrinterPresetDialog::create_step_switch_item()
{ 
    wxBoxSizer *step_switch_sizer = new wxBoxSizer(wxVERTICAL); 

    std::string      wiki_url             = "https://makerhub-pre.bambu-lab.com";
    wxHyperlinkCtrl *m_download_hyperlink = new wxHyperlinkCtrl(this, wxID_ANY, _L("wiki"), wiki_url, wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    step_switch_sizer->Add(m_download_hyperlink, 0,  wxRIGHT | wxALIGN_RIGHT, FromDIP(5));

    wxBoxSizer *horizontal_sizer  = new wxBoxSizer(wxHORIZONTAL);
    m_step_1                      = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("step_1", nullptr, FromDIP(20)), wxDefaultPosition, wxDefaultSize);
    horizontal_sizer->Add(m_step_1, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(3));
    wxStaticText *static_create_printer_text = new wxStaticText(this, wxID_ANY, _L("Create Printer"), wxDefaultPosition, wxDefaultSize);
    horizontal_sizer->Add(static_create_printer_text, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(3));
    auto divider_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(50), 1));
    divider_line->SetBackgroundColour(PRINTER_LIST_COLOUR);
    horizontal_sizer->Add(divider_line, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(3));
    m_step_2 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("step_2_ready", nullptr, FromDIP(20)), wxDefaultPosition, wxDefaultSize);
    horizontal_sizer->Add(m_step_2, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(3));
    wxStaticText *static_improt_presets_text = new wxStaticText(this, wxID_ANY, _L("Improt Presets"), wxDefaultPosition, wxDefaultSize);
    horizontal_sizer->Add(static_improt_presets_text, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(3));

    step_switch_sizer->Add(horizontal_sizer, 0, wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, FromDIP(10));

    auto line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line_top->SetBackgroundColour(PRINTER_LIST_COLOUR);

    step_switch_sizer->Add(line_top, 0, wxEXPAND | wxALL, FromDIP(10));
    
    return step_switch_sizer;
}

void CreatePrinterPresetDialog::create_printer_page1(wxWindow *parent)
{ 
    this->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *page1_sizer = new wxBoxSizer(wxVERTICAL); 

    page1_sizer->Add(create_type_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_printer_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_nozzle_diameter_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_bed_shape_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_bed_size_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_origin_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_hot_bed_stl_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_hot_bed_svg_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_max_print_height_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page1_sizer->Add(create_page1_btns_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));

    parent->SetSizer(page1_sizer);
    Layout();

    wxGetApp().UpdateDlgDarkUI(this);
}

wxBoxSizer *CreatePrinterPresetDialog::create_type_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_serial_text = new wxStaticText(parent, wxID_ANY, _L("Create Type"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_serial_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *radioBoxSizer = new wxBoxSizer(wxVERTICAL);
    
    radioBoxSizer->Add(create_radio_item(m_create_printer_type[0], parent, wxEmptyString, m_create_type_btns), 0, wxEXPAND | wxALL, 0);
    radioBoxSizer->Add(create_radio_item(m_create_printer_type[1], parent, wxEmptyString, m_create_type_btns), 0, wxEXPAND | wxTOP, FromDIP(10));
    horizontal_sizer->Add(radioBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_printer_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_vendor_text = new wxStaticText(parent, wxID_ANY, _L("Printer"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_vendor_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *vertical_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *comboBoxSizer = new wxBoxSizer(wxHORIZONTAL);
    m_select_vendor            = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    m_select_vendor->SetValue(_L("Select Vendor"));
    m_select_vendor->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    wxArrayString printer_vendor;
    for (const std::string &vendor : printer_vendors) { 
        printer_vendor.Add(vendor); 
    }
    m_select_vendor->Set(printer_vendor);
    m_select_vendor->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent e) { 
        m_select_vendor->SetLabelColor(*wxBLACK);
        std::string curr_selected_vendor = into_u8(m_select_vendor->GetStringSelection());
        std::unordered_map<std::string,std::vector<std::string>>::const_iterator iter  = printer_model_map.find(curr_selected_vendor);
        if (iter != printer_model_map.end())
        {
            std::vector<std::string> vendor_model = iter->second;
            wxArrayString            model_choice;
            for (const std::string &model : vendor_model) { 
                model_choice.Add(model);
            }
            m_select_model->Set(model_choice);
            if (!model_choice.empty()) { 
                m_select_model->SetSelection(0);
                m_select_model->SetLabelColor(*wxBLACK);
            }
        } else {
            MessageDialog dlg(this, _L("The model is not fond, place reselect vendor."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
            dlg.ShowModal();
        }
        e.Skip();
    });
    
    comboBoxSizer->Add(m_select_vendor, 0, wxEXPAND | wxALL, 0);

    m_select_model = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    comboBoxSizer->Add(m_select_model, 0, wxEXPAND | wxLEFT, FromDIP(5));
    m_select_model->SetValue(_L("Select model"));
    m_select_model->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    m_select_model->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent e) {
        m_select_model->SetLabelColor(*wxBLACK);
        e.Skip();
    });

    m_select_printer = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    comboBoxSizer->Add(m_select_printer, 0, wxEXPAND | wxALL, 0);
    m_select_printer->SetValue(_L("Select printer"));
    m_select_printer->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    m_select_printer->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent e) {
        m_select_printer->SetLabelColor(*wxBLACK);
        e.Skip();
    });
    m_select_printer->Hide();

    vertical_sizer->Add(comboBoxSizer, 0, wxEXPAND, 0);

    wxBoxSizer *checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_can_not_find_vendor_combox = new CheckBox(parent);

    checkbox_sizer->Add(m_can_not_find_vendor_combox, 0, wxALIGN_CENTER, 0);
    checkbox_sizer->Add(0, 0, 0, wxEXPAND | wxRIGHT, FromDIP(5));

    m_can_not_find_vendor_text = new wxStaticText(parent, wxID_ANY, _L("Can't find my printer model"), wxDefaultPosition, wxDefaultSize, 0);
    m_can_not_find_vendor_text->SetFont(::Label::Body_13);

    wxSize size = m_can_not_find_vendor_text->GetTextExtent(_L("Can't find my printer model"));
    m_can_not_find_vendor_text->SetMinSize(wxSize(size.x + FromDIP(4), -1));
    m_can_not_find_vendor_text->Wrap(-1);
    checkbox_sizer->Add(m_can_not_find_vendor_text, 0, wxALIGN_CENTER, 0);

    m_custom_vendor_model = new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE);
    m_custom_vendor_model->SetHint(_L("Input Printer Vendor and Model"));
    checkbox_sizer->Add(m_custom_vendor_model, 0, wxLEFT | wxALIGN_CENTER, FromDIP(13));
    m_custom_vendor_model->Hide();

    m_can_not_find_vendor_combox->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
        bool value = m_can_not_find_vendor_combox->GetValue();
        if (value) {
            m_can_not_find_vendor_combox->SetValue(true);
            m_custom_vendor_model->Show();
            m_select_vendor->Enable(false);
            m_select_model->Enable(false);
        } else {
            m_can_not_find_vendor_combox->SetValue(false);
            m_custom_vendor_model->Hide();
            m_select_vendor->Enable(true);
            m_select_model->Enable(true);
        }
        Refresh();
        Layout();
        Fit();
    });

    vertical_sizer->Add(checkbox_sizer, 0, wxEXPAND | wxTOP, FromDIP(5));

    horizontal_sizer->Add(vertical_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;

}

wxBoxSizer *CreatePrinterPresetDialog::create_nozzle_diameter_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(parent, wxID_ANY, _L("Nozzle Diameter"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *comboBoxSizer = new wxBoxSizer(wxVERTICAL);
    m_nozzle_diameter         = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, NAME_OPTION_COMBOBOX_SIZE, 0, nullptr, wxCB_READONLY);
    wxArrayString nozzle_diameters;
    for (const std::string& nozzle : nozzle_diameter) { 
        nozzle_diameters.Add(nozzle + " nozzle");
    }
    m_nozzle_diameter->Set(nozzle_diameters);
    m_nozzle_diameter->SetSelection(0);
    comboBoxSizer->Add(m_nozzle_diameter, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(comboBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_bed_shape_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(parent, wxID_ANY, _L("Bed Shape"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *  bed_shape_sizer       = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_bed_shape_text = new wxStaticText(parent, wxID_ANY, _L("Rectangle"), wxDefaultPosition, wxDefaultSize);
    bed_shape_sizer->Add(static_bed_shape_text, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(bed_shape_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_bed_size_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(parent, wxID_ANY, _L("Printable Space"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *  length_sizer          = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_length_text = new wxStaticText(parent, wxID_ANY, _L("X"), wxDefaultPosition, wxDefaultSize);
    static_length_text->SetMinSize(ORIGIN_TEXT_SIZE);
    static_length_text->SetSize(ORIGIN_TEXT_SIZE);
    length_sizer->Add(static_length_text, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(length_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    wxBoxSizer *length_input_sizer      = new wxBoxSizer(wxVERTICAL);
    m_bed_size_x_input             = new TextInput(parent, "200", "mm", wxEmptyString, wxDefaultPosition, PRINTER_SPACE_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    wxTextValidator validator(wxFILTER_DIGITS);
    m_bed_size_x_input->GetTextCtrl()->SetValidator(validator);
    length_input_sizer->Add(m_bed_size_x_input, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(length_input_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    wxBoxSizer *  width_sizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_width_text = new wxStaticText(parent, wxID_ANY, _L("Y"), wxDefaultPosition, wxDefaultSize);
    static_width_text->SetMinSize(ORIGIN_TEXT_SIZE);
    static_width_text->SetSize(ORIGIN_TEXT_SIZE);
    width_sizer->Add(static_width_text, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(width_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    wxBoxSizer *width_input_sizer      = new wxBoxSizer(wxVERTICAL);
    m_bed_size_y_input            = new TextInput(parent, "200", "mm", wxEmptyString, wxDefaultPosition, PRINTER_SPACE_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_bed_size_y_input->GetTextCtrl()->SetValidator(validator);
    width_input_sizer->Add(m_bed_size_y_input, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(width_input_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    return horizontal_sizer;

}

wxBoxSizer *CreatePrinterPresetDialog::create_origin_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(parent, wxID_ANY, _L("Origin"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *  length_sizer       = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_origin_x_text = new wxStaticText(parent, wxID_ANY, _L("X"), wxDefaultPosition, wxDefaultSize);
    static_origin_x_text->SetMinSize(ORIGIN_TEXT_SIZE);
    static_origin_x_text->SetSize(ORIGIN_TEXT_SIZE);
    length_sizer->Add(static_origin_x_text, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(length_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    wxBoxSizer *length_input_sizer = new wxBoxSizer(wxVERTICAL);
    m_bed_origin_x_input           = new TextInput(parent, "0", "mm", wxEmptyString, wxDefaultPosition, PRINTER_SPACE_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    wxTextValidator validator(wxFILTER_DIGITS);
    m_bed_origin_x_input->GetTextCtrl()->SetValidator(validator);
    length_input_sizer->Add(m_bed_origin_x_input, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(length_input_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    wxBoxSizer *  width_sizer       = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_origin_y_text = new wxStaticText(parent, wxID_ANY, _L("Y"), wxDefaultPosition, wxDefaultSize);
    static_origin_y_text->SetMinSize(ORIGIN_TEXT_SIZE);
    static_origin_y_text->SetSize(ORIGIN_TEXT_SIZE);
    width_sizer->Add(static_origin_y_text, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(width_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxALIGN_CENTER_VERTICAL, FromDIP(10));
    wxBoxSizer *width_input_sizer = new wxBoxSizer(wxVERTICAL);
    m_bed_origin_y_input          = new TextInput(parent, "0", "mm", wxEmptyString, wxDefaultPosition, PRINTER_SPACE_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_bed_origin_y_input->GetTextCtrl()->SetValidator(validator);
    width_input_sizer->Add(m_bed_origin_y_input, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(width_input_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_hot_bed_stl_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(parent, wxID_ANY, _L("Hot Bed STL"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *hot_bed_stl_sizer = new wxBoxSizer(wxVERTICAL);

    StateColor flush_bg_col(std::pair<wxColour, int>(wxColour(219, 253, 231), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal));

    StateColor flush_bd_col(std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Pressed), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Normal));

    m_button_bed_stl = new Button(parent, _L("Upload"));
    m_button_bed_stl->Bind(wxEVT_BUTTON, ([this](wxCommandEvent &e) { load_model_stl(); }));
    m_button_bed_stl->SetFont(Label::Body_10);

    m_button_bed_stl->SetPaddingSize(wxSize(FromDIP(30), FromDIP(8)));
    m_button_bed_stl->SetFont(Label::Body_13);
    m_button_bed_stl->SetCornerRadius(FromDIP(8));
    m_button_bed_stl->SetBackgroundColor(flush_bg_col);
    m_button_bed_stl->SetBorderColor(flush_bd_col);
    hot_bed_stl_sizer->Add(m_button_bed_stl, 0, wxEXPAND | wxALL, 0);

    horizontal_sizer->Add(hot_bed_stl_sizer, 0, wxEXPAND | wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_hot_bed_svg_item(wxWindow *parent)
{ 
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(parent, wxID_ANY, _L("Hot Bed SVG"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *hot_bed_stl_sizer = new wxBoxSizer(wxVERTICAL);

    StateColor flush_bg_col(std::pair<wxColour, int>(wxColour(219, 253, 231), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal));

    StateColor flush_bd_col(std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Pressed), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Normal));

    m_button_bed_svg = new Button(parent, _L("Upload"));
    m_button_bed_svg->Bind(wxEVT_BUTTON, ([this](wxCommandEvent &e) { load_texture(); }));
    m_button_bed_svg->SetFont(Label::Body_10);

    m_button_bed_svg->SetPaddingSize(wxSize(FromDIP(30), FromDIP(8)));
    m_button_bed_svg->SetFont(Label::Body_13);
    m_button_bed_svg->SetCornerRadius(FromDIP(8));
    m_button_bed_svg->SetBackgroundColor(flush_bg_col);
    m_button_bed_svg->SetBorderColor(flush_bd_col);
    hot_bed_stl_sizer->Add(m_button_bed_svg, 0, wxEXPAND | wxALL, 0);

    horizontal_sizer->Add(hot_bed_stl_sizer, 0, wxEXPAND | wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_max_print_height_item(wxWindow *parent)
{
    wxBoxSizer *  horizontal_sizer  = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer      = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_type_text = new wxStaticText(parent, wxID_ANY, _L("Max print height"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_type_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *hight_input_sizer = new wxBoxSizer(wxVERTICAL);
    m_print_height_input          = new TextInput(parent, "200", "mm", wxEmptyString, wxDefaultPosition, PRINTER_SPACE_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    wxTextValidator validator(wxFILTER_DIGITS);
    m_print_height_input->GetTextCtrl()->SetValidator(validator);
    hight_input_sizer->Add(m_print_height_input, 0, wxEXPAND | wxLEFT, FromDIP(5));
    horizontal_sizer->Add(hight_input_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_page1_btns_item(wxWindow *parent)
{
    wxBoxSizer *bSizer_button = new wxBoxSizer(wxHORIZONTAL);
    bSizer_button->Add(0, 0, 1, wxEXPAND, 0);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_OK = new Button(parent, _L("OK"));
    m_button_OK->SetBackgroundColor(btn_bg_green);
    m_button_OK->SetBorderColor(*wxWHITE);
    m_button_OK->SetTextColor(wxColour(0xFFFFFE));
    m_button_OK->SetFont(Label::Body_12);
    m_button_OK->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_OK->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_OK->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_OK, 0, wxRIGHT, FromDIP(10));

    m_button_OK->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        if (!validate_input_valid()) return;
        data_init();
        show_page2();
        });

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_page1_cancel = new Button(parent, _L("Cancel"));
    m_button_page1_cancel->SetBackgroundColor(btn_bg_white);
    m_button_page1_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_page1_cancel->SetFont(Label::Body_12);
    m_button_page1_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_page1_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_page1_cancel->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_page1_cancel, 0, wxRIGHT, FromDIP(10));

    m_button_page1_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_CANCEL); });

    return bSizer_button;
}
static std::string last_directory = "";
void CreatePrinterPresetDialog::load_texture() {
    wxFileDialog       dialog(this, _L("Choose a file to import bed texture from (PNG/SVG):"), last_directory, "", file_wildcards(FT_TEX), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK)
        return;

    m_custom_texture = "";
    last_directory        = dialog.GetDirectory().ToUTF8().data();
    std::string file_name = dialog.GetPath().ToUTF8().data();
    if (!boost::algorithm::iends_with(file_name, ".png") && !boost::algorithm::iends_with(file_name, ".svg")) {
        show_error(this, _L("Invalid file format."));
        return;
    }
    m_custom_texture = file_name;
}

void CreatePrinterPresetDialog::load_model_stl()
{
    wxFileDialog dialog(this, _L("Choose an STL file to import bed model from:"), last_directory, "", file_wildcards(FT_STL), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK)
        return;

    m_custom_model = "";
    last_directory        = dialog.GetDirectory().ToUTF8().data();
    std::string file_name = dialog.GetPath().ToUTF8().data();
    if (!boost::algorithm::iends_with(file_name, ".stl")) {
        show_error(this, _L("Invalid file format."));
        return;
    }
    m_custom_model = file_name;
}

wxBoxSizer *CreatePrinterPresetDialog::create_radio_item(wxString title, wxWindow *parent, wxString tooltip, std::vector<std::pair<RadioBox *, wxString>> &radiobox_list)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);
    RadioBox *  radiobox         = new RadioBox(parent);
    horizontal_sizer->Add(radiobox, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(5));
    radiobox_list.push_back(std::make_pair(radiobox,title));
    int btn_idx = radiobox_list.size() - 1;
    radiobox->Bind(wxEVT_LEFT_DOWN, [this, &radiobox_list, btn_idx](wxMouseEvent &e) {
        select_curr_radiobox(radiobox_list, btn_idx);
    });

    wxStaticText *text = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->Bind(wxEVT_LEFT_DOWN, [this, &radiobox_list, btn_idx](wxMouseEvent &e) {
        select_curr_radiobox(radiobox_list, btn_idx);
    });
    horizontal_sizer->Add(text, 0, wxEXPAND | wxLEFT, 0);

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return horizontal_sizer;

}

void CreatePrinterPresetDialog::select_curr_radiobox(std::vector<std::pair<RadioBox *, wxString>> &radiobox_list, int btn_idx)
{
    int len = radiobox_list.size();
    for (int i = 0; i < len; ++i) {
        if (i == btn_idx) {
            radiobox_list[i].first->SetValue(true);
            wxString curr_selected_type = radiobox_list[i].second;
            if (curr_selected_type == m_create_presets_type[0]) {
                m_filament_preset_template_sizer->Clear(true);
                m_filament_preset.clear();
                m_process_preset_template_sizer->Clear(true);
                m_process_preset.clear();
            } else if (curr_selected_type == m_create_presets_type[1]) {
                if (into_u8(m_printer_model->GetLabel()) == _L("Select model")) {
                    m_filament_preset_template_sizer->Clear(true);
                    m_filament_preset.clear();
                    m_process_preset_template_sizer->Clear(true);
                    m_process_preset.clear();
                } else {
                    update_presets_list();
                }
            } else if (curr_selected_type == m_create_printer_type[0]) {
                m_select_printer->Hide();
                m_select_vendor->Show();
                m_select_model->Show();
                m_can_not_find_vendor_combox->Show();
                m_can_not_find_vendor_text->Show();
                if (m_can_not_find_vendor_combox->GetValue()) { 
                    m_custom_vendor_model->Show();
                }
            } else if (curr_selected_type == m_create_printer_type[1]) {
                m_select_vendor->Hide();
                m_select_model->Hide();
                m_can_not_find_vendor_combox->Hide();
                m_can_not_find_vendor_text->Hide();
                m_custom_vendor_model->Hide();
                m_select_printer->Show();
            }
            Layout();
            Fit();
            Refresh();
        } else {
            radiobox_list[i].first->SetValue(false);
        }
    }
}

void CreatePrinterPresetDialog::create_printer_page2(wxWindow *parent)
{
    this->SetBackgroundColour(*wxWHITE);

    wxBoxSizer* page2_sizer = new wxBoxSizer(wxVERTICAL);

    page2_sizer->Add(create_printer_preset_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page2_sizer->Add(create_presets_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page2_sizer->Add(create_presets_template_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    page2_sizer->Add(create_page2_btns_item(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));

    parent->SetSizer(page2_sizer);
    Layout();
    Fit();

    wxGetApp().UpdateDlgDarkUI(this);
}

wxBoxSizer *CreatePrinterPresetDialog::create_printer_preset_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_vendor_text = new wxStaticText(parent, wxID_ANY, _L("Printer preset"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_vendor_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *  vertical_sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *combobox_title  = new wxStaticText(parent, wxID_ANY, _L("Create based on current printer"), wxDefaultPosition, wxDefaultSize, 0);
    combobox_title->SetFont(::Label::Body_13);
    auto size = combobox_title->GetTextExtent(_L("Create based on current printer"));
    combobox_title->SetMinSize(wxSize(size.x + FromDIP(4), -1));
    combobox_title->Wrap(-1);
    vertical_sizer->Add(combobox_title, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer *comboBox_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_printer_vendor           = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, PRINTER_PRESET_VENDOR_SIZE, 0, nullptr, wxCB_READONLY);
    m_printer_vendor->SetValue(_L("Select vendor"));
    m_printer_vendor->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    comboBox_sizer->Add(m_printer_vendor, 0, wxEXPAND, 0);
    m_printer_model = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, PRINTER_PRESET_MODEL_SIZE, 0, nullptr, wxCB_READONLY);
    m_printer_model->SetLabelColor(DEFAULT_PROMPT_TEXT_COLOUR);
    m_printer_model->SetValue(_L("Select model"));

    comboBox_sizer->Add(m_printer_model, 0, wxEXPAND | wxLEFT, FromDIP(10));
    vertical_sizer->Add(comboBox_sizer, 0, wxEXPAND | wxTOP, FromDIP(5));

    horizontal_sizer->Add(vertical_sizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;

}

wxBoxSizer *CreatePrinterPresetDialog::create_presets_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_serial_text = new wxStaticText(parent, wxID_ANY, _L("Presets"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_serial_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *radioBoxSizer = new wxBoxSizer(wxVERTICAL);

    radioBoxSizer->Add(create_radio_item(m_create_presets_type[0], parent, wxEmptyString, m_create_presets_btns), 0, wxEXPAND | wxALL, 0);
    radioBoxSizer->Add(create_radio_item(m_create_presets_type[1], parent, wxEmptyString, m_create_presets_btns), 0, wxEXPAND | wxTOP, FromDIP(10));
    horizontal_sizer->Add(radioBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_presets_template_item(wxWindow *parent)
{
    wxBoxSizer *vertical_sizer = new wxBoxSizer(wxVERTICAL);

    m_preset_template_panel = new wxScrolledWindow(parent);
    m_preset_template_panel->SetSize(wxSize(-1, -1));
    m_preset_template_panel->SetBackgroundColour(PRINTER_LIST_COLOUR);
    //m_filament_preset_panel->SetMinSize(PRESET_TEMPLATE_SIZE);
    wxBoxSizer *  filament_sizer              = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_filament_preset_text = new wxStaticText(m_preset_template_panel, wxID_ANY, _L("Filament preset template"), wxDefaultPosition, wxDefaultSize);
    filament_sizer->Add(static_filament_preset_text, 0, wxEXPAND | wxALL, FromDIP(5));
    m_filament_preset_panel          = new wxPanel(m_preset_template_panel);
    m_filament_preset_template_sizer = new wxGridSizer(3, FromDIP(5), FromDIP(5));
    m_filament_preset_panel->SetSize(PRESET_TEMPLATE_SIZE);
    m_filament_preset_panel->SetSizer(m_filament_preset_template_sizer);
    filament_sizer->Add(m_filament_preset_panel, 0, wxEXPAND | wxALL, FromDIP(5));
    
    wxBoxSizer *hori_filament_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxPanel *   filament_btn_panel      = new wxPanel(m_preset_template_panel);
    filament_btn_panel->SetBackgroundColour(FILAMENT_OPTION_COLOUR);
    wxStaticText *filament_sel_all_text = new wxStaticText(filament_btn_panel, wxID_ANY, _L("Select All"), wxDefaultPosition, wxDefaultSize);
    filament_sel_all_text->SetForegroundColour(SELECT_ALL_OPTION_COLOUR);
    filament_sel_all_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { 
        select_all_preset_template(m_filament_preset); 
        e.Skip();
        });
    wxStaticText *filament_desel_all_text = new wxStaticText(filament_btn_panel, wxID_ANY, _L("Deselect All"), wxDefaultPosition, wxDefaultSize);
    filament_desel_all_text->SetForegroundColour(SELECT_ALL_OPTION_COLOUR);
    filament_desel_all_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        deselect_all_preset_template(m_filament_preset);
        e.Skip();
    });
    hori_filament_btn_sizer->Add(filament_sel_all_text, 0, wxEXPAND | wxALL, FromDIP(5));
    hori_filament_btn_sizer->Add(filament_desel_all_text, 0, wxEXPAND | wxALL, FromDIP(5));
    filament_btn_panel->SetSizer(hori_filament_btn_sizer);
    filament_sizer->Add(filament_btn_panel, 0, wxEXPAND, 0);
    
    wxPanel *split_panel = new wxPanel(m_preset_template_panel, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(10)));
    split_panel->SetBackgroundColour(wxColour(*wxWHITE));
    filament_sizer->Add(split_panel, 0, wxEXPAND, 0);

    wxStaticText *static_process_preset_text = new wxStaticText(m_preset_template_panel, wxID_ANY, _L("Process preset template"), wxDefaultPosition, wxDefaultSize);
    filament_sizer->Add(static_process_preset_text, 0, wxEXPAND | wxALL, FromDIP(5));
    m_process_preset_panel = new wxPanel(m_preset_template_panel);
    m_process_preset_panel->SetSize(PRESET_TEMPLATE_SIZE);
    m_process_preset_template_sizer = new wxGridSizer(3, FromDIP(5), FromDIP(5));
    m_process_preset_panel->SetSizer(m_process_preset_template_sizer);
    filament_sizer->Add(m_process_preset_panel, 0, wxEXPAND | wxALL, FromDIP(5));
    

    wxBoxSizer *hori_process_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxPanel *   process_btn_panel      = new wxPanel(m_preset_template_panel);
    process_btn_panel->SetBackgroundColour(FILAMENT_OPTION_COLOUR);
    wxStaticText *process_sel_all_text = new wxStaticText(process_btn_panel, wxID_ANY, _L("Select All"), wxDefaultPosition, wxDefaultSize);
    process_sel_all_text->SetForegroundColour(SELECT_ALL_OPTION_COLOUR);
    process_sel_all_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        select_all_preset_template(m_process_preset);
        e.Skip();
    });
    wxStaticText *process_desel_all_text = new wxStaticText(process_btn_panel, wxID_ANY, _L("Deselect All"), wxDefaultPosition, wxDefaultSize);
    process_desel_all_text->SetForegroundColour(SELECT_ALL_OPTION_COLOUR);
    process_desel_all_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        deselect_all_preset_template(m_process_preset);
        e.Skip();
    });
    hori_process_btn_sizer->Add(process_sel_all_text, 0, wxEXPAND | wxALL, FromDIP(5));
    hori_process_btn_sizer->Add(process_desel_all_text, 0, wxEXPAND | wxALL, FromDIP(5));
    process_btn_panel->SetSizer(hori_process_btn_sizer);
    filament_sizer->Add(process_btn_panel, 0, wxEXPAND, 0);

    m_preset_template_panel->SetSizer(filament_sizer);
    vertical_sizer->Add(m_preset_template_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return vertical_sizer;
}

wxBoxSizer *CreatePrinterPresetDialog::create_page2_btns_item(wxWindow *parent)
{
    wxBoxSizer *bSizer_button = new wxBoxSizer(wxHORIZONTAL);
    bSizer_button->Add(0, 0, 1, wxEXPAND, 0);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_page2_back = new Button(parent, _L("Back"));
    m_button_page2_back->SetBackgroundColor(btn_bg_white);
    m_button_page2_back->SetBorderColor(wxColour(38, 46, 48));
    m_button_page2_back->SetFont(Label::Body_12);
    m_button_page2_back->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_page2_back->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_page2_back->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_page2_back, 0, wxRIGHT, FromDIP(10));

    m_button_page2_back->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { show_page1(); });

    m_button_create = new Button(parent, _L("Create"));
    m_button_create->SetBackgroundColor(btn_bg_green);
    m_button_create->SetBorderColor(*wxWHITE);
    m_button_create->SetTextColor(wxColour(0xFFFFFE));
    m_button_create->SetFont(Label::Body_12);
    m_button_create->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_create->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_create->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_create, 0, wxRIGHT, FromDIP(10));

    m_button_create->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {

        PresetBundle *preset_bundle = wxGetApp().preset_bundle;
        wxString curr_selected_printer_type = curr_create_printer_type();
        wxString curr_selected_preset_type  = curr_create_preset_type();
        if (curr_selected_preset_type == m_create_presets_type[0]) {

        } else if (curr_selected_printer_type == m_create_printer_type[0] && curr_selected_preset_type == m_create_presets_type[1]) {//create printer and based on printer
            /******************************   clone printer preset     ********************************/
            //create preset name
            std::string printer_preset_name;
            std::string nozzle_diameter = into_u8(m_nozzle_diameter->GetStringSelection());
            if (m_can_not_find_vendor_combox->GetValue()) {
                std::string vendor_model = into_u8(m_custom_vendor_model->GetValue());
                if (vendor_model.empty()) {
                    MessageDialog dlg(this, _L("The custom printer and model are not inputed, place return page 1 to input."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                      wxYES | wxYES_DEFAULT | wxCENTRE);
                    dlg.ShowModal();
                    show_page1();
                    return;
                }
                printer_preset_name = vendor_model + " " + nozzle_diameter;
            } else {
                std::string vender_name  = into_u8(m_select_vendor->GetStringSelection());
                std::string model_name   = into_u8(m_select_model->GetStringSelection());
                printer_preset_name = vender_name + " " + model_name + " " + nozzle_diameter;
            }
            //Confirm if the printer preset exists
            if (!m_printer_preset) {
                MessageDialog dlg(this, _L("You have not yet chosen which printer preset to create based on. Please choose the vendor and model of the printer"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                  wxYES | wxYES_DEFAULT | wxCENTRE);
                dlg.ShowModal();
                return;
                show_page1();
            }
            // Confirm if the printer preset has a duplicate name
            if (!rewritten && preset_bundle->printers.find_preset(printer_preset_name)) {
                MessageDialog dlg(this,
                                  _L("The preset you created already has a preset with the same name. Do you want to overwrite it?\n\tYes: Overwrite the printer preset with the "
                                     "same name, and filament and process presets with the same preset name will not be recreated.\n\tCancel: Do not create a preset, return to the creation interface."),
                                  wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxCANCEL | wxYES_DEFAULT | wxCENTRE);
                int           res = dlg.ShowModal();
                if (res == wxID_YES) { 
                    rewritten = true;
                } else {
                    return;
                }
            }

            //Confirm if the filament preset is exist
            bool                        filament_preset_is_exist = false;
            std::vector<Preset const *> selected_filament_presets;
            for (std::pair<CheckBox *, Preset const *> filament_preset : m_filament_preset) {
                if (filament_preset.first->GetValue()) {
                    selected_filament_presets.push_back(filament_preset.second);
                }
                if (!filament_preset_is_exist && preset_bundle->filaments.find_preset(filament_preset.second->alias + " @ " + printer_preset_name) != nullptr) { 
                    filament_preset_is_exist = true;
                }
            }
            if (selected_filament_presets.empty() && !filament_preset_is_exist) {
                MessageDialog dlg(this, _L("You need to select at least one filling preset."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                  wxYES | wxYES_DEFAULT | wxCENTRE);
                dlg.ShowModal();
                return;
            }

            // Confirm if the process preset is exist
            bool                        process_preset_is_exist = false;
            std::vector<Preset const *> selected_process_presets;
            for (std::pair<CheckBox *, Preset const *> process_preset : m_process_preset) {
                if (process_preset.first->GetValue()) {
                    selected_process_presets.push_back(process_preset.second);
                }
                if (!process_preset_is_exist && preset_bundle->prints.find_preset(process_preset.second->alias + " @" + printer_preset_name) != nullptr) {
                    process_preset_is_exist = true;
                }
            }
            if (selected_process_presets.empty() && !process_preset_is_exist) {
                MessageDialog dlg(this, _L("You need to select at least one process preset."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                  wxYES | wxYES_DEFAULT | wxCENTRE);
                dlg.ShowModal();
                return;
            }

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":creater printer ";
            /******************************   clone filament preset    ********************************/
            std::vector<std::string> failures;
            if (!selected_filament_presets.empty()) {
                bool create_preset_result = preset_bundle->filaments.clone_presets_for_printer(selected_filament_presets, failures, printer_preset_name, rewritten);
                if (!create_preset_result) {
                    std::string message;
                    for (const std::string& failure : failures) {
                        message += "\t" + failure + "\n";
                    }
                    MessageDialog dlg(this, _L("Create filament presets failed. As follows:\n") + message, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                                      wxYES | wxYES_DEFAULT | wxCENTRE);
                    dlg.ShowModal();
                }
            }

            /******************************   clone process preset    ********************************/
            failures.clear();
            if (!selected_process_presets.empty()) {
                bool create_preset_result = preset_bundle->prints.clone_presets_for_printer(selected_process_presets, failures, printer_preset_name, rewritten);
                if (!create_preset_result) {
                    std::string message;
                    for (const std::string& failure : failures) {
                        message += "\t" + failure + "\n";
                    }
                    MessageDialog dlg(this, _L("Create process presets failed. As follows:\n") + message, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
                    dlg.ShowModal();
                }
            }
            save_printable_area_config(m_printer_preset);
            preset_bundle->printers.save_current_preset(printer_preset_name, true, false, m_printer_preset);
            preset_bundle->update_compatible(PresetSelectCompatibleType::Always);
        }
        EndModal(wxID_OK);
        });

    m_button_page2_cancel = new Button(parent, _L("Cancel"));
    m_button_page2_cancel->SetBackgroundColor(btn_bg_white);
    m_button_page2_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_page2_cancel->SetFont(Label::Body_12);
    m_button_page2_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_page2_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_page2_cancel->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_page2_cancel, 0, wxRIGHT, FromDIP(10));

    m_button_page2_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_CANCEL); });

    return bSizer_button;
}

void CreatePrinterPresetDialog::show_page1()
{
    m_step_1->SetBitmap(create_scaled_bitmap("step_1", nullptr, FromDIP(20)));
    m_step_2->SetBitmap(create_scaled_bitmap("step_2_ready", nullptr, FromDIP(20)));
    m_page1->Show();
    m_page2->Hide();
    Refresh();
    Layout();
    Fit();
}

void CreatePrinterPresetDialog::show_page2()
{
    m_step_1->SetBitmap(create_scaled_bitmap("step_is_ok", nullptr, FromDIP(20)));
    m_step_2->SetBitmap(create_scaled_bitmap("step_2", nullptr, FromDIP(20)));
    m_page2->Show();
    m_page1->Hide();
    Refresh();
    Layout();
    Fit();
}

bool CreatePrinterPresetDialog::data_init()
{ 
    std::string nozzle_type  = into_u8(m_nozzle_diameter->GetStringSelection());
    size_t      index_nozzle = nozzle_type.find(" nozzle");
    nozzle_type              = nozzle_type.substr(0, index_nozzle);
    float nozzle             = std::stof(nozzle_type);

    VendorMap vendors;
    wxArrayString exist_vendor_choice = get_exist_vendor_choices(vendors);
    m_printer_vendor->Set(exist_vendor_choice);

    m_printer_model->Bind(wxEVT_COMBOBOX, &CreatePrinterPresetDialog::on_preset_model_value_change, this);
    
    m_printer_vendor->Bind(wxEVT_COMBOBOX, [this, vendors, nozzle](wxCommandEvent e) {
        m_printer_vendor->SetLabelColor(*wxBLACK);

        std::string   curr_selected_vendor = into_u8(m_printer_vendor->GetStringSelection());
        auto          iterator             = vendors.find(curr_selected_vendor);
        if (iterator != vendors.end()) {
            m_printer_preset_vendor_selected = iterator->second;
        } else {
            MessageDialog dlg(this, _L("Vendor is not find, please reselect."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES_NO | wxYES_DEFAULT | wxCENTRE);
            dlg.ShowModal();
            return;
        }
        
        wxArrayString printer_preset_model = printer_preset_sort_with_nozzle_diameter(m_printer_preset_vendor_selected, nozzle);
        if (printer_preset_model.size() == 0) {
            MessageDialog dlg(this, _L("Current vendor has no models, please reselect."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
            dlg.ShowModal();
            return;
        }
        m_printer_model->Set(printer_preset_model);
        if (!printer_preset_model.empty()) { 
            m_printer_model->SetSelection(0);
            wxCommandEvent e;
            on_preset_model_value_change(e);
        }
        rewritten = false;
        e.Skip();
        
    });
    return true;

}

wxArrayString CreatePrinterPresetDialog::printer_preset_sort_with_nozzle_diameter(const VendorProfile &vendor_profile, float nozzle_diameter)
{
    std::vector<pair<float, std::string>> preset_sort;

    for (const Slic3r::VendorProfile::PrinterModel &model : vendor_profile.models) {
        std::string model_name = model.name;
        for (const Slic3r::VendorProfile::PrinterVariant &variant : model.variants) {
            try {
                float variant_diameter = std::stof(variant.name);
                preset_sort.push_back(std::make_pair(variant_diameter, model_name + " @ " + variant.name + " nozzle"));
            }
            catch (...) {
                continue;
            }
        }
    }

    std::sort(preset_sort.begin(), preset_sort.end(), [](const std::pair<float, std::string> &a, const std::pair<float, std::string> &b) { return a.first < b.first; });

    int index_nearest_nozzle = -1;
    float nozzle_diameter_diff = 1;
    for (int i = 0; i < preset_sort.size(); ++i) { 
        float curr_nozzle_diameter_diff = std::abs(nozzle_diameter - preset_sort[i].first);
        if (curr_nozzle_diameter_diff < nozzle_diameter_diff) { 
            index_nearest_nozzle = i;
            nozzle_diameter_diff = curr_nozzle_diameter_diff;
            if (curr_nozzle_diameter_diff == 0) break;
        }
    }
    wxArrayString printer_preset_model_selection;
    int right_index = index_nearest_nozzle + 1;
    while (index_nearest_nozzle >= 0 || right_index < preset_sort.size()) { 
        if (index_nearest_nozzle >= 0 && right_index < preset_sort.size()) {
            float left_nozzle_diff  = std::abs(nozzle_diameter - preset_sort[index_nearest_nozzle].first);
            float right_nozzle_diff = std::abs(nozzle_diameter - preset_sort[right_index].first);
            bool  left_is_little    = left_nozzle_diff < right_nozzle_diff;
            if (left_is_little) {
                printer_preset_model_selection.Add(preset_sort[index_nearest_nozzle].second);
                index_nearest_nozzle--;
            } else {
                printer_preset_model_selection.Add(preset_sort[right_index].second);
                right_index++;
            }
        } else if (index_nearest_nozzle >= 0) {
            printer_preset_model_selection.Add(preset_sort[index_nearest_nozzle].second);
            index_nearest_nozzle--;
        } else if (right_index < preset_sort.size()) {
            printer_preset_model_selection.Add(preset_sort[right_index].second);
            right_index++;
        }    
    }
    return printer_preset_model_selection;
}
/*
wxBoxSizer *CreatePrinterPresetDialog::create_checkbox(wxWindow *parent, Preset *preset, std::vector<std::pair<CheckBox *, Preset *>> &preset_checkbox)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    CheckBox* checkbox  = new CheckBox(parent);
    sizer->Add(checkbox, 0, 0, 0);
    preset_checkbox.push_back(std::make_pair(checkbox, preset));
    wxStaticText *preset_name = new wxStaticText(parent, wxID_ANY, preset->name);
    sizer->Add(preset_name, 0, wxLEFT, 5);
    return sizer;
}
*/
void CreatePrinterPresetDialog::select_all_preset_template(std::vector<std::pair<CheckBox *, Preset *>> &preset_templates)
{
    for (std::pair < CheckBox *, Preset const * > filament_preset : preset_templates) { 
        filament_preset.first->SetValue(true);
    }
}

void CreatePrinterPresetDialog::deselect_all_preset_template(std::vector<std::pair<CheckBox *, Preset *>> &preset_templates)
{
    for (std::pair<CheckBox *, Preset const *> filament_preset : preset_templates) { 
        filament_preset.first->SetValue(false); 
    }
}

void CreatePrinterPresetDialog::update_presets_list()
{
    std::string curr_selected_model = into_u8(m_printer_model->GetStringSelection());
    int         nozzle_index        = curr_selected_model.find_first_of("@");
    std::string select_model        = curr_selected_model.substr(0, nozzle_index - 1);
    for (const Slic3r::VendorProfile::PrinterModel &model : m_printer_preset_vendor_selected.models) {
        if (model.name == select_model) {
            m_printer_preset_model_selected = model;
            break;
        }
    }

    PresetBundle            temp_preset_bundle;
    std::string  preset_path;
    if (boost::filesystem::exists(boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR / m_printer_preset_vendor_selected.id)) { 
        preset_path = (boost::filesystem::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR).string();
    } else if (boost::filesystem::exists(boost::filesystem::path(Slic3r::resources_dir()) / "profiles" / m_printer_preset_vendor_selected.id)) {
        preset_path = (boost::filesystem::path(Slic3r::resources_dir()) / "profiles").string();
    }

    if (preset_path.empty()) {
        MessageDialog dlg(this, _L("Preset path is not find, please reselect vendor."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES_NO | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();
        return;
    }

    temp_preset_bundle.load_vendor_configs_from_json(preset_path, m_printer_preset_vendor_selected.id, PresetBundle::LoadConfigBundleAttribute::LoadSystem,
                                                     ForwardCompatibilitySubstitutionRule::EnableSilent);
    
    std::string dir_user_presets = wxGetApp().app_config->get("preset_folder");
    if (dir_user_presets.empty()) {
        temp_preset_bundle.load_user_presets(DEFAULT_USER_FOLDER_NAME, ForwardCompatibilitySubstitutionRule::EnableSilent);
    } else {
        temp_preset_bundle.load_user_presets(dir_user_presets, ForwardCompatibilitySubstitutionRule::EnableSilent);
    }

    std::string model_varient = into_u8(m_printer_model->GetStringSelection());
    size_t      index_at      = model_varient.find(" @ ");
    size_t      index_nozzle  = model_varient.find("nozzle");
    std::string varient;
    if (index_at != std::string::npos && index_nozzle != std::string::npos) {
        varient = model_varient.substr(index_at + 3, index_nozzle - index_at - 4);
    }
    else {
        MessageDialog dlg(this, _L("The nozzle_diameter is not fond, place reselect."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES_NO | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();
        return;
    }

    const Preset* temp_printer_preset = temp_preset_bundle.printers.find_system_preset_by_model_and_variant(m_printer_preset_model_selected.id, varient);

    if (temp_printer_preset) {
        temp_preset_bundle.printers.select_preset_by_name(temp_printer_preset->name, true);
        m_printer_preset = new Preset(*temp_printer_preset);
    }
    else {
        MessageDialog dlg(this, _L("The printer preset is not fond, place reselect."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES_NO | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();
        return;
    }

    temp_preset_bundle.update_compatible(PresetSelectCompatibleType::Always);

    const std::deque<Preset> &filament_presets = temp_preset_bundle.filaments.get_presets();
    const std::deque<Preset> &process_presets  = temp_preset_bundle.prints.get_presets();

    // clear filament preset window sizer
    clear_preset_combobox();

    // update filament preset window sizer
    for (const Preset &filament_preset : filament_presets) {
        if (filament_preset.is_compatible) {
            Preset *temp_filament = new Preset(filament_preset);
            m_filament_preset_template_sizer->Add(create_checkbox(m_filament_preset_panel, temp_filament, temp_filament->name, m_filament_preset), 0, wxEXPAND, FromDIP(5));
        }
    }

    for (const Preset &process_preset : process_presets) {
        if (process_preset.is_compatible) { 
            Preset *temp_process = new Preset(process_preset);
            m_process_preset_template_sizer->Add(create_checkbox(m_process_preset_panel, temp_process, temp_process->name, m_process_preset), 0, wxEXPAND, FromDIP(5));
        }
    }
}

void CreatePrinterPresetDialog::clear_preset_combobox()
{ 
    for (std::pair<CheckBox *, Preset *> preset : m_filament_preset) {
        if (preset.second) { 
            delete preset.second;
            preset.second = nullptr;
        }
    }
    m_filament_preset.clear();
    m_filament_preset_template_sizer->Clear(true);

    for (std::pair<CheckBox *, Preset *> preset : m_process_preset) {
        if (preset.second) {
            delete preset.second;
            preset.second = nullptr;
        }
    }
    m_process_preset.clear();
    m_process_preset_template_sizer->Clear(true);
}

bool CreatePrinterPresetDialog::save_printable_area_config(Preset *preset)
{
    DynamicPrintConfig &config = preset->config;

    double x = 0;
    m_bed_size_x_input->GetTextCtrl()->GetValue().ToDouble(&x);
    double y = 0;
    m_bed_size_y_input->GetTextCtrl()->GetValue().ToDouble(&y);
    double dx = 0;
    m_bed_origin_x_input->GetTextCtrl()->GetValue().ToDouble(&dx);
    double dy = 0;
    m_bed_origin_y_input->GetTextCtrl()->GetValue().ToDouble(&dy);
    //range check begin
    if (x == 0 || y == 0) {
        return false;
    }
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = x;
    double y1 = y;
    if (dx >= x || dy >= y) {
        return false;
    }
    x0 -= dx;
    x1 -= dx;
    y0 -= dy;
    y1 -= dy;
    // range check end
    std::vector<Vec2d> points = {Vec2d(x0, y0),
                                Vec2d(x1, y0),
                                Vec2d(x1, y1),
                                Vec2d(x0, y1)};
    config.set_key_value("printable_area", new ConfigOptionPoints(points));

    double max_print_height = 0;
    m_print_height_input->GetTextCtrl()->GetValue().ToDouble(&max_print_height);
    config.set("printable_height", max_print_height);
    std::regex  regex("\\\\");
    m_custom_model = std::regex_replace(m_custom_model, regex, "/");
    m_custom_texture = std::regex_replace(m_custom_texture, regex, "/");
    config.set("bed_custom_model", m_custom_model);
    config.set("bed_custom_texture", m_custom_texture);
    return true;
}

bool CreatePrinterPresetDialog::check_printable_area() {
    double x = 0;
    m_bed_size_x_input->GetTextCtrl()->GetValue().ToDouble(&x);
    double y = 0;
    m_bed_size_y_input->GetTextCtrl()->GetValue().ToDouble(&y);
    double dx = 0;
    m_bed_origin_x_input->GetTextCtrl()->GetValue().ToDouble(&dx);
    double dy = 0;
    m_bed_origin_y_input->GetTextCtrl()->GetValue().ToDouble(&dy);
    // range check begin
    if (x == 0 || y == 0) {
        return false;
    }
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = x;
    double y1 = y;
    if (dx >= x || dy >= y) {
        return false;
    }
    return true;
}

bool CreatePrinterPresetDialog::validate_input_valid()
{
    std::string vendor_name = into_u8(m_select_vendor->GetStringSelection());
    std::string model_name  = into_u8(m_select_model->GetStringSelection());
    std::string custom_vendor_model = into_u8(m_custom_vendor_model->GetValue());
    if ((vendor_name.empty() || model_name.empty()) && custom_vendor_model.empty()) {
        MessageDialog dlg(this, _L("You have not selected the vendor and model or inputed the custom vendor and model."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"),
                          wxYES | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();
        return false;
    }
    if (m_custom_texture.empty()) {
        MessageDialog dlg(this, _L("You have not upload bed texture."),
                          wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();
        return false;
    }
    if (m_custom_model.empty()) {
        MessageDialog dlg(this, _L("You have not upload bed model."),
                          wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();
        return false;
    }
    if (check_printable_area() == false) {
        MessageDialog dlg(this, _L("Please check bed shape input."), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        dlg.ShowModal();
        return false;
    }
    return true;
}

void CreatePrinterPresetDialog::on_preset_model_value_change(wxCommandEvent &e)
{
    m_printer_model->SetLabelColor(*wxBLACK);
    if (m_printer_preset_vendor_selected.models.empty()) return;

    wxString curr_selected_preset_type = curr_create_preset_type();
    if (curr_selected_preset_type == m_create_presets_type[1]) {
        update_presets_list();
    } else if (curr_selected_preset_type == m_create_presets_type[0]) {
        clear_preset_combobox();
    }
    rewritten = false;

    Layout();
    Fit();
    Refresh();
    e.Skip();
}

wxString CreatePrinterPresetDialog::curr_create_preset_type()
{
    wxString curr_selected_preset_type;
    for (const std::pair<RadioBox *, wxString> &presets_radio : m_create_presets_btns) {
        if (presets_radio.first->GetValue()) { 
            curr_selected_preset_type = presets_radio.second; 
        }
    }
    return curr_selected_preset_type;
}

wxString CreatePrinterPresetDialog::curr_create_printer_type()
{
    wxString curr_selected_printer_type;
    for (const std::pair<RadioBox *, wxString> &printer_radio : m_create_type_btns) {
        if (printer_radio.first->GetValue()) { curr_selected_printer_type = printer_radio.second; }
    }
    return curr_selected_printer_type;
}

CreatePresetSuccessfulDialog::CreatePresetSuccessfulDialog(wxWindow *parent, const SuccessType &create_success_type)
    : DPIDialog(parent ? parent : nullptr, wxID_ANY, _L("Create Printer/Filament Successful"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    this->SetBackgroundColour(*wxWHITE);
    this->SetSize(wxSize(FromDIP(450), FromDIP(200)));
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    wxBoxSizer *m_main_sizer = new wxBoxSizer(wxVERTICAL);
    // top line
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_main_sizer->Add(m_line_top, 0, wxEXPAND, 0);
    m_main_sizer->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);
    horizontal_sizer->Add(0, 0, 0, wxLEFT, FromDIP(30));

    wxBoxSizer *success_bitmap_sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticBitmap *success_bitmap       = new wxStaticBitmap(this,wxID_ANY, create_scaled_bitmap("create_success", nullptr, FromDIP(24)));
    success_bitmap_sizer->Add(success_bitmap, 0, wxEXPAND, 0);
    horizontal_sizer->Add(success_bitmap_sizer, 0, wxEXPAND | wxALL, FromDIP(5));

    wxBoxSizer *success_text_sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *success_text;
    wxStaticText *next_step_text;
    switch (create_success_type) {
    case PRINTER: 
        success_text = new wxStaticText(this, wxID_ANY, _L("Printer Created")); 
        next_step_text = new wxStaticText(this, wxID_ANY, _L("Please go to printer settings to edit your presets")); 
        break;
    case FILAMENT: 
        success_text = new wxStaticText(this, wxID_ANY, _L("Filament Created")); 
        next_step_text = new wxStaticText(this, wxID_ANY, _L("Please go to filament setting to edit your presets if you need")); 
        break;
    }
    success_text->SetFont(Label::Head_18);
    next_step_text->SetFont(Label::Body_16);
    success_text_sizer->Add(success_text, 0, wxEXPAND, 0);
    success_text_sizer->Add(next_step_text, 0, wxEXPAND | wxTOP, FromDIP(5));
    horizontal_sizer->Add(success_text_sizer, 0, wxEXPAND | wxALL, FromDIP(5));
    horizontal_sizer->Add(0, 0, 0, wxLEFT, FromDIP(60));

    m_main_sizer->Add(horizontal_sizer, 0, wxALL, FromDIP(5));

    wxBoxSizer *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->Add(0, 0, 1, wxEXPAND, 0);
    switch (create_success_type) {
    case PRINTER: 
        m_button_ok = new Button(this, _L("Printer Setting"));
        break;
    case FILAMENT:
        m_button_ok = new Button(this, _L("OK"));
        break;
    }
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(wxColour(*wxWHITE));
    m_button_ok->SetTextColor(wxColour(*wxWHITE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
    btn_sizer->Add(m_button_ok, 0, wxRIGHT, FromDIP(10));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_OK); });
    
    m_button_cancel = new Button(this, _L("Cancle"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetTextColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));
    btn_sizer->Add(m_button_cancel, 0, wxRIGHT, FromDIP(10));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_CANCEL); });

    m_main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(15));
    m_main_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));

    SetSizer(m_main_sizer);
    Layout();
    Fit();
    wxGetApp().UpdateDlgDarkUI(this);
}

CreatePresetSuccessfulDialog::~CreatePresetSuccessfulDialog() {}

void CreatePresetSuccessfulDialog::on_dpi_changed(const wxRect &suggested_rect) {}

ExportConfigsDialog::ExportConfigsDialog(wxWindow *parent)
    : DPIDialog(parent ? parent : nullptr, wxID_ANY, _L("Export Configs"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    m_exprot_type.preset_bundle   = _L("Printer config bundle(.bbscfg)");
    m_exprot_type.filament_bundle = _L("Filament bundle(.bbsflmt)");
    m_exprot_type.printer_preset  = _L("Printer presets(.json)");
    m_exprot_type.filament_preset = _L("Filament presets(.json)");
    m_exprot_type.process_preset  = _L("Process presets(.json)");

    this->SetBackgroundColour(*wxWHITE);
    this->SetSize(wxSize(FromDIP(600), FromDIP(600)));

    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    wxBoxSizer *m_main_sizer = new wxBoxSizer(wxVERTICAL);
    // top line
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_main_sizer->Add(m_line_top, 0, wxEXPAND, 0);
    m_main_sizer->Add(0, 0, 0, wxTOP, FromDIP(5));

    m_main_sizer->Add(create_txport_config_item(this), 0, wxEXPAND | wxALL, FromDIP(5));
    m_main_sizer->Add(create_select_printer(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    m_main_sizer->Add(create_button_item(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));

    data_init();

    this->SetSizer(m_main_sizer);

    this->Layout();
    this->Fit();

    wxGetApp().UpdateDlgDarkUI(this);

}

ExportConfigsDialog::~ExportConfigsDialog()
{
    for (std::pair<std::string, Preset *> printer_preset : m_printer_presets) {
        Preset *preset = printer_preset.second;
        if (preset) {
            delete preset;
            preset = nullptr;
        }
    }
    for (std::pair<std::string, std::vector<Preset *>> filament_presets : m_filament_presets) {
        for (Preset* preset : filament_presets.second) {
            if (preset) {
                delete preset;
                preset = nullptr;
            }
        }
    }
    for (std::pair<std::string, std::vector<Preset *>> filament_presets : m_process_presets) {
        for (Preset *preset : filament_presets.second) {
            if (preset) {
                delete preset;
                preset = nullptr;
            }
        }
    }
    for (std::pair<std::string, std::vector<std::pair<std::string, Preset *>>> filament_preset : m_filament_name_to_presets) {
        for (std::pair<std::string, Preset*> printer_name_preset : filament_preset.second) {
            Preset *preset = printer_name_preset.second;
            if (preset) {
                delete preset;
                preset = nullptr;
            }
        }
    }
}

void ExportConfigsDialog::on_dpi_changed(const wxRect &suggested_rect) {}

void ExportConfigsDialog::show_export_result(const ExportCase &export_case)
{
    MessageDialog *msg_dlg;
    switch (export_case) {
    case ExportCase::INITIALIZE_FAIL:
        msg_dlg = new MessageDialog(this, _L("initialize fail"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        break;
    case ExportCase::ADD_FILE_FAIL:
        msg_dlg = new MessageDialog(this, _L("add file fail"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        break;
    case ExportCase::ADD_BUNDLE_STRUCTURE_FAIL:
        msg_dlg = new MessageDialog(this, _L("add bundle structure file fail"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        break;
    case ExportCase::FINALIZE_FAIL:
        msg_dlg = new MessageDialog(this, _L("finalize fail"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        break;
    case ExportCase::OPEN_ZIP_WRITTEN_FILE:
        msg_dlg = new MessageDialog(this, _L("open zip written fail"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        break;
    case ExportCase::EXPORT_SUCCESS:
        msg_dlg = new MessageDialog(this, _L("Export successful"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES | wxYES_DEFAULT | wxCENTRE);
        break;
    }
    
    if (msg_dlg) {
        msg_dlg->ShowModal();
        delete msg_dlg;
        msg_dlg = nullptr;
    }
}

std::string ExportConfigsDialog::initial_file_path(const wxString &path, const std::string &sub_file_path)
{
    std::string             export_path         = into_u8(path);
    boost::filesystem::path printer_export_path = (boost::filesystem::path(export_path) / sub_file_path).make_preferred();
    if (boost::filesystem::exists(printer_export_path)) {
        MessageDialog dlg(this, wxString::Format(_L("The '%s' folder already exists in the current directory. Do you want to clear it and rebuild it.\nIf not, a time suffix will be "
                             "added, and you can modify the name after creation."), sub_file_path), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES_NO | wxYES_DEFAULT | wxCENTRE);
        int           res = dlg.ShowModal();
        if (wxID_YES == res) {
            boost::filesystem::remove_all(printer_export_path);
            boost::filesystem::create_directories(printer_export_path);
            export_path = printer_export_path.string();
        } else if (wxID_NO == res) {
            export_path = printer_export_path.string();
            std::string              export_path_with_time;
            boost::filesystem::path *printer_export_path_with_time = nullptr;
            do {
                if (printer_export_path_with_time) {
                    delete printer_export_path_with_time;
                    printer_export_path_with_time = nullptr;
                }
                export_path_with_time         = export_path + " " + get_curr_time();
                printer_export_path_with_time = new boost::filesystem::path(export_path_with_time);
            } while (boost::filesystem::exists(*printer_export_path_with_time));
            export_path = export_path_with_time;
            boost::filesystem::create_directories(*printer_export_path_with_time);
            if (printer_export_path_with_time) {
                delete printer_export_path_with_time;
                printer_export_path_with_time = nullptr;
            }
        } else {
            return "";
        }
    } else {
        boost::filesystem::create_directories(printer_export_path);
        export_path = printer_export_path.string();
    }
    return export_path;
}

std::string ExportConfigsDialog::initial_file_name(const wxString &path, const std::string file_name)
{
    std::string             export_path         = into_u8(path);
    boost::filesystem::path printer_export_path = (boost::filesystem::path(export_path) / file_name).make_preferred();
    if (boost::filesystem::exists(printer_export_path)) {
        MessageDialog dlg(this, wxString::Format(_L("The '%s' folder already exists in the current directory. Do you want to clear it and rebuild it.\nIf not, a time suffix will be "
                             "added, and you can modify the name after creation."), file_name), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxYES_NO | wxYES_DEFAULT | wxCENTRE);
        int           res = dlg.ShowModal();
        if (wxID_YES == res) {
            boost::filesystem::remove_all(printer_export_path);
            export_path = printer_export_path.string();
        } else if (wxID_NO == res) {
            export_path = printer_export_path.string();
            export_path = export_path.substr(0, export_path.find(".zip"));
            std::string              export_path_with_time;
            boost::filesystem::path *printer_export_path_with_time = nullptr;
            do {
                if (printer_export_path_with_time) {
                    delete printer_export_path_with_time;
                    printer_export_path_with_time = nullptr;
                }
                export_path_with_time         = export_path + " " + get_curr_time() + ".zip";
                printer_export_path_with_time = new boost::filesystem::path(export_path_with_time);
            } while (boost::filesystem::exists(*printer_export_path_with_time));
            export_path = export_path_with_time;
            if (printer_export_path_with_time) {
                delete printer_export_path_with_time;
                printer_export_path_with_time = nullptr;
            }
        } else {
            return "";
        }
    } else {
        export_path = printer_export_path.string();
    }
    return export_path;
}

wxBoxSizer *ExportConfigsDialog::create_txport_config_item(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    wxStaticText *static_serial_text = new wxStaticText(parent, wxID_ANY, _L("Presets"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(static_serial_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    wxBoxSizer *radioBoxSizer = new wxBoxSizer(wxVERTICAL);

    radioBoxSizer->Add(create_radio_item(m_exprot_type.preset_bundle, parent, wxEmptyString, m_export_type_btns), 0, wxEXPAND | wxALL, 0);
    radioBoxSizer->Add(0, 0, 0, wxTOP, FromDIP(6));
    wxStaticText *static_text = new wxStaticText(parent, wxID_ANY, _L("Printer and all the filament&process presets that belongs to the printer. \nCan be share in makerworld."), wxDefaultPosition, wxDefaultSize);
    static_text->SetFont(Label::Body_12);
    static_text->SetForegroundColour(wxColour("#6B6B6B"));
    radioBoxSizer->Add(static_text, 0, wxEXPAND | wxLEFT, FromDIP(22));
    radioBoxSizer->Add(create_radio_item(m_exprot_type.filament_bundle, parent, wxEmptyString, m_export_type_btns), 0, wxEXPAND | wxTOP, FromDIP(10));
    radioBoxSizer->Add(create_radio_item(m_exprot_type.printer_preset, parent, wxEmptyString, m_export_type_btns), 0, wxEXPAND | wxTOP, FromDIP(10));
    radioBoxSizer->Add(create_radio_item(m_exprot_type.filament_preset, parent, wxEmptyString, m_export_type_btns), 0, wxEXPAND | wxTOP, FromDIP(10));
    radioBoxSizer->Add(create_radio_item(m_exprot_type.process_preset, parent, wxEmptyString, m_export_type_btns), 0, wxEXPAND | wxTOP, FromDIP(10));
    horizontal_sizer->Add(radioBoxSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    return horizontal_sizer;
}

wxBoxSizer *ExportConfigsDialog::create_radio_item(wxString title, wxWindow *parent, wxString tooltip, std::vector<std::pair<RadioBox *, wxString>> &radiobox_list)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxHORIZONTAL);
    RadioBox *  radiobox         = new RadioBox(parent);
    horizontal_sizer->Add(radiobox, 0, wxEXPAND | wxALL, 0);
    horizontal_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(5));
    radiobox_list.push_back(std::make_pair(radiobox, title));
    int btn_idx = radiobox_list.size() - 1;
    radiobox->Bind(wxEVT_LEFT_DOWN, [this, &radiobox_list, btn_idx](wxMouseEvent &e) {
        select_curr_radiobox(radiobox_list, btn_idx);
        });

    wxStaticText *text = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->Bind(wxEVT_LEFT_DOWN, [this, &radiobox_list, btn_idx](wxMouseEvent &e) {
        select_curr_radiobox(radiobox_list, btn_idx);
        });
    horizontal_sizer->Add(text, 0, wxEXPAND | wxLEFT, 0);

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return horizontal_sizer;
}

mz_bool ExportConfigsDialog::initial_zip_archive(mz_zip_archive &zip_archive, const std::string &file_path)
{
    mz_zip_zero_struct(&zip_archive);
    mz_bool status;

    // Initialize the ZIP file to write to the structure, using memory storage

    std::string export_dir = encode_path(file_path.c_str());
    status                 = mz_zip_writer_init_file(&zip_archive, export_dir.c_str(), 0);
    return status;
}

ExportConfigsDialog::ExportCase ExportConfigsDialog::save_zip_archive_to_file(mz_zip_archive &zip_archive)
{
    // Complete writing of ZIP file
    mz_bool status = mz_zip_writer_finalize_archive(&zip_archive);
    if (MZ_FALSE == status) {
        BOOST_LOG_TRIVIAL(info) << "Failed to finalize ZIP archive";
        mz_zip_writer_end(&zip_archive);
        return ExportCase::FINALIZE_FAIL;
    }

    // Release ZIP file to write structure and related resources
    mz_zip_writer_end(&zip_archive);

    return ExportCase::CASE_COUNT;
}

ExportConfigsDialog::ExportCase ExportConfigsDialog::save_presets_to_zip(const std::string &export_file, const std::vector<std::pair<std::string, std::string>> &config_paths)
{
    mz_zip_archive zip_archive;
    mz_bool        status = initial_zip_archive(zip_archive, export_file);

    if (MZ_FALSE == status) {
        BOOST_LOG_TRIVIAL(info) << "Failed to initialize ZIP archive";
        return ExportCase::INITIALIZE_FAIL;
    }

    for (std::pair<std::string, std::string> config_path : config_paths) {
        std::string preset_name = config_path.first;

        // Add a file to the ZIP file
        status = mz_zip_writer_add_file(&zip_archive, (preset_name).c_str(), config_path.second.c_str(), NULL, 0, MZ_DEFAULT_COMPRESSION);
        // status = mz_zip_writer_add_mem(&zip_archive, ("printer/" + printer_preset->name + ".json").c_str(), json_contents, strlen(json_contents), MZ_DEFAULT_COMPRESSION);
        if (MZ_FALSE == status) {
            BOOST_LOG_TRIVIAL(info) << preset_name << " Filament preset failed to add file to ZIP archive";
            mz_zip_writer_end(&zip_archive);
            return ExportCase::ADD_FILE_FAIL;
        }
        BOOST_LOG_TRIVIAL(info) << "Printer preset json add successful: " << preset_name;
    }
    return save_zip_archive_to_file(zip_archive);
}

void ExportConfigsDialog::select_curr_radiobox(std::vector<std::pair<RadioBox *, wxString>> &radiobox_list, int btn_idx)
{
    int len = radiobox_list.size();
    for (int i = 0; i < len; ++i) {
        if (i == btn_idx) {
            radiobox_list[i].first->SetValue(true);
            const wxString &export_type = radiobox_list[i].second;
            m_preset_sizer->Clear(true);
            m_printer_name.clear();
            m_preset.clear();
            if (export_type == m_exprot_type.preset_bundle) {
                for (std::pair<std::string, Preset *> preset : m_printer_presets) {
                    m_preset_sizer->Add(create_checkbox(m_presets_window, preset.second, preset.first, m_preset), 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(5));
                }
                m_serial_text->SetLabel(_L("Only display printer names with changes to printer, filament, and process presets for uploading to the model mall."));
            }else if (export_type == m_exprot_type.filament_bundle) {
                for (std::pair<std::string, std::vector<std::pair<std::string, Preset*>>> filament_name_to_preset : m_filament_name_to_presets) {
                    m_preset_sizer->Add(create_checkbox(m_presets_window, filament_name_to_preset.first, m_printer_name), 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(5));
                }
                m_serial_text->SetLabel(_L("Only display the filament names with changes to filament presets for uploading to the model mall."));
            } else if (export_type == m_exprot_type.printer_preset) {
                for (std::pair<std::string, Preset *> preset : m_printer_presets) {
                    if (preset.second->is_system) continue;
                    m_preset_sizer->Add(create_checkbox(m_presets_window, preset.second, preset.first, m_preset), 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(5));
                }
                m_serial_text->SetLabel(_L("Only printer names with user printer presets will be displayed, and each preset you choose will be exported as a zip."));
            } else if (export_type == m_exprot_type.filament_preset) {
                for (std::pair<std::string, std::vector<std::pair<std::string, Preset *>>> filament_name_to_preset : m_filament_name_to_presets) {
                    m_preset_sizer->Add(create_checkbox(m_presets_window, filament_name_to_preset.first, m_printer_name), 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(5));
                }
                m_serial_text->SetLabel(_L("Only the filament names with user filament presets will be displayed, \nand all user filament presets in each filament name you select will be exported as a zip."));
            } else if (export_type == m_exprot_type.process_preset) {
                for (std::pair<std::string, std::vector<Preset *>> presets : m_process_presets) {
                    for (Preset *preset : presets.second) {
                        if (!preset->is_system) {
                            m_preset_sizer->Add(create_checkbox(m_presets_window, presets.first, m_printer_name), 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(5));
                            break;
                        }
                    }
                    
                }
                m_serial_text->SetLabel(_L("Only printer names with changed process presets will be displayed, \nand all user process presets in each printer name you select will be exported as a zip."));
            }
            Layout();
            Fit();
        } else {
            radiobox_list[i].first->SetValue(false);
        }
    }
}

ExportConfigsDialog::ExportCase ExportConfigsDialog::archive_preset_bundle_to_file(const wxString &path)
{
    std::string export_path = initial_file_path(path, "Printer config bundle");
    if (export_path.empty()) return ExportCase::EXPORT_CANCEL;
    BOOST_LOG_TRIVIAL(info) << "Export printer preset bundle";
    
    for (std::pair<CheckBox *, Preset *> checkbox_preset : m_preset) {
        if (checkbox_preset.first->GetValue()) {
            Preset *printer_preset = checkbox_preset.second;
            std::string printer_preset_name_ = printer_preset->name;

            json          bundle_structure;
            NetworkAgent *agent = wxGetApp().getAgent();
            std::string   clock = get_curr_timestmp();
            if (agent) {
                bundle_structure["user_name"] = agent->get_user_name();
                bundle_structure["user_id"]   = agent->get_user_id();
                bundle_structure["version"]   = agent->get_version();
                bundle_structure["bundle_id"] = agent->get_user_id() + "_" + printer_preset_name_ + "_" + clock;
            } else {
                bundle_structure["user_name"] = "";
                bundle_structure["user_id"]   = "";
                bundle_structure["version"]   = "";
                bundle_structure["bundle_id"] = "offline_" + printer_preset_name_ + "_" + clock;
            }
            bundle_structure["bundle_type"] = "printer config bundle";
            bundle_structure["printer_preset_name"] = printer_preset_name_;
            json printer_config   = json::array();
            json filament_configs = json::array();
            json process_configs  = json::array();

            mz_zip_archive zip_archive;
            mz_bool        status = initial_zip_archive(zip_archive, export_path + "/" + printer_preset->name + ".bbscfg");
            if (MZ_FALSE == status) {
                BOOST_LOG_TRIVIAL(info) << "Failed to initialize ZIP archive";
                return ExportCase::INITIALIZE_FAIL;
            }
            
            boost::filesystem::path pronter_file_path      = boost::filesystem::path(printer_preset->file);
            std::string             preset_path       = pronter_file_path.make_preferred().string();
            if (preset_path.empty()) {
                BOOST_LOG_TRIVIAL(info) << "Export printer preset: " << printer_preset->name << " skip because of the preset file path is empty.";
                continue;
            }

            // Add a file to the ZIP file
            std::string printer_config_file_name = "printer/" + pronter_file_path.filename().string();
            status = mz_zip_writer_add_file(&zip_archive, printer_config_file_name.c_str(), preset_path.c_str(), NULL, 0, MZ_DEFAULT_COMPRESSION);
            //status = mz_zip_writer_add_mem(&zip_archive, ("printer/" + printer_preset->name + ".json").c_str(), json_contents, strlen(json_contents), MZ_DEFAULT_COMPRESSION);
            if (MZ_FALSE == status) {
                BOOST_LOG_TRIVIAL(info) << printer_preset->name << " Failed to add file to ZIP archive";
                mz_zip_writer_end(&zip_archive);
                return ExportCase::ADD_FILE_FAIL;
            }
            printer_config.push_back(printer_config_file_name);
            BOOST_LOG_TRIVIAL(info) << "Printer preset json add successful: " << printer_preset->name;

            const std::string printer_preset_name = printer_preset->name;
            std::unordered_map<std::string, std::vector<Preset *>>::iterator iter = m_filament_presets.find(printer_preset_name);
            if (m_filament_presets.end() != iter) {
                for (Preset *preset : iter->second) {
                    boost::filesystem::path filament_file_path   = boost::filesystem::path(preset->file);
                    std::string             filament_preset_path = filament_file_path.make_preferred().string();
                    if (filament_preset_path.empty()) {
                        BOOST_LOG_TRIVIAL(info) << "Export filament preset: " << preset->name << " skip because of the preset file path is empty.";
                        continue;
                    }

                    std::string filament_config_file_name = "filament/" + filament_file_path.filename().string();
                    status = mz_zip_writer_add_file(&zip_archive, filament_config_file_name.c_str(), filament_preset_path.c_str(), NULL, 0, MZ_DEFAULT_COMPRESSION);
                    if (MZ_FALSE == status) {
                        BOOST_LOG_TRIVIAL(info) << preset->name << " Failed to add file to ZIP archive";
                        mz_zip_writer_end(&zip_archive);
                        return ExportCase::ADD_FILE_FAIL;
                    }
                    filament_configs.push_back(filament_config_file_name);
                    BOOST_LOG_TRIVIAL(info) << "Filament preset json add successful: ";
                }
            }

            iter = m_process_presets.find(printer_preset_name);
            if (m_process_presets.end() != iter) {
                for (Preset *preset : iter->second) {
                    boost::filesystem::path process_file_path   = boost::filesystem::path(preset->file);
                    std::string             process_preset_path = process_file_path.make_preferred().string();
                    if (process_preset_path.empty()) {
                        BOOST_LOG_TRIVIAL(info) << "Export process preset: " << preset->name << " skip because of the preset file path is empty.";
                        continue;
                    }

                    std::string process_config_file_name = "process/" + process_file_path.filename().string();
                    status = mz_zip_writer_add_file(&zip_archive, process_config_file_name.c_str(), process_preset_path.c_str(), NULL, 0, MZ_DEFAULT_COMPRESSION);
                    if (MZ_FALSE == status) {
                        BOOST_LOG_TRIVIAL(info) << preset->name << " Failed to add file to ZIP archive";
                        mz_zip_writer_end(&zip_archive);
                        return ExportCase::ADD_FILE_FAIL;
                    }
                    process_configs.push_back(process_config_file_name);
                    BOOST_LOG_TRIVIAL(info) << "Process preset json add successful: ";
                }
            }

            bundle_structure["printer_config"]  = printer_config;
            bundle_structure["filament_config"] = filament_configs;
            bundle_structure["process_config"]  = process_configs;

            std::string bundle_structure_str = bundle_structure.dump();
            status = mz_zip_writer_add_mem(&zip_archive, BUNDLE_STRUCTURE_JSON_NAME, bundle_structure_str.data(), bundle_structure_str.size(), MZ_DEFAULT_COMPRESSION);
            if (MZ_FALSE == status) {
                BOOST_LOG_TRIVIAL(info) << " Failed to add file: " << BUNDLE_STRUCTURE_JSON_NAME;
                mz_zip_writer_end(&zip_archive);
                return ExportCase::ADD_BUNDLE_STRUCTURE_FAIL;
            }
            BOOST_LOG_TRIVIAL(info) << " Success to add file: " << BUNDLE_STRUCTURE_JSON_NAME;

            ExportCase save_result = save_zip_archive_to_file(zip_archive);
            if (ExportCase::CASE_COUNT != save_result) return save_result;
        }
    }
    BOOST_LOG_TRIVIAL(info) << "ZIP archive created successfully";

    return ExportCase::EXPORT_SUCCESS;
}

ExportConfigsDialog::ExportCase ExportConfigsDialog::archive_filament_bundle_to_file(const wxString &path)
{
    std::string export_path = initial_file_path(path, "Filament bundle");
    if (export_path.empty()) return ExportCase::EXPORT_CANCEL;
    BOOST_LOG_TRIVIAL(info) << "Export filament preset bundle";

    for (std::pair<CheckBox *, std::string> checkbox_filament_name : m_printer_name) {
        if (checkbox_filament_name.first->GetValue()) {
            std::string filament_name = checkbox_filament_name.second;

            json          bundle_structure;
            NetworkAgent *agent = wxGetApp().getAgent();
            std::string   clock = get_curr_timestmp();
            if (agent) {
                bundle_structure["user_name"] = agent->get_user_name();
                bundle_structure["user_id"]   = agent->get_user_id();
                bundle_structure["version"]   = agent->get_version();
                bundle_structure["bundle_id"] = agent->get_user_id() + "_" + filament_name + "_" + clock;
            } else {
                bundle_structure["user_name"] = "";
                bundle_structure["user_id"]   = "";
                bundle_structure["version"]   = "";
                bundle_structure["bundle_id"] = "offline_" + filament_name + "_" + clock;
            }
            bundle_structure["bundle_type"] = "filament config bundle";
            bundle_structure["filament_name"] = filament_name;
            std::unordered_map<std::string, json> vendor_structure;

            mz_zip_archive zip_archive;
            mz_bool        status = initial_zip_archive(zip_archive, export_path + "/" + filament_name + ".bbsflmt");
            if (MZ_FALSE == status) {
                BOOST_LOG_TRIVIAL(info) << "Failed to initialize ZIP archive";
                return ExportCase::INITIALIZE_FAIL;
            }

            std::unordered_map<std::string, std::vector<std::pair<std::string, Preset *>>>::iterator iter = m_filament_name_to_presets.find(filament_name);
            if (m_filament_name_to_presets.end() == iter) {
                BOOST_LOG_TRIVIAL(info) << "Filament name do not find, filament name:" << filament_name;
                continue;
            }
            for (std::pair<std::string, Preset *> printer_name_to_preset : iter->second) {
                std::string printer_vendor = printer_name_to_preset.first;
                if (printer_vendor.empty()) continue;
                Preset *    filament_preset = printer_name_to_preset.second;
                std::string preset_path     = boost::filesystem::path(filament_preset->file).make_preferred().string();
                if (preset_path.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "Export printer preset: " << filament_preset->name << " skip because of the preset file path is empty.";
                    continue;
                }
                // Add a file to the ZIP file
                std::string file_name = printer_vendor + "/" + filament_preset->name + ".json";
                status                = mz_zip_writer_add_file(&zip_archive, file_name.c_str(), preset_path.c_str(), NULL, 0, MZ_DEFAULT_COMPRESSION);
                // status = mz_zip_writer_add_mem(&zip_archive, ("printer/" + printer_preset->name + ".json").c_str(), json_contents, strlen(json_contents), MZ_DEFAULT_COMPRESSION);
                if (MZ_FALSE == status) {
                    BOOST_LOG_TRIVIAL(info) << filament_preset->name << " Failed to add file to ZIP archive";
                    mz_zip_writer_end(&zip_archive);
                    return ExportCase::ADD_FILE_FAIL;
                }
                std::unordered_map<std::string, json>::iterator iter = vendor_structure.find(printer_vendor);
                if (vendor_structure.end() == iter) {
                    json j = json::array();
                    j.push_back(file_name);
                    vendor_structure[printer_vendor] = j;
                } else {
                    iter->second.push_back(file_name);
                }
                BOOST_LOG_TRIVIAL(info) << "Filament preset json add successful: " << filament_preset->name;
            }
            
            for (const std::pair<std::string, json>& vendor_name_to_json : vendor_structure) {
                json j;
                std::string printer_vendor = vendor_name_to_json.first;
                j["vendor"]                = printer_vendor;
                j["filament_path"]         = vendor_name_to_json.second;
                bundle_structure["printer_vendor"].push_back(j);
            }

            std::string bundle_structure_str = bundle_structure.dump();
            status = mz_zip_writer_add_mem(&zip_archive, BUNDLE_STRUCTURE_JSON_NAME, bundle_structure_str.data(), bundle_structure_str.size(), MZ_DEFAULT_COMPRESSION);
            if (MZ_FALSE == status) {
                BOOST_LOG_TRIVIAL(info) << " Failed to add file: " << BUNDLE_STRUCTURE_JSON_NAME;
                mz_zip_writer_end(&zip_archive);
                return ExportCase::ADD_BUNDLE_STRUCTURE_FAIL;
            }
            BOOST_LOG_TRIVIAL(info) << " Success to add file: " << BUNDLE_STRUCTURE_JSON_NAME;

            // Complete writing of ZIP file
            ExportCase save_result = save_zip_archive_to_file(zip_archive);
            if (ExportCase::CASE_COUNT != save_result) return save_result;
        }
    }
    BOOST_LOG_TRIVIAL(info) << "ZIP archive created successfully";

    return ExportCase::EXPORT_SUCCESS;
}

ExportConfigsDialog::ExportCase ExportConfigsDialog::archive_printer_preset_to_file(const wxString &path)
{
    std::string export_file = "Printer presets.zip";
    export_file             = initial_file_name(path, export_file);
    if (export_file.empty()) return ExportCase::EXPORT_CANCEL;

    std::vector<std::pair<std::string, std::string>> config_paths;

    for (std::pair<CheckBox *, Preset *> checkbox_preset : m_preset) {
        if (checkbox_preset.first->GetValue()) {
            Preset *    printer_preset = checkbox_preset.second;
            std::string preset_path    = boost::filesystem::path(printer_preset->file).make_preferred().string();
            if (preset_path.empty()) {
                BOOST_LOG_TRIVIAL(info) << "Export printer preset: " << printer_preset->name << " skip because of the preset file path is empty.";
                continue;
            }
            std::string preset_name = printer_preset->name + ".json";
            config_paths.push_back(std::make_pair(preset_name, preset_path));
        }
    }

    ExportCase save_result = save_presets_to_zip(export_file, config_paths);
    if (ExportCase::CASE_COUNT != save_result) return save_result;

    BOOST_LOG_TRIVIAL(info) << "ZIP archive created successfully";

    return ExportCase::EXPORT_SUCCESS;

}

ExportConfigsDialog::ExportCase ExportConfigsDialog::archive_filament_preset_to_file(const wxString &path)
{
    std::string export_file = "Filament presets.zip";
    export_file             = initial_file_name(path, export_file);
    if (export_file.empty()) return ExportCase::EXPORT_CANCEL;

    std::vector<std::pair<std::string, std::string>> config_paths;

    for (std::pair<CheckBox *, std::string> checkbox_preset : m_printer_name) {
        if (checkbox_preset.first->GetValue()) {
            std::string filament_name = checkbox_preset.second;

            std::unordered_map<std::string, std::vector<std::pair<std::string, Preset *>>>::iterator iter = m_filament_name_to_presets.find(filament_name);
            if (m_filament_name_to_presets.end() == iter) {
                BOOST_LOG_TRIVIAL(info) << "Filament name do not find, filament name:" << filament_name;
                continue;
            }
            for (std::pair<std::string, Preset*> printer_name_preset : iter->second) {
                Preset *    filament_preset = printer_name_preset.second;
                std::string preset_path     = boost::filesystem::path(filament_preset->file).make_preferred().string();
                if (preset_path.empty()) {
                    BOOST_LOG_TRIVIAL(info) << "Export filament preset: " << filament_preset->name << " skip because of the filament file path is empty.";
                    continue;
                }

                std::string preset_name = filament_preset->name + ".json";
                config_paths.push_back(std::make_pair(preset_name, preset_path));
            }
        }
    }

    ExportCase save_result = save_presets_to_zip(export_file, config_paths);
    if (ExportCase::CASE_COUNT != save_result) return save_result;

    BOOST_LOG_TRIVIAL(info) << "ZIP archive created successfully";

    return ExportCase::EXPORT_SUCCESS;
}

ExportConfigsDialog::ExportCase ExportConfigsDialog::archive_process_preset_to_file(const wxString &path)
{
    std::string export_file = "Process presets.zip";
    export_file             = initial_file_name(path, export_file);
    if (export_file.empty()) return ExportCase::EXPORT_CANCEL;

    std::vector<std::pair<std::string, std::string>> config_paths;

    for (std::pair<CheckBox *, std::string> checkbox_preset : m_printer_name) {
        if (checkbox_preset.first->GetValue()) {
            std::string printer_name = checkbox_preset.second;
            std::unordered_map<std::string, std::vector<Preset *>>::iterator iter = m_process_presets.find(printer_name);
            if (m_process_presets.end() != iter) {
                for (Preset *process_preset : iter->second) {
                    std::string preset_path = boost::filesystem::path(process_preset->file).make_preferred().string();
                    if (preset_path.empty()) {
                        BOOST_LOG_TRIVIAL(info) << "Export process preset: " << process_preset->name << " skip because of the preset file path is empty.";
                        continue;
                    }
                    
                    std::string preset_name = process_preset->name + ".json";
                    config_paths.push_back(std::make_pair(preset_name, preset_path));
                }
            }
        }
    }

    ExportCase save_result = save_presets_to_zip(export_file, config_paths);
    if (ExportCase::CASE_COUNT != save_result) return save_result;

    BOOST_LOG_TRIVIAL(info) << "ZIP archive created successfully";

    return ExportCase::EXPORT_SUCCESS;
}

wxBoxSizer *ExportConfigsDialog::create_button_item(wxWindow* parent)
{
    wxBoxSizer *bSizer_button = new wxBoxSizer(wxHORIZONTAL);
    bSizer_button->Add(0, 0, 1, wxEXPAND, 0);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_ok, 0, wxRIGHT, FromDIP(10));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        wxDirDialog dlg(this, _L("Choose a directory"), from_u8(wxGetApp().app_config->get_last_dir()), wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        wxString    path;
        if (dlg.ShowModal() == wxID_OK) path = dlg.GetPath();
        ExportCase export_case = ExportCase::EXPORT_CANCEL;
        if (!path.IsEmpty()) {
            wxGetApp().app_config->update_config_dir(into_u8(path));
            const wxString curr_radio_type = get_curr_radio_type(m_export_type_btns);

            if (curr_radio_type == m_exprot_type.preset_bundle) {
                export_case = archive_preset_bundle_to_file(path);
            } else if (curr_radio_type == m_exprot_type.filament_bundle) {
                export_case = archive_filament_bundle_to_file(path);
            } else if (curr_radio_type == m_exprot_type.printer_preset) {
                export_case = archive_printer_preset_to_file(path);
            } else if (curr_radio_type == m_exprot_type.filament_preset) {
                export_case = archive_filament_preset_to_file(path);
            } else if (curr_radio_type == m_exprot_type.process_preset) {
                export_case = archive_process_preset_to_file(path);
            }
        } else {
            return;
        }
        show_export_result(export_case);
        if (ExportCase::EXPORT_SUCCESS != export_case) return;
        
        EndModal(wxID_OK);
        });

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_cancel, 0, wxRIGHT, FromDIP(10));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_CANCEL); });

    return bSizer_button;
}

wxBoxSizer *ExportConfigsDialog::create_select_printer(wxWindow *parent)
{
    wxBoxSizer *horizontal_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *  optionSizer        = new wxBoxSizer(wxVERTICAL);
    m_serial_text           = new wxStaticText(parent, wxID_ANY, _L("Please select a type you want to export"), wxDefaultPosition, wxDefaultSize);
    optionSizer->Add(m_serial_text, 0, wxEXPAND | wxALL, 0);
    optionSizer->SetMinSize(OPTION_SIZE);
    horizontal_sizer->Add(optionSizer, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    m_presets_window = new wxWindow(parent, wxID_ANY);
    m_presets_window->SetBackgroundColour(PRINTER_LIST_COLOUR);
    wxBoxSizer *select_printer_sizer  = new wxBoxSizer(wxVERTICAL);

    m_preset_sizer = new wxGridSizer(3, FromDIP(5), FromDIP(5));
    select_printer_sizer->Add(m_preset_sizer, 0, wxEXPAND, FromDIP(5));
    m_presets_window->SetSizer(select_printer_sizer);

    horizontal_sizer->Add(m_presets_window, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

    return horizontal_sizer;
}

void ExportConfigsDialog::data_init()
{
    PresetBundle preset_bundle(*wxGetApp().preset_bundle);

    const std::deque<Preset> & printer_presets = preset_bundle.printers.get_presets();
    for (const Preset &printer_preset : printer_presets) {
        
        std::string preset_name        = wxString::FromUTF8(printer_preset.name).ToStdString();
        if (!printer_preset.is_visible || "Default Printer" == preset_name) continue;
        if (preset_bundle.printers.select_preset_by_name(preset_name, false)) {
            preset_bundle.update_compatible(PresetSelectCompatibleType::Always);

            bool has_user_preset = false;
            const std::deque<Preset> &filament_presets = preset_bundle.filaments.get_presets();
            for (const Preset &filament_preset : filament_presets) {
                if (filament_preset.is_system || "Default Filament" == filament_preset.name) continue;
                if (filament_preset.is_compatible) {
                    Preset *new_filament_preset = new Preset(filament_preset);
                    m_filament_presets[preset_name].push_back(new_filament_preset);
                    has_user_preset = true;
                }
            }

            const std::deque<Preset> &process_presets = preset_bundle.prints.get_presets();
            for (const Preset &process_preset : process_presets) {
                if (process_preset.is_system || "Default Setting" == process_preset.name) continue;
                if (process_preset.is_compatible) {
                    Preset *new_prpcess_preset = new Preset(process_preset);
                    m_process_presets[preset_name].push_back(new_prpcess_preset);
                    has_user_preset = true;
                }
            }
            if (has_user_preset) {
                Preset *new_printer_preset     = new Preset(printer_preset);
                m_printer_presets[preset_name] = new_printer_preset;
            }
        }
    }
    const std::deque<Preset> &filament_presets = preset_bundle.filaments.get_presets();
    for (const Preset &filament_preset : filament_presets) {
        if (filament_preset.is_system || "Default Filament" == filament_preset.name) continue;
        Preset *new_filament_preset = new Preset(filament_preset);
        std::string filament_preset_name = filament_preset.name;
        std::string machine_name         = get_machine_name(filament_preset_name);
        m_filament_name_to_presets[get_filament_name(filament_preset_name)].push_back(std::make_pair(get_vendor_name(machine_name), new_filament_preset));
    }
}

}}  //Slic3r