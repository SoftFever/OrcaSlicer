#include "DropDown.hpp"
#include "Label.hpp"

#include <wx/display.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

#include <set>

wxDEFINE_EVENT(EVT_DISMISS, wxCommandEvent);

BEGIN_EVENT_TABLE(DropDown, PopupWindow)

EVT_LEFT_DOWN(DropDown::mouseDown)
EVT_LEFT_UP(DropDown::mouseReleased)
EVT_MOUSE_CAPTURE_LOST(DropDown::mouseCaptureLost)
EVT_MOTION(DropDown::mouseMove)
EVT_MOUSEWHEEL(DropDown::mouseWheelMoved)

// catch paint events
EVT_PAINT(DropDown::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

DropDown::DropDown(std::vector<Item> &items)
    : items(items)
    , state_handler(this)
    , border_color(0xDBDBDB)
    , text_color(0x363636)
    , selector_border_color(std::make_pair(0x009688, (int) StateColor::Hovered),
        std::make_pair(*wxWHITE, (int) StateColor::Normal))
    , selector_background_color(std::make_pair(0xBFE1DE, (int) StateColor::Checked), // ORCA updated background color for checked item
        std::make_pair(*wxWHITE, (int) StateColor::Normal))
{
}

DropDown::DropDown(wxWindow *parent, std::vector<Item> &items, long style)
    : DropDown(items)
{
    Create(parent, style);
}

void DropDown::Create(wxWindow *parent, long style)
{
    PopupWindow::Create(parent, wxPU_CONTAINS_CONTROLS);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);
    state_handler.attach({&border_color, &text_color, &selector_border_color, &selector_background_color});
    state_handler.update_binds();
    if ((style & DD_NO_CHECK_ICON) == 0)
        check_bitmap = ScalableBitmap(this, "checked", 16);
    arrow_bitmap = ScalableBitmap(this, "hms_arrow", 16);
    text_off = style & DD_NO_TEXT;

    // BBS set default font
    SetFont(Label::Body_14);
#ifdef __WXOSX__
    // PopupWindow releases mouse on idle, which may cause various problems,
    //  such as losting mouse move, and dismissing soon on first LEFT_DOWN event.
    Bind(wxEVT_IDLE, [] (wxIdleEvent & evt) {});
#endif
}

void DropDown::Invalidate(bool clear)
{
    if (clear) {
        selection = hover_item = -1;
        offset = wxPoint();
    }
    assert(selection < (int) items.size());
    need_sync = true;
}

void DropDown::SetSelection(int n)
{
    if (n >= (int) items.size())
        n = -1;
    if (selection == n) return;
    selection = n;
    if (need_sync) { // for icon Size
        messureSize();
        need_sync = true;
    }
    if (subDropDown)
        subDropDown->SetSelection(n);
    paintNow();
}

wxString DropDown::GetValue() const
{
    return selection >= 0 ? items[selection].text : wxString();
}

void DropDown::SetValue(const wxString &value)
{
    auto i    = std::find_if(items.begin(), items.end(), [&value](Item & item) { return item.text == value; });
    selection = i == items.end() ? -1 : std::distance(items.begin(), i);
}

void DropDown::SetCornerRadius(double radius)
{
    this->radius = radius;
    paintNow();
}

void DropDown::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetSelectorBorderColor(StateColor const &color)
{
    selector_border_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetSelectorBackgroundColor(StateColor const &color)
{
    selector_background_color = color;
    state_handler.update_binds();
    paintNow();
}

void DropDown::SetUseContentWidth(bool use, bool limit_max_content_width)
{
    if (use_content_width == use)
        return;
    use_content_width = use;
    this->limit_max_content_width = limit_max_content_width;
    need_sync = true;
    messureSize();
}

void DropDown::SetAlignIcon(bool align) { align_icon = align; }

void DropDown::Rescale()
{
    need_sync = true;
}

bool DropDown::HasDismissLongTime()
{
    auto now = boost::posix_time::microsec_clock::universal_time();
    return !IsShown() &&
        (now - dismissTime).total_milliseconds() >= 20;
}

void DropDown::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxBufferedPaintDC dc(this);
    render(dc);
}

/*
 * Alternatively, you can use a clientDC to paint on the panel
 * at any time. Using this generally does not free you from
 * catching paint events, since it is possible that e.g. the window
 * manager throws away your drawing when the window comes to the
 * background, and expects you will redraw it when the window comes
 * back (by sending a paint event).
 */
void DropDown::paintNow()
{
    // depending on your system you may need to look at double-buffered dcs
    //wxClientDC dc(this);
    //render(dc);
    Refresh();
}

static wxSize GetBmpSize(wxBitmap & bmp)
{
#ifdef __APPLE__
    return bmp.GetScaledSize();
#else
    return bmp.GetSize();
#endif
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void DropDown::render(wxDC &dc)
{
    if (items.size() == 0) return;
    int states = state_handler.states();
    if (subDropDown)
        states |= subDropDown->state_handler.states();
    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(GetBackgroundColour())));
    // if (GetWindowStyle() & wxBORDER_NONE)
    //    dc.SetPen(wxNullPen);

    // draw background
    wxSize size = GetSize();
    if (radius == 0)
        dc.DrawRectangle(0, 0, size.x, size.y);
    else
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);

    int selected_item = selectedItem();

    // draw hover rectangle
    wxRect rcContent = {{0, offset.y}, rowSize};
    if (hover_item >= 0 && (states & StateColor::Hovered)) {
        rcContent.y += rowSize.y * hover_item;
        if (rcContent.GetBottom() > 0 && rcContent.y < size.y) {
            if (selected_item == hover_item)
                dc.SetBrush(wxBrush(selector_background_color.colorForStates(states | StateColor::Checked)));
            dc.SetPen(wxPen(selector_border_color.colorForStates(states)));
            rcContent.Deflate(4, 1);
            dc.DrawRectangle(rcContent);
            rcContent.Inflate(4, 1);
        }
        rcContent.y = offset.y;
    }
    // draw checked rectangle
    if (selected_item >= 0 && (selected_item != hover_item || (states & StateColor::Hovered) == 0)) {
        rcContent.y += rowSize.y * selected_item;
        if (rcContent.GetBottom() > 0 && rcContent.y < size.y) {
            dc.SetBrush(wxBrush(selector_background_color.colorForStates(states | StateColor::Checked)));
            dc.SetPen(wxPen(selector_background_color.colorForStates(states)));
            rcContent.Deflate(4, 1);
            dc.DrawRectangle(rcContent);
            rcContent.Inflate(4, 1);
        }
        rcContent.y = offset.y;
    }
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    {
        wxSize offset = (rowSize - textSize) / 2;
        rcContent.Deflate(0, offset.y);
    }

    // draw position bar
    if (rowSize.y * count > size.y) {
        int    height = rowSize.y * count;
        wxRect rect = {size.x - 6, -offset.y * size.y / height, 4,
                       size.y * size.y / height};
        dc.SetPen(wxPen(border_color.defaultColor()));
        dc.SetBrush(wxBrush(*wxLIGHT_GREY));
        dc.DrawRoundedRectangle(rect, 2);
        rcContent.width -= 6;
    }

    // draw check icon
    rcContent.x += 5;
    rcContent.width -= 5;
    if (check_bitmap.bmp().IsOk()) {
        auto szBmp = check_bitmap.GetBmpSize();
        if (selected_item >= 0) {
            wxPoint pt = rcContent.GetLeftTop();
            pt.y += (rcContent.height - szBmp.y) / 2;
            pt.y += rowSize.y * selected_item;
            if (pt.y + szBmp.y > 0 && pt.y < size.y)
                dc.DrawBitmap(check_bitmap.bmp(), pt);
        }
        rcContent.x += szBmp.x + 5;
        rcContent.width -= szBmp.x + 5;
    }

    std::set<wxString> groups;
    // draw texts & icons
    dc.SetTextForeground(text_color.colorForStates(states));
    int index = 0;
    for (int i = 0; i < items.size(); ++i) {
        auto &item = items[i];
        // Skip by group
        if (group.IsEmpty()) {
            if (!item.group.IsEmpty()) {
                if (groups.find(item.group) == groups.end())
                    groups.insert(item.group);
                else
                    continue;
            }
        } else {
            if (item.group != group)
                continue;
        }
        bool is_hover = index == hover_item;
        ++index;
        if (rcContent.GetBottom() < 0) {
            rcContent.y += rowSize.y;
            continue;
        }
        if (rcContent.y > size.y) break;
        wxPoint pt   = rcContent.GetLeftTop();
        auto &  icon  = item.icon;
        auto size2 = GetBmpSize(icon);
        if (iconSize.x > 0) {
            if (icon.IsOk()) {
                pt.y += (rcContent.height - size2.y) / 2;
                dc.DrawBitmap(icon, pt);
            }
            pt.x += iconSize.x + 5;
            pt.y = rcContent.y;
        } else if (icon.IsOk()) {
            pt.y += (rcContent.height - size2.y) / 2;
            dc.DrawBitmap(icon, pt);
            pt.x += size2.x + 5;
            pt.y = rcContent.y;
        }
        auto text = group.IsEmpty()
                        ? (item.group.IsEmpty() ? item.text : item.group)
                        : (item.text.StartsWith(group) ? item.text.substr(group.size()).Trim(false) : item.text);
        if (!text_off && !text.IsEmpty()) {
            wxSize tSize = dc.GetMultiLineTextExtent(text);
            if (pt.x + tSize.x > rcContent.GetRight()) {
                if (is_hover && item.tip.IsEmpty())
                    SetToolTip(text);
                text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END,
                                            rcContent.GetRight() - pt.x);
            }
            pt.y += (rcContent.height - textSize.y) / 2;
            dc.SetFont(GetFont());
            dc.DrawText(text, pt);
            if (group.IsEmpty() && !item.group.IsEmpty()) {
                auto szBmp = arrow_bitmap.GetBmpSize();
                pt.x = rcContent.GetRight() - szBmp.x - 5;
                pt.y = rcContent.y += (rcContent.height - szBmp.y) / 2;
                dc.DrawBitmap(arrow_bitmap.bmp(), pt);
            }
        }
        rcContent.y += rowSize.y;
    }
}

int DropDown::hoverIndex()
{
    if (hover_item < 0)
        return -1;
    if (count == items.size())
        return hover_item;
    int index = -1;
    std::set<wxString> groups;
    for (size_t i = 0; i < items.size(); ++i) {
        auto &item = items[i];
        // Skip by group
        if (group.IsEmpty()) {
            if (!item.group.IsEmpty()) {
                if (groups.find(item.group) == groups.end())
                    groups.insert(item.group);
                else
                    continue;
            }
        } else {
            if (item.group != group)
                continue;
        }
        if (++index == hover_item)
            return (item.group.IsEmpty() || !group.IsEmpty()) ? i : -i - 2;
    }
    return -1;
}

int DropDown::selectedItem()
{
    if (selection < 0)
        return -1;
    if (count == items.size())
        return selection;
    auto & sel = items[selection];
    if (group.IsEmpty() ? !sel.group.IsEmpty() : sel.group != group)
        return -1;
    if (selection == 0)
        return 0;
    int                index = 0;
    std::set<wxString> groups;
    for (size_t i = 0; i < selection; ++i) {
        auto &item = items[i];
        // Skip by group
        if (group.IsEmpty()) {
            if (!item.group.IsEmpty()) {
                if (groups.find(item.group) == groups.end())
                    groups.insert(item.group);
                else
                    continue;
            }
        } else {
            if (item.group != group)
                continue;
        }
        ++index;
    }
    return index;
}

void DropDown::messureSize()
{
    if (!need_sync) return;
    textSize = wxSize();
    iconSize = wxSize();
    count = 0;
    wxClientDC dc(GetParent() ? GetParent() : this);
    dc.SetFont(GetFont());
    std::set<wxString> groups;
    for (size_t i = 0; i < items.size(); ++i) {
        auto &item = items[i];
        // Skip by group
        if (group.IsEmpty()) {
            if (!item.group.IsEmpty()) {
                if (groups.find(item.group) == groups.end())
                    groups.insert(item.group);
                else
                    continue;
            }
        } else {
            if (item.group != group)
                continue;
        }
        ++count;
        wxSize size1;
        if (!text_off) {
            auto text = group.IsEmpty()
                        ? (item.group.IsEmpty() ? item.text : item.group)
                        : (item.text.StartsWith(group) ? item.text.substr(group.size()).Trim(false) : item.text);
            size1 = dc.GetMultiLineTextExtent(text);
        }
        if (item.icon.IsOk()) {
            wxSize size2 = GetBmpSize(item.icon);
            if (size2.x > iconSize.x)
                iconSize = size2;
            if (!align_icon) {
                size1.x += size2.x + (text_off ? 0 : 5);
            }
        }
        if (size1.x > textSize.x) textSize = size1;
    }
    if (!align_icon) iconSize.x = 0;
    wxSize szContent = textSize;
    if (szContent.x < FromDIP(120))
        szContent.x = FromDIP(120);
    szContent.x += 10;
    if (check_bitmap.bmp().IsOk()) {
        auto szBmp = check_bitmap.GetBmpSize();
        szContent.x += szBmp.x + 5;
    }
    if (iconSize.x > 0) szContent.x += iconSize.x + (text_off ? 0 : 5);
    if (iconSize.y > szContent.y) szContent.y = iconSize.y;
    szContent.y += 10;
    if (count > 15) szContent.x += 6;
    if (GetParent() && group.IsEmpty()) {
        auto x = GetParent()->GetSize().x;
        if (!use_content_width || x > szContent.x)
            szContent.x = x;
    }
    rowSize = szContent;
    if (limit_max_content_width) {
        wxSize parent_size = GetParent()->GetSize();
        if (rowSize.x > parent_size.x * 2) {
            rowSize.x = 2 * parent_size.x;
            szContent = rowSize;
        }
    }
    szContent.y *= std::min((size_t) 15, count);
    szContent.y += items.size() > 15 ? rowSize.y / 2 : 0;
    wxWindow::SetSize(szContent);
#ifdef __WXGTK__
    // Gtk has a wrapper window for popup widget
    gtk_window_resize (GTK_WINDOW (m_widget), szContent.x, szContent.y);
#endif
    if (!groups.empty() && subDropDown == nullptr) {
        subDropDown = new DropDown(items);
        subDropDown->mainDropDown = this;
        subDropDown->check_bitmap      = check_bitmap;
        subDropDown->text_off          = text_off;
        subDropDown->use_content_width = true;
        subDropDown->Create(GetParent());
        subDropDown->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
            e.SetEventObject(this);
            e.SetId(GetId());
            GetEventHandler()->ProcessEvent(e);
        });
    }
    need_sync = false;
}

void DropDown::autoPosition()
{
    messureSize();
    wxPoint pos;
    wxSize  off;
    if (mainDropDown) {
        pos = mainDropDown->ClientToScreen(wxPoint(0, 0));
        off = mainDropDown->GetSize();
        pos.x += 6;
        pos.y += mainDropDown->hover_item * rowSize.y + rowSize.y + mainDropDown->offset.y;
        off.x -= 12;
        off.y = -rowSize.y;
    } else {
        pos = GetParent()->ClientToScreen(wxPoint(0, 0));
        off = GetParent()->GetSize();
        pos.y -= 6;
        off.x = 0;
        off.y += 12;
    }
    wxPoint old = GetPosition();
    wxSize size = GetSize();
    Position(pos, off);
    if (old != GetPosition()) {
        size = rowSize;
        size.y *= std::min((size_t) 15, count);
        size.y += count > 15 ? rowSize.y / 2 : 0;
        if (size != GetSize()) {
            wxWindow::SetSize(size);
            offset = wxPoint();
            Position(pos, off);
        }
    }
    if (GetPosition().y > pos.y) {
        // may exceed
        auto drect = wxDisplay(GetParent()).GetGeometry();
        if (GetPosition().y + size.y + 10 > drect.GetBottom()) {
            if (use_content_width && count <= 15) size.x += 6;
            size.y = drect.GetBottom() - GetPosition().y - 10;
            wxWindow::SetSize(size);
            if (selection >= 0) {
                if (offset.y + rowSize.y * (selection + 1) > size.y)
                    offset.y = size.y - rowSize.y * (selection + 1);
                else if (offset.y + rowSize.y * selection < 0)
                    offset.y = -rowSize.y * selection;
            }
        }
    }
}

void DropDown::mouseDown(wxMouseEvent& event)
{
    // Receivce unexcepted LEFT_DOWN on Mac after OnDismiss
    if (!IsShown())
        return;
    // force calc hover item again
    mouseMove(event);
    pressedDown = true;
    CaptureMouse();
    dragStart   = event.GetPosition();
}

void DropDown::mouseReleased(wxMouseEvent& event)
{
    if (pressedDown) {
        dragStart = wxPoint();
        pressedDown = false;
        if (HasCapture())
            ReleaseMouse();
        if (hover_item >= 0) { // not moved
            sendDropDownEvent();
            DismissAndNotify();
        }
    }
}

void DropDown::mouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    wxMouseEvent evt;
    mouseReleased(evt);
}

void DropDown::mouseMove(wxMouseEvent &event)
{
    wxPoint pt  = event.GetPosition();
#ifdef __WXOSX__
    if (mainDropDown) {
        auto size = GetSize();
        if (pt.x < 0 || pt.y < 0 || pt.x >= size.x || pt.y >= size.y) {
            auto diff = GetPosition() - mainDropDown->GetPosition();
            event.SetX(pt.x + diff.x);
            event.SetY(pt.y + diff.y);
            mainDropDown->mouseMove(event);
            return;
        }
    }
#endif
    if (pressedDown) {
        wxPoint pt2 = offset + pt - dragStart;
        wxSize  size = GetSize();
        dragStart    = pt;
        if (pt2.y > 0)
            pt2.y = 0;
        else if (pt2.y + rowSize.y * int(count) < size.y)
            pt2.y = size.y - rowSize.y * int(count);
        if (pt2.y != offset.y) {
            offset = pt2;
            hover_item = -1; // moved
        } else {
            return;
        }
    }
    if (!pressedDown || hover_item >= 0) {
        int hover = (pt.y - offset.y) / rowSize.y;
        if (hover >= (int) count) hover = -1;
        if (hover == hover_item) return;
        hover_item = hover;
        int index  = hoverIndex();
        if (index < -1) {
            auto & drop = *subDropDown;
            drop.group  = items[-index - 2].group;
            drop.need_sync = true;
            drop.messureSize();
            drop.autoPosition();
            drop.paintNow();
            if (!drop.IsShown())
                drop.Popup(&drop);
        } else if (index >= 0) {
            if (subDropDown) {
                if (subDropDown->IsShown())
                    subDropDown->Dismiss();
            }
            SetToolTip(items[index].tip);
        }
    }
    paintNow();
}

void DropDown::mouseWheelMoved(wxMouseEvent &event)
{
    auto delta = event.GetWheelRotation();
    wxSize  size  = GetSize();
    wxPoint pt2   = offset + wxPoint{0, delta};
    if (pt2.y > 0)
        pt2.y = 0;
    else if (pt2.y + rowSize.y * int(count) < size.y)
        pt2.y = size.y - rowSize.y * int(count);
    if (pt2.y != offset.y) {
        offset = pt2;
    } else {
        return;
    }
    int hover = (event.GetPosition().y - offset.y) / rowSize.y;
    if (hover >= (int) count) hover = -1;
    if (hover != hover_item) {
        hover_item = hover;
        if (hover >= 0) SetToolTip(items[hover].tip);
    }
    paintNow();
}

// currently unused events
void DropDown::sendDropDownEvent()
{
    int index = hoverIndex();
    if (index < 0)
        return;
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(index);
    event.SetString(items[index].text);
    GetEventHandler()->ProcessEvent(event);
}

void DropDown::Dismiss()
{
    if (subDropDown && subDropDown->IsShown())
        return;
    PopupWindow::Dismiss();
}

void DropDown::OnDismiss()
{
    if (mainDropDown) {
        if (mainDropDown->hover_item < 0)
            mainDropDown->DismissAndNotify();
        else
#ifdef __WIN32__
            SetActiveWindow(mainDropDown->GetHandle());
#else
            ;
#endif
        return;
    }
    if (subDropDown && subDropDown->IsShown())
        return;
    dismissTime = boost::posix_time::microsec_clock::universal_time();
    hover_item  = -1;
    wxCommandEvent e(EVT_DISMISS);
    GetEventHandler()->ProcessEvent(e);
}
