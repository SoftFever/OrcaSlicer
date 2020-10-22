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
    m_og(og)
{
    if (!wxOSX)
        SetDoubleBuffered(true);// SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX

    // init bitmaps
    m_bmp_mode_simple    = ScalableBitmap(this, "mode_simple"  , wxOSX ? 10 : 12);
    m_bmp_mode_advanced    = ScalableBitmap(this, "mode_advanced", wxOSX ? 10 : 12);
    m_bmp_mode_expert    = ScalableBitmap(this, "mode_expert"  , wxOSX ? 10 : 12);
    m_bmp_blinking        = ScalableBitmap(this, "search_blink");

    m_v_gap     = lround(1.0 * wxGetApp().em_unit());
    m_h_gap     = lround(0.2 * wxGetApp().em_unit());

    init_ctrl_lines();// from og.lines()

    this->Bind(wxEVT_PAINT,     &OG_CustomCtrl::OnPaint, this);
    this->Bind(wxEVT_MOTION,    &OG_CustomCtrl::OnMotion, this);
    this->Bind(wxEVT_LEFT_DOWN, &OG_CustomCtrl::OnLeftDown, this);
    this->Bind(wxEVT_LEFT_UP,   &OG_CustomCtrl::OnLeftUp, this);

    const wxFont& font = wxGetApp().normal_font();
    m_font = wxOSX ? font.Smaller() : font;
}

void OG_CustomCtrl::init_ctrl_lines()
{
    wxCoord    v_pos = 0;

    for (const Line& line : m_og->get_lines())
    {
        if (line.full_width && (
            // description line
            line.widget != nullptr ||
            // description line with widget (button)
            !line.get_extra_widgets().empty())
            )
            continue;

        auto option_set = line.get_options();

        wxCoord height = 0;

        // if we have a single option with no label, no sidetext just add it directly to sizer
        if (option_set.size() == 1 && m_og->label_width == 0 && option_set.front().opt.full_width &&
            option_set.front().opt.label.empty() &&
            option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr &&
            line.get_extra_widgets().size() == 0)
        {
            height = m_bmp_blinking.bmp().GetHeight() + m_v_gap;
            ctrl_lines.emplace_back(CtrlLine{ height, this, line, true });
        }
        else if (m_og->label_width != 0 && !line.label.IsEmpty())
        {
            wxSize label_sz = GetTextExtent(line.label);
            height = label_sz.y * (label_sz.GetWidth() > (m_og->label_width*wxGetApp().em_unit()) ? 2 : 1) + m_v_gap;
            ctrl_lines.emplace_back(CtrlLine{ height, this, line });
        }
        else
            int i = 0;
        v_pos += height;
    }

    this->SetMinSize(wxSize(wxDefaultCoord, v_pos));
}

int OG_CustomCtrl::get_height(const Line& line)
{
    for (auto ctrl_line : ctrl_lines)
        if (&ctrl_line.m_og_line == &line)
            return ctrl_line.m_height;
        
    return 0;
}

wxPoint OG_CustomCtrl::get_pos(const Line& line, Field* field_in/* = nullptr*/)
{
    wxCoord v_pos = 0;
    wxCoord h_pos = 0;
    for (auto ctrl_line : ctrl_lines) {
        if (&ctrl_line.m_og_line == &line)
        {
            h_pos = m_bmp_mode_simple.bmp().GetWidth() + m_h_gap;
            if (line.near_label_widget) {
                wxSize near_label_widget_sz = m_og->get_last_near_label_widget()->GetSize();
                if (field_in)
                    h_pos += near_label_widget_sz.GetWidth() + m_h_gap;
                else
                    break;
            }

            wxString label = line.label;
            if (m_og->label_width != 0 && !label.IsEmpty())
                h_pos += m_og->label_width * wxGetApp().em_unit() + m_h_gap;

            if (line.widget)
                break;

            // If we have a single option with no sidetext
            const std::vector<Option>& option_set = line.get_options();
            if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
                option_set.front().opt.label.empty() &&
                option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0)
            {
                h_pos += 3 * (m_bmp_blinking.bmp().GetWidth() + m_h_gap);
                break;
            }

            for (auto opt : option_set) {
                Field* field = m_og->get_field(opt.opt_id);
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
                h_pos += 3 * (m_bmp_blinking.bmp().GetWidth() + m_h_gap);
                
                if (field == field_in)
                    break;    
                h_pos += field->getWindow()->GetSize().x;

                if (option_set.size() == 1 && option_set.front().opt.full_width)
                    break;

                // add sidetext if any
                if (!option.sidetext.empty() || m_og->sidetext_width > 0)
                    h_pos += m_og->sidetext_width * wxGetApp().em_unit() + m_h_gap;

                if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
                    h_pos += lround(0.6 * wxGetApp().em_unit());
            }
            break;
        }
        v_pos += ctrl_line.m_height;
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
        v_pos += line.m_height;
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
    return true;
}

void OG_CustomCtrl::msw_rescale()
{
    const wxFont& font = GUI::wxGetApp().normal_font();
    m_font = wxOSX ? font.Smaller() : font;

    wxSize new_sz = GUI::wxGetApp().em_unit() * this->GetSize();
    SetMinSize(new_sz);
    GetParent()->Layout();
}

void OG_CustomCtrl::sys_color_changed()
{
    
}


void OG_CustomCtrl::CtrlLine::render(wxDC& dc, wxCoord v_pos)
{
    Field* field = nullptr;
    field = m_ctrl->m_og->get_field(m_og_line.get_options().front().opt_id);

    if (draw_just_act_buttons) {
        if (field)
            draw_act_bmps(dc, wxPoint(0, v_pos), m_ctrl->m_bmp_blinking.bmp(), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp());
        return;
    }

    wxCoord h_pos = draw_mode_bmp(dc, v_pos);

    if (m_og_line.near_label_widget)
        h_pos += m_ctrl->m_bmp_blinking.bmp().GetWidth() + m_ctrl->m_h_gap;//m_og_line.near_label_widget->GetSize().x;;

    const std::vector<Option>& option_set = m_og_line.get_options();

    wxString label = m_og_line.label;
    if (m_ctrl->m_og->label_width != 0 && !label.IsEmpty()) {
        const wxColour* text_clr = (option_set.size() == 1 && field ? field->label_color() : m_og_line.full_Label_color);
        h_pos = draw_text(dc, wxPoint(h_pos, v_pos), label + ":", text_clr, m_ctrl->m_og->label_width * wxGetApp().em_unit());
    }

    // If there's a widget, build it and add the result to the sizer.
    if (m_og_line.widget != nullptr)
        return;

    // If we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things

    // If we have a single option with no sidetext just add it directly to the grid sizer
    if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
        option_set.front().opt.label.empty() &&
        option_set.front().side_widget == nullptr && m_og_line.get_extra_widgets().size() == 0)
    {
        if (field)
            draw_act_bmps(dc, wxPoint(h_pos, v_pos), m_ctrl->m_bmp_blinking.bmp(), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp());
        return;
    }

    for (auto opt : option_set) {
        field = m_ctrl->m_og->get_field(opt.opt_id);
        ConfigOptionDef option = opt.opt;
        // add label if any
        if (!option.label.empty()) {
            //!            To correct translation by context have to use wxGETTEXT_IN_CONTEXT macro from wxWidget 3.1.1
            label = (option.label == L_CONTEXT("Top", "Layers") || option.label == L_CONTEXT("Bottom", "Layers")) ?
                    _CTX(option.label, "Layers") : _(option.label);
            label += ":";

            h_pos = draw_text(dc, wxPoint(h_pos, v_pos), label, field ? field->label_color() : nullptr, m_ctrl->m_og->sublabel_width * wxGetApp().em_unit());
        }

        if (field) {
            h_pos = draw_act_bmps(dc, wxPoint(h_pos, v_pos), m_ctrl->m_bmp_blinking.bmp(), field->undo_to_sys_bitmap()->bmp(), field->undo_bitmap()->bmp());
            if (field->getSizer())
            {
                auto children = field->getSizer()->GetChildren();
                for (auto child : children)
                    if (child->IsWindow())
                        h_pos += child->GetWindow()->GetSize().x + m_ctrl->m_h_gap;
            }
            else if (field->getWindow())
                h_pos += field->getWindow()->GetSize().x + m_ctrl->m_h_gap;
        }

        // add field
        if (option_set.size() == 1 && option_set.front().opt.full_width)
            break;

        // add sidetext if any
        if (!option.sidetext.empty() || m_ctrl->m_og->sidetext_width > 0)
            h_pos = draw_text(dc, wxPoint(h_pos, v_pos), _(option.sidetext), nullptr, m_ctrl->m_og->sidetext_width * wxGetApp().em_unit());

        if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
            h_pos += lround(0.6 * wxGetApp().em_unit());
    }
}

wxCoord OG_CustomCtrl::CtrlLine::draw_mode_bmp(wxDC& dc, wxCoord v_pos)
{
    ConfigOptionMode mode = m_og_line.get_options()[0].opt.mode;
    const wxBitmap&  bmp  = mode == ConfigOptionMode::comSimple   ? m_ctrl->m_bmp_mode_simple.bmp()   :
                            mode == ConfigOptionMode::comAdvanced ? m_ctrl->m_bmp_mode_advanced.bmp() : m_ctrl->m_bmp_mode_expert.bmp();

    wxCoord y_draw = v_pos + lround((m_height - bmp.GetHeight()) / 2);

    dc.DrawBitmap(bmp, 0, y_draw);

    return bmp.GetWidth() + m_ctrl->m_h_gap;
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

    pos.y = pos.y + lround((m_height - text_height) / 2);
    
    wxColour old_clr = dc.GetTextForeground();
    dc.SetTextForeground(color ? *color : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    dc.DrawText(out_text, pos);
    dc.SetTextForeground(old_clr);

    if (width < 1)
        width = text_width;

    return pos.x + width + m_ctrl->m_h_gap;
}

wxCoord    OG_CustomCtrl::CtrlLine::draw_act_bmps(wxDC& dc, wxPoint pos, const wxBitmap& bmp_blinking, const wxBitmap& bmp_undo_to_sys, const wxBitmap& bmp_undo)
{
    wxCoord h_pos = pos.x;
    wxCoord pos_y = pos.y + lround((m_height - bmp_blinking.GetHeight()) / 2);

    dc.DrawBitmap(bmp_blinking, h_pos, pos_y);

    int bmp_dim = bmp_blinking.GetWidth();
    m_rect_blinking = wxRect(h_pos, pos_y, bmp_dim, bmp_dim);

    h_pos += bmp_dim + m_ctrl->m_h_gap;
    dc.DrawBitmap(bmp_undo_to_sys, h_pos, pos_y);

    bmp_dim = bmp_undo_to_sys.GetWidth();
    m_rect_undo_to_sys_icon = wxRect(h_pos, pos_y, bmp_dim, bmp_dim);

    h_pos += bmp_dim + m_ctrl->m_h_gap;
    dc.DrawBitmap(bmp_undo, h_pos, pos_y);

    bmp_dim = bmp_undo.GetWidth();
    m_rect_undo_icon = wxRect(h_pos, pos_y, bmp_dim, bmp_dim);

    h_pos += bmp_dim + m_ctrl->m_h_gap;

    return h_pos;
}

} // GUI
} // Slic3r
