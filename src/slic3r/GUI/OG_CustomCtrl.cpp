#include "OG_CustomCtrl.hpp"
#include "OptionsGroup.hpp"
#include "ConfigExceptions.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"

#include <utility>
#include <wx/numformatter.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include "libslic3r/Exception.hpp"
#include "libslic3r/Utils.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

OG_CustomCtrl::OG_CustomCtrl(   wxWindow*            parent,
                                OptionsGroup*        og,
                                const wxPoint&       pos /* = wxDefaultPosition*/,
                                const wxSize&        size/* = wxDefaultSize*/,
                                const wxValidator&   val /* = wxDefaultValidator*/,
                                const wxString&      name/* = wxEmptyString*/) :
    wxControl(parent, wxID_ANY, pos, size, wxWANTS_CHARS | wxBORDER_NONE),
    opt_group(og)
{
    if (!wxOSX)
        SetDoubleBuffered(true);// SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX

    m_font      = wxGetApp().normal_font();
    m_em_unit   = em_unit(m_parent);
    m_v_gap     = lround(1.0 * m_em_unit);
    m_h_gap     = lround(0.2 * m_em_unit);

    m_bmp_mode_sz       = create_scaled_bitmap("mode_simple", this, wxOSX ? 10 : 12).GetSize();
    m_bmp_blinking_sz   = create_scaled_bitmap("search_blink", this).GetSize();

    init_ctrl_lines();// from og.lines()

    this->Bind(wxEVT_PAINT,     &OG_CustomCtrl::OnPaint, this);
    this->Bind(wxEVT_MOTION,    &OG_CustomCtrl::OnMotion, this);
    this->Bind(wxEVT_LEFT_DOWN, &OG_CustomCtrl::OnLeftDown, this);
    this->Bind(wxEVT_LEFT_UP,   &OG_CustomCtrl::OnLeftUp, this);
}

void OG_CustomCtrl::init_ctrl_lines()
{
    for (const Line& line : opt_group->get_lines())
    {
        if (line.full_width && (
            // description line
            line.widget != nullptr ||
            // description line with widget (button)
            !line.get_extra_widgets().empty())
            )
            continue;

        const std::vector<Option>& option_set = line.get_options();
        wxCoord height;

        // if we have a single option with no label, no sidetext just add it directly to sizer
        if (option_set.size() == 1 && opt_group->label_width == 0 && option_set.front().opt.full_width &&
            option_set.front().opt.label.empty() &&
            option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr &&
            line.get_extra_widgets().size() == 0)
        {
            height = m_bmp_blinking_sz.GetHeight() + m_v_gap;
            ctrl_lines.emplace_back(CtrlLine{ height, this, line, true });
        }
        else if (opt_group->label_width != 0 && !line.label.IsEmpty())
        {
            wxSize label_sz = GetTextExtent(line.label);
            height = label_sz.y * (label_sz.GetWidth() > int(opt_group->label_width * m_em_unit) ? 2 : 1) + m_v_gap;
            ctrl_lines.emplace_back(CtrlLine{ height, this, line });
        }
        else
            int i = 0;
    }
}

int OG_CustomCtrl::get_height(const Line& line)
{
    for (auto ctrl_line : ctrl_lines)
        if (&ctrl_line.og_line == &line)
            return ctrl_line.height;
        
    return 0;
}

wxPoint OG_CustomCtrl::get_pos(const Line& line, Field* field_in/* = nullptr*/)
{
    wxCoord v_pos = 0;
    wxCoord h_pos = 0;
    for (auto ctrl_line : ctrl_lines) {
        if (&ctrl_line.og_line == &line)
        {
            h_pos = m_bmp_mode_sz.GetWidth() + m_h_gap;
            if (line.near_label_widget_win) {
                wxSize near_label_widget_sz = line.near_label_widget_win->GetSize();
                if (field_in)
                    h_pos += near_label_widget_sz.GetWidth() + m_h_gap;
                else
                    break;
            }

            wxString label = line.label;
            if (opt_group->label_width != 0 && !label.IsEmpty())
                h_pos += opt_group->label_width * m_em_unit + m_h_gap;

            if (line.widget)
                break;

            int action_buttons_width = 3 * (m_bmp_blinking_sz.GetWidth() + m_h_gap);

            // If we have a single option with no sidetext
            const std::vector<Option>& option_set = line.get_options();
            if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
                option_set.front().opt.label.empty() &&
                option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0)
            {
                h_pos += action_buttons_width;
                break;
            }

            for (auto opt : option_set) {
                Field* field = opt_group->get_field(opt.opt_id);
                ConfigOptionDef option = opt.opt;
                // add label if any
                if (!option.label.empty()) {
                    //!            To correct translation by context have to use wxGETTEXT_IN_CONTEXT macro from wxWidget 3.1.1
                    label = (option.label == L_CONTEXT("Top", "Layers") || option.label == L_CONTEXT("Bottom", "Layers")) ?
                        _CTX(option.label, "Layers") : _(option.label);
                    label += ":";

                    wxPaintDC dc(this);
                    dc.SetFont(m_font);
                    h_pos += dc.GetMultiLineTextExtent(label).x + m_h_gap;
                }                
                h_pos += action_buttons_width;
                
                if (field == field_in)
                    break;    
                h_pos += field->getWindow()->GetSize().x;

                if (option_set.size() == 1 && option_set.front().opt.full_width)
                    break;

                // add sidetext if any
                if (!option.sidetext.empty() || opt_group->sidetext_width > 0)
                    h_pos += opt_group->sidetext_width * m_em_unit + m_h_gap;

                if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
                    h_pos += lround(0.6 * m_em_unit);
            }
            break;
        }
        if (ctrl_line.is_visible)
            v_pos += ctrl_line.height;
    }

    return wxPoint(h_pos, v_pos);
}


void OG_CustomCtrl::OnPaint(wxPaintEvent&)
{
#ifdef _WIN32 
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#else
    SetBackgroundColour(GetParent()->GetBackgroundColour());
#endif // _WIN32 

    wxPaintDC dc(this);
    dc.SetFont(m_font);

    wxCoord v_pos = 0;
    for (auto line : ctrl_lines) {
        if (!line.is_visible)
            continue;
        line.render(dc, v_pos);
        v_pos += line.height;
    }
}

void OG_CustomCtrl::OnMotion(wxMouseEvent& event)
{
    bool action = false;

    const wxPoint pos = event.GetLogicalPosition(wxClientDC(this));
    Refresh();
    Update();
    event.Skip();
}

void OG_CustomCtrl::OnLeftDown(wxMouseEvent& event)
{
    if (HasCapture())
        return;
    this->CaptureMouse();

    event.Skip();
}

void OG_CustomCtrl::OnLeftUp(wxMouseEvent& event)
{
    if (HasCapture())
        return;
    this->CaptureMouse();

    event.Skip();
}

bool OG_CustomCtrl::update_visibility(ConfigOptionMode mode)
{
    wxCoord    v_pos = 0;

    size_t invisible_lines = 0;
    for (CtrlLine& line : ctrl_lines) {
        line.update_visibility(mode);
        if (line.is_visible)
            v_pos += (wxCoord)line.height;
        else
            invisible_lines++;
    }    

    this->SetMinSize(wxSize(wxDefaultCoord, v_pos));

    return invisible_lines != ctrl_lines.size();
}

void OG_CustomCtrl::correct_window_position(wxWindow* win, const Line& line, Field* field/* = nullptr*/)
{
    wxPoint pos = get_pos(line, field);
    int line_height = get_height(line);
    pos.y += std::max(0, int(0.5 * (line_height - win->GetSize().y)));
    win->SetPosition(pos);
};

void OG_CustomCtrl::correct_widgets_position(wxSizer* widget, const Line& line, Field* field/* = nullptr*/) {
    auto children = widget->GetChildren();
    wxPoint line_pos = get_pos(line, field);
    int line_height = get_height(line);
    for (auto child : children)
        if (child->IsWindow()) {
            wxPoint pos = line_pos;
            wxSize  sz = child->GetWindow()->GetSize();
            pos.y += std::max(0, int(0.5 * (line_height - sz.y)));
            child->GetWindow()->SetPosition(pos);
            line_pos.x += sz.x + m_h_gap;
        }
};

void OG_CustomCtrl::msw_rescale()
{
    m_font      = wxGetApp().normal_font();
    m_em_unit   = em_unit(m_parent);
    m_v_gap     = lround(1.0 * m_em_unit);
    m_h_gap     = lround(0.2 * m_em_unit);

    m_bmp_mode_sz = create_scaled_bitmap("mode_simple", this, wxOSX ? 10 : 12).GetSize();
    m_bmp_blinking_sz = create_scaled_bitmap("search_blink", this).GetSize();

    wxCoord    v_pos = 0;
    for (CtrlLine& line : ctrl_lines) {
        line.msw_rescale();
        if (line.is_visible)
            v_pos += (wxCoord)line.height;
    }
    this->SetMinSize(wxSize(wxDefaultCoord, v_pos));

    GetParent()->Layout();
}

void OG_CustomCtrl::sys_color_changed()
{
    msw_rescale();
}

void OG_CustomCtrl::CtrlLine::correct_items_positions()
{
    if (draw_just_act_buttons || !is_visible)
        return;

    if (og_line.near_label_widget_win)
        ctrl->correct_window_position(og_line.near_label_widget_win, og_line);
    if (og_line.widget_sizer)
        ctrl->correct_widgets_position(og_line.widget_sizer, og_line);
    if (og_line.extra_widget_sizer)
        ctrl->correct_widgets_position(og_line.extra_widget_sizer, og_line);

    const std::vector<Option>& option_set = og_line.get_options();
    for (auto opt : option_set) {
        Field* field = ctrl->opt_group->get_field(opt.opt_id);
        if (!field)
            continue;
        if (field->getSizer())
            ctrl->correct_widgets_position(field->getSizer(), og_line, field);
        else if (field->getWindow())
            ctrl->correct_window_position(field->getWindow(), og_line, field);
    }
}

void OG_CustomCtrl::CtrlLine::msw_rescale()
{
    // if we have a single option with no label, no sidetext
    if (draw_just_act_buttons)
        height = create_scaled_bitmap("empty").GetHeight();

    if (ctrl->opt_group->label_width != 0 && !og_line.label.IsEmpty()) {
        wxSize label_sz = ctrl->GetTextExtent(og_line.label);
        height = label_sz.y * (label_sz.GetWidth() > int(ctrl->opt_group->label_width * ctrl->m_em_unit) ? 2 : 1) + ctrl->m_v_gap;
    }
    
    if (og_line.get_options().front().opt.full_width) {
        Field* field = ctrl->opt_group->get_field(og_line.get_options().front().opt_id);
        if (field->getWindow())
            field->getWindow()->SetSize(wxSize(3 * Field::def_width_wider() * ctrl->m_em_unit, -1));
    }

    correct_items_positions();
}

void OG_CustomCtrl::CtrlLine::update_visibility(ConfigOptionMode mode)
{
    const std::vector<Option>& option_set = og_line.get_options();

    const ConfigOptionMode& line_mode = option_set.front().opt.mode;
    is_visible = line_mode <= mode;

    if (draw_just_act_buttons)
        return;

    if (og_line.near_label_widget_win)
        og_line.near_label_widget_win->Show(is_visible);
    if (og_line.widget_sizer)
        og_line.widget_sizer->ShowItems(is_visible);
    if (og_line.extra_widget_sizer)
        og_line.extra_widget_sizer->ShowItems(is_visible);

    for (auto opt : option_set) {
        Field* field = ctrl->opt_group->get_field(opt.opt_id);
        if (!field)
            continue;

        if (field->getSizer()) {
            auto children = field->getSizer()->GetChildren();
            for (auto child : children)
                if (child->IsWindow())
                    child->GetWindow()->Show(is_visible);
        }
        else if (field->getWindow())
            field->getWindow()->Show(is_visible);
    }

    correct_items_positions();
}

void OG_CustomCtrl::CtrlLine::render(wxDC& dc, wxCoord v_pos)
{
    Field* field = ctrl->opt_group->get_field(og_line.get_options().front().opt_id);

    if (draw_just_act_buttons) {
        if (field)
            draw_act_bmps(dc, wxPoint(0, v_pos), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp());
        return;
    }

    wxCoord h_pos = draw_mode_bmp(dc, v_pos);

    if (og_line.near_label_widget_win)
        h_pos += og_line.near_label_widget_win->GetSize().x + ctrl->m_h_gap;

    const std::vector<Option>& option_set = og_line.get_options();

    wxString label = og_line.label;
    if (ctrl->opt_group->label_width != 0 && !label.IsEmpty()) {
        const wxColour* text_clr = (option_set.size() == 1 && field ? field->label_color() : og_line.full_Label_color);
        h_pos = draw_text(dc, wxPoint(h_pos, v_pos), label + ":", text_clr, ctrl->opt_group->label_width * ctrl->m_em_unit);
    }

    // If there's a widget, build it and add the result to the sizer.
    if (og_line.widget != nullptr)
        return;

    // If we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things

    // If we have a single option with no sidetext just add it directly to the grid sizer
    if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
        option_set.front().opt.label.empty() &&
        option_set.front().side_widget == nullptr && og_line.get_extra_widgets().size() == 0)
    {
        if (field)
            draw_act_bmps(dc, wxPoint(h_pos, v_pos), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp());
        return;
    }

    for (auto opt : option_set) {
        field = ctrl->opt_group->get_field(opt.opt_id);
        ConfigOptionDef option = opt.opt;
        // add label if any
        if (!option.label.empty()) {
            //!            To correct translation by context have to use wxGETTEXT_IN_CONTEXT macro from wxWidget 3.1.1
            label = (option.label == L_CONTEXT("Top", "Layers") || option.label == L_CONTEXT("Bottom", "Layers")) ?
                    _CTX(option.label, "Layers") : _(option.label);
            label += ":";

            h_pos = draw_text(dc, wxPoint(h_pos, v_pos), label, field ? field->label_color() : nullptr, ctrl->opt_group->sublabel_width * ctrl->m_em_unit);
        }

        if (field) {
            h_pos = draw_act_bmps(dc, wxPoint(h_pos, v_pos), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp());
            if (field->getSizer())
            {
                auto children = field->getSizer()->GetChildren();
                for (auto child : children)
                    if (child->IsWindow())
                        h_pos += child->GetWindow()->GetSize().x + ctrl->m_h_gap;
            }
            else if (field->getWindow())
                h_pos += field->getWindow()->GetSize().x + ctrl->m_h_gap;
        }

        // add field
        if (option_set.size() == 1 && option_set.front().opt.full_width)
            break;

        // add sidetext if any
        if (!option.sidetext.empty() || ctrl->opt_group->sidetext_width > 0)
            h_pos = draw_text(dc, wxPoint(h_pos, v_pos), _(option.sidetext), nullptr, ctrl->opt_group->sidetext_width * ctrl->m_em_unit);

        if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
            h_pos += lround(0.6 * ctrl->m_em_unit);
    }
}

wxCoord OG_CustomCtrl::CtrlLine::draw_mode_bmp(wxDC& dc, wxCoord v_pos)
{
    ConfigOptionMode mode = og_line.get_options()[0].opt.mode;
    const std::string& bmp_name = mode == ConfigOptionMode::comSimple   ? "mode_simple" :
                                  mode == ConfigOptionMode::comAdvanced ? "mode_advanced" : "mode_expert";
    wxBitmap bmp = create_scaled_bitmap(bmp_name, ctrl, wxOSX ? 10 : 12);
    wxCoord y_draw = v_pos + lround((height - bmp.GetHeight()) / 2);

    dc.DrawBitmap(bmp, 0, y_draw);

    return bmp.GetWidth() + ctrl->m_h_gap;
}

wxCoord    OG_CustomCtrl::CtrlLine::draw_text(wxDC& dc, wxPoint pos, const wxString& text, const wxColour* color, int width)
{
    wxString multiline_text;
    if (width > 0 && dc.GetTextExtent(text).x > width) {
        multiline_text = text;

        size_t idx = size_t(-1);
        for (size_t i = 0; i < multiline_text.Len(); i++)
        {
            if (multiline_text[i] == ' ')
            {
                if (dc.GetTextExtent(multiline_text.SubString(0, i)).x < width)
                    idx = i;
                else {
                    if (idx != size_t(-1))
                        multiline_text[idx] = '\n';
                    else
                        multiline_text[i] = '\n';
                    break;
                }
            }
        }

        if (idx != size_t(-1))
            multiline_text[idx] = '\n';
    }

    const wxString& out_text = multiline_text.IsEmpty() ? text : multiline_text;
    wxCoord text_width, text_height;
    dc.GetMultiLineTextExtent(out_text, &text_width, &text_height);

    pos.y = pos.y + lround((height - text_height) / 2);
    
    wxColour old_clr = dc.GetTextForeground();
    dc.SetTextForeground(color ? *color : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    dc.DrawText(out_text, pos);
    dc.SetTextForeground(old_clr);

    if (width < 1)
        width = text_width;

    return pos.x + width + ctrl->m_h_gap;
}

wxCoord    OG_CustomCtrl::CtrlLine::draw_act_bmps(wxDC& dc, wxPoint pos, const wxBitmap& bmp_undo_to_sys, const wxBitmap& bmp_undo)
{
    wxBitmap bmp_blinking = create_scaled_bitmap("search_blink", ctrl);
    wxCoord h_pos = pos.x;
    wxCoord pos_y = pos.y + lround((height - bmp_blinking.GetHeight()) / 2);

    dc.DrawBitmap(bmp_blinking, h_pos, pos_y);

    int bmp_dim = bmp_blinking.GetWidth();
    m_rect_blinking = wxRect(h_pos, pos_y, bmp_dim, bmp_dim);

    h_pos += bmp_dim + ctrl->m_h_gap;
    dc.DrawBitmap(bmp_undo_to_sys, h_pos, pos_y);

    bmp_dim = bmp_undo_to_sys.GetWidth();
    m_rect_undo_to_sys_icon = wxRect(h_pos, pos_y, bmp_dim, bmp_dim);

    h_pos += bmp_dim + ctrl->m_h_gap;
    dc.DrawBitmap(bmp_undo, h_pos, pos_y);

    bmp_dim = bmp_undo.GetWidth();
    m_rect_undo_icon = wxRect(h_pos, pos_y, bmp_dim, bmp_dim);

    h_pos += bmp_dim + ctrl->m_h_gap;

    return h_pos;
}

} // GUI
} // Slic3r
