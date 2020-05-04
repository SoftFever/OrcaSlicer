#include "Search.hpp"

#include <cstddef>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/nowide/convert.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "GUI_App.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

#include "imgui/imconfig.h"

using boost::optional;

namespace Slic3r {

using GUI::from_u8;
using GUI::into_u8;

namespace Search {

static const std::vector<std::wstring>& NameByType()
{
    static std::vector<std::wstring> data;
    if (data.empty()) {
        data.assign(Preset::TYPE_COUNT, std::wstring());
        data[Preset::TYPE_PRINT         ] = _L("Print"      ).ToStdWstring();
        data[Preset::TYPE_FILAMENT      ] = _L("Filament"   ).ToStdWstring();
        data[Preset::TYPE_SLA_MATERIAL  ] = _L("Material"   ).ToStdWstring();
        data[Preset::TYPE_SLA_PRINT     ] = _L("Print"      ).ToStdWstring();
        data[Preset::TYPE_PRINTER       ] = _L("Printer"    ).ToStdWstring();
	};
	return data;
}

void FoundOption::get_marked_label_and_tooltip(const char** label_, const char** tooltip_) const
{
    *label_   = marked_label.c_str();
    *tooltip_ = tooltip.c_str();
}

template<class T>
//void change_opt_key(std::string& opt_key, DynamicPrintConfig* config)
void change_opt_key(std::string& opt_key, DynamicPrintConfig* config, int& cnt)
{
    T* opt_cur = static_cast<T*>(config->option(opt_key));
    cnt = opt_cur->values.size();
    return;

    if (opt_cur->values.size() > 0)
        opt_key += "#" + std::to_string(0);
}

void OptionsSearcher::append_options(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode)
{
    auto emplace = [this, type](const std::string opt_key, const wxString& label)
    {
        const GroupAndCategory& gc = groups_and_categories[opt_key];
        if (gc.group.IsEmpty() || gc.category.IsEmpty())
            return;

        wxString suffix;
        wxString suffix_local;
        if (gc.category == "Machine limits") {
            suffix = opt_key.back()=='1' ? L("Stealth") : L("Normal");
            suffix_local = " " + _(suffix);
            suffix = " " + suffix;
        }

        if (!label.IsEmpty())
            options.emplace_back(Option{ boost::nowide::widen(opt_key), type,
                                        (label + suffix).ToStdWstring(), (_(label) + suffix_local).ToStdWstring(),
                                        gc.group.ToStdWstring(), _(gc.group).ToStdWstring(),
                                        gc.category.ToStdWstring(), _(gc.category).ToStdWstring() });
    };

    for (std::string opt_key : config->keys())
    {
        const ConfigOptionDef& opt = config->def()->options.at(opt_key);
        if (opt.mode > mode)
            continue;

        int cnt = 0;

        if ( (type == Preset::TYPE_SLA_MATERIAL || type == Preset::TYPE_PRINTER) && opt_key != "bed_shape")
            switch (config->option(opt_key)->type())
            {
            case coInts:	change_opt_key<ConfigOptionInts		>(opt_key, config, cnt);	break;
            case coBools:	change_opt_key<ConfigOptionBools	>(opt_key, config, cnt);	break;
            case coFloats:	change_opt_key<ConfigOptionFloats	>(opt_key, config, cnt);	break;
            case coStrings:	change_opt_key<ConfigOptionStrings	>(opt_key, config, cnt);	break;
            case coPercents:change_opt_key<ConfigOptionPercents	>(opt_key, config, cnt);	break;
            case coPoints:	change_opt_key<ConfigOptionPoints	>(opt_key, config, cnt);	break;
            default:		break;
            }

        wxString label = opt.full_label.empty() ? opt.label : opt.full_label;

        if (cnt == 0)
            emplace(opt_key, label);
        else
            for (int i = 0; i < cnt; ++i)
                emplace(opt_key + "[" + std::to_string(i) + "]", label);

        /*const GroupAndCategory& gc = groups_and_categories[opt_key];
        if (gc.group.IsEmpty() || gc.category.IsEmpty())
            continue;

        if (!label.IsEmpty())
            options.emplace_back(Option{opt_key, type,
                                        label, _(label),
                                        gc.group, _(gc.group),
                                        gc.category, _(gc.category) });*/
    }
}

// Wrap a string with ColorMarkerStart and ColorMarkerEnd symbols
static wxString wrap_string(const wxString& str)
{
    return wxString::Format("%c%s%c", ImGui::ColorMarkerStart, str, ImGui::ColorMarkerEnd);
}

// Mark a string using ColorMarkerStart and ColorMarkerEnd symbols
static std::wstring mark_string(const std::wstring &str, const std::vector<uint16_t> &matches)
{
	std::wstring out;
	if (matches.empty())
		out = str;
	else {
		out.reserve(str.size() * 2);
		if (matches.front() > 0)
			out += str.substr(0, matches.front());
		for (size_t i = 0;;) {
			// Find the longest string of successive indices.
			size_t j = i + 1;
            while (j < matches.size() && matches[j] == matches[j - 1] + 1)
                ++ j;
            out += ImGui::ColorMarkerStart;
            out += str.substr(matches[i], matches[j - 1] - matches[i] + 1);
            out += ImGui::ColorMarkerEnd;
            if (j == matches.size()) {
				out += str.substr(matches[j - 1] + 1);
				break;
			}
            out += str.substr(matches[j - 1] + 1, matches[j] - matches[j - 1] - 1);
            i = j;
		}
	}
	return out;
}

bool OptionsSearcher::search()
{
    return search(search_line, true);
}

static bool fuzzy_match(const std::wstring &search_pattern, const std::wstring &label, int& out_score, std::vector<uint16_t> &out_matches)
{
    uint16_t matches[fts::max_matches + 1]; // +1 for the stopper
    int score;
    if (fts::fuzzy_match(search_pattern.c_str(), label.c_str(), score, matches)) {
	    size_t cnt = 0;
	    for (; matches[cnt] != fts::stopper; ++cnt);
	    out_matches.assign(matches, matches + cnt);
		out_score = score;
		return true;
	} else
		return false;
}

bool OptionsSearcher::search(const std::string& search, bool force/* = false*/)
{
    if (search_line == search && !force)
        return false;

    found.clear();

    bool full_list = search.empty();
    std::wstring sep = L" : ";
    const std::vector<std::wstring>& name_by_type = NameByType();

    auto get_label = [this, &name_by_type, &sep](const Option& opt)
    {
        std::wstring out;
    	const std::wstring *prev = nullptr;
    	for (const std::wstring * const s : {
	        view_params.type 		? &(name_by_type[opt.type]) : nullptr,
	        view_params.category 	? &opt.category_local 		: nullptr,
	        view_params.group 		? &opt.group_local			: nullptr,
	        &opt.label_local })
    		if (s != nullptr && (prev == nullptr || *prev != *s)) {
    			if (! out.empty())
    				out += sep;
    			out += *s;
    			prev = s;
    		}
        return out;
    };

    auto get_label_english = [this, &name_by_type, &sep](const Option& opt)
    {
        std::wstring out;
    	const std::wstring*prev = nullptr;
    	for (const std::wstring * const s : {
	        view_params.type 		? &name_by_type[opt.type] 	: nullptr,
	        view_params.category 	? &opt.category 			: nullptr,
	        view_params.group 		? &opt.group				: nullptr,
	        &opt.label })
    		if (s != nullptr && (prev == nullptr || *prev != *s)) {
    			if (! out.empty())
    				out += sep;
    			out += *s;
    			prev = s;
    		}
        return out;
    };

    auto get_tooltip = [this, &name_by_type, &sep](const Option& opt)
    {
        return  name_by_type[opt.type] + sep +
                opt.category_local + sep +
                opt.group_local + sep + opt.label_local;
    };

    std::vector<uint16_t> matches, matches2;
    for (size_t i=0; i < options.size(); i++)
    {
        const Option &opt = options[i];
        if (full_list) {
            std::string label = into_u8(get_label(opt));
            found.emplace_back(FoundOption{ label, label, boost::nowide::narrow(get_tooltip(opt)), i, 0 });
            continue;
        }

        std::wstring wsearch       = boost::nowide::widen(search);
        boost::trim_left(wsearch);
        std::wstring label         = get_label(opt);
        std::wstring label_english = get_label_english(opt);
        int score = std::numeric_limits<int>::min();
        int score2;
        matches.clear();
        fuzzy_match(wsearch, label, score, matches);
        if (fuzzy_match(wsearch, opt.opt_key, score2, matches2) && score2 > score) {
        	for (fts::pos_type &pos : matches2)
        		pos += label.size() + 1;
        	label += L"(" + opt.opt_key + L")";
        	append(matches, matches2);
        	score = score2;
        }
        if (fuzzy_match(wsearch, label_english, score2, matches2) && score2 > score) {
        	label   = std::move(label_english);
        	matches = std::move(matches2);
        	score   = score2;
        }
        if (score > std::numeric_limits<int>::min()) {
		    label = mark_string(label, matches);
	        std::string label_u8 = into_u8(label);
	        std::string label_plain = label_u8;
	        boost::erase_all(label_plain, std::string(1, char(ImGui::ColorMarkerStart)));
	        boost::erase_all(label_plain, std::string(1, char(ImGui::ColorMarkerEnd)));
            found.emplace_back(FoundOption{ label_plain, label_u8, boost::nowide::narrow(get_tooltip(opt)), i, score });
        }
    }

    if (!full_list)
        sort_found();
 
    if (search_line != search)
        search_line = search;

    return true;
}

OptionsSearcher::OptionsSearcher()
{
    search_dialog = new SearchDialog(this);
}

OptionsSearcher::~OptionsSearcher()
{
    if (search_dialog)
        search_dialog->Destroy();
}

void OptionsSearcher::init(std::vector<InputInfo> input_values)
{
    options.clear();
    for (auto i : input_values)
        append_options(i.config, i.type, i.mode);
    sort_options();

    search(search_line, true);
}

void OptionsSearcher::apply(DynamicPrintConfig* config, Preset::Type type, ConfigOptionMode mode)
{
    if (options.empty())
        return;

    options.erase(std::remove_if(options.begin(), options.end(), [type](Option opt) {
            return opt.type == type;
        }), options.end());

    append_options(config, type, mode);

    sort_options();

    search(search_line, true);
}

const Option& OptionsSearcher::get_option(size_t pos_in_filter) const
{
    assert(pos_in_filter != size_t(-1) && found[pos_in_filter].option_idx != size_t(-1));
    return options[found[pos_in_filter].option_idx];
}

void OptionsSearcher::add_key(const std::string& opt_key, const wxString& group, const wxString& category)
{
    groups_and_categories[opt_key] = GroupAndCategory{group, category};
}


//------------------------------------------
//          SearchComboPopup
//------------------------------------------


void SearchComboPopup::Init()
{
    this->Bind(wxEVT_MOTION,    &SearchComboPopup::OnMouseMove,     this);
    this->Bind(wxEVT_LEFT_UP,   &SearchComboPopup::OnMouseClick,    this);
    this->Bind(wxEVT_KEY_DOWN,  &SearchComboPopup::OnKeyDown,       this);
}

bool SearchComboPopup::Create(wxWindow* parent)
{
    return wxListBox::Create(parent, 1, wxPoint(0, 0), wxDefaultSize);
}

void SearchComboPopup::SetStringValue(const wxString& s)
{
    int n = wxListBox::FindString(s);
    if (n >= 0 && n < int(wxListBox::GetCount()))
        wxListBox::Select(n);

    // save a combo control's string
    m_input_string = s;
}

void SearchComboPopup::ProcessSelection(int selection) 
{
    wxCommandEvent event(wxEVT_LISTBOX, GetId());
    event.SetInt(selection);
    event.SetEventObject(this);
    ProcessEvent(event);

    Dismiss();
}

void SearchComboPopup::OnMouseMove(wxMouseEvent& event)
{
    wxPoint pt = wxGetMousePosition() - this->GetScreenPosition();
    int selection = this->HitTest(pt);
    wxListBox::Select(selection);
}

void SearchComboPopup::OnMouseClick(wxMouseEvent&)
{
    int selection = wxListBox::GetSelection();
    SetSelection(wxNOT_FOUND);
    ProcessSelection(selection);
}

void SearchComboPopup::OnKeyDown(wxKeyEvent& event)
{
    int key = event.GetKeyCode();

    // change selected item in the list
    if (key == WXK_UP || key == WXK_DOWN)
    {
        int selection = wxListBox::GetSelection();

        if (key == WXK_UP && selection > 0)
            selection--;
        if (key == WXK_DOWN && selection < int(wxListBox::GetCount() - 1))
            selection++;

        wxListBox::Select(selection);
    }
    // send wxEVT_LISTBOX event if "Enter" was pushed
    else if (key == WXK_NUMPAD_ENTER || key == WXK_RETURN)
        ProcessSelection(wxListBox::GetSelection());
    else
        event.Skip(); // !Needed to have EVT_CHAR generated as well
}


//------------------------------------------
//          SearchDialog
//------------------------------------------

SearchDialog::SearchDialog(OptionsSearcher* searcher)
    : GUI::DPIDialog(NULL, wxID_ANY, _L("Search"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    searcher(searcher)
{
    SetFont(GUI::wxGetApp().normal_font());
    wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    SetBackgroundColour(bgr_clr);

    default_string = _L("Type here to search");
    int border = 10;

    search_line = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);

    // wxWANTS_CHARS style is neede for process Enter key press
    search_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(em_unit() * 40, em_unit() * 30), 0, NULL, wxWANTS_CHARS);

    wxBoxSizer* check_sizer = new wxBoxSizer(wxHORIZONTAL);

    check_type      = new wxCheckBox(this, wxID_ANY, _L("Type"));
    check_category  = new wxCheckBox(this, wxID_ANY, _L("Category"));
    check_group     = new wxCheckBox(this, wxID_ANY, _L("Group"));

    wxStdDialogButtonSizer* cancel_btn = this->CreateStdDialogButtonSizer(wxCANCEL);

    check_sizer->Add(check_type,     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->Add(check_category, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->Add(check_group,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->AddStretchSpacer(border);
    check_sizer->Add(cancel_btn,     0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(search_line, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(search_list, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(check_sizer, 0, wxEXPAND | wxALL, border);

    search_line->Bind(wxEVT_TEXT,    &SearchDialog::OnInputText, this);
    search_line->Bind(wxEVT_LEFT_UP, &SearchDialog::OnLeftUpInTextCtrl, this);
    // process wxEVT_KEY_DOWN to navigate inside search_list, if ArrowUp/Down was pressed
    search_line->Bind(wxEVT_KEY_DOWN,&SearchDialog::OnKeyDown, this);

    search_list->Bind(wxEVT_MOTION,  &SearchDialog::OnMouseMove, this);
    search_list->Bind(wxEVT_LEFT_UP, &SearchDialog::OnMouseClick, this);
    search_list->Bind(wxEVT_KEY_DOWN,&SearchDialog::OnKeyDown, this);

    check_type    ->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);
    check_category->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);
    check_group   ->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);

    this->Bind(wxEVT_LISTBOX, &SearchDialog::OnSelect, this);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

void SearchDialog::Popup(wxPoint position /*= wxDefaultPosition*/)
{
    const std::string& line = searcher->search_string();
    search_line->SetValue(line.empty() ? default_string : from_u8(line));
    search_line->SetFocus();
    search_line->SelectAll();

    update_list();

    const OptionViewParameters& params = searcher->view_params;
    check_type->SetValue(params.type);
    check_category->SetValue(params.category);
    check_group->SetValue(params.group);

    this->SetPosition(position);
    this->ShowModal();
}

void SearchDialog::ProcessSelection(int selection)
{
    if (selection < 0)
        return;

    GUI::wxGetApp().sidebar().jump_to_option(selection);
    this->EndModal(wxID_CLOSE);
}

void SearchDialog::OnInputText(wxCommandEvent&)
{
    search_line->SetInsertionPointEnd();

    wxString input_string = search_line->GetValue();
    if (input_string == default_string)
        input_string.Clear();

    searcher->search(into_u8(input_string));

    update_list();
}

void SearchDialog::OnLeftUpInTextCtrl(wxEvent& event)
{
    if (search_line->GetValue() == default_string)
        search_line->SetValue("");

    event.Skip();
}

void SearchDialog::OnMouseMove(wxMouseEvent& event)
{
    wxPoint pt = wxGetMousePosition() - search_list->GetScreenPosition();
    int selection = search_list->HitTest(pt);
    search_list->Select(selection);
}

void SearchDialog::OnMouseClick(wxMouseEvent&)
{
    int selection = search_list->GetSelection();
    search_list->SetSelection(wxNOT_FOUND);

    wxCommandEvent event(wxEVT_LISTBOX, search_list->GetId());
    event.SetInt(selection);
    event.SetEventObject(search_list);
    ProcessEvent(event);
}

void SearchDialog::OnSelect(wxCommandEvent& event)
{
    int selection = event.GetSelection();
    ProcessSelection(selection);
}

void SearchDialog::update_list()
{
    search_list->Clear();

    const std::vector<FoundOption>& filters = searcher->found_options();
    for (const FoundOption& item : filters)
        search_list->Append(from_u8(item.label));
}

void SearchDialog::OnKeyDown(wxKeyEvent& event)
{
    int key = event.GetKeyCode();

    // change selected item in the list
    if (key == WXK_UP || key == WXK_DOWN)
    {
        int selection = search_list->GetSelection();

        if (key == WXK_UP && selection > 0)
            selection--;
        if (key == WXK_DOWN && selection < int(search_list->GetCount() - 1))
            selection++;

        search_list->Select(selection);
        // This function could be called from search_line,
        // So, for the next correct navigation, set focus on the search_list
        search_list->SetFocus();
    }
    // process "Enter" pressed
    else if (key == WXK_NUMPAD_ENTER || key == WXK_RETURN)
        ProcessSelection(search_list->GetSelection());
    else
        event.Skip(); // !Needed to have EVT_CHAR generated as well
}

void SearchDialog::OnCheck(wxCommandEvent& event)
{
    OptionViewParameters& params = searcher->view_params;
    params.type     = check_type->GetValue();
    params.category = check_category->GetValue();
    params.group    = check_group->GetValue();

    searcher->search();
    update_list();
}

void SearchDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL });

    const wxSize& size = wxSize(40 * em, 30 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}


}

}    // namespace Slic3r::GUI
