#include "FilamentDropDown.hpp"
#include "Widgets/Label.hpp"

#include <algorithm>
#include <limits>
#include <memory>

#include <boost/log/trivial.hpp>

#include <wx/display.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/weakref.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

#ifdef __WIN32__
#include <wx/msw/private.h>
#endif

namespace
{

constexpr int g_filament_submenu_width_dip = 200;

size_t max_visible_row_count(int max_visible_rows)
{
    return max_visible_rows > 0 ? static_cast<size_t>(max_visible_rows) : size_t{1};
}

int multiply_to_int(int value, size_t multiplier)
{
    if (value <= 0 || multiplier == 0)
        return 0;

    const size_t positive_value = static_cast<size_t>(value);
    const size_t max_int        = static_cast<size_t>(std::numeric_limits<int>::max());
    if (positive_value > max_int / multiplier)
        return std::numeric_limits<int>::max();
    return static_cast<int>(positive_value * multiplier);
}

int add_to_int(int left, int right)
{
    if (right > 0 && left > std::numeric_limits<int>::max() - right)
        return std::numeric_limits<int>::max();
    if (right < 0 && left < std::numeric_limits<int>::min() - right)
        return std::numeric_limits<int>::min();
    return left + right;
}

bool point_in_anchor_gap(const wxWindow *anchor, const wxWindow *popup, const wxPoint &screen_point)
{
    if (anchor == nullptr || popup == nullptr)
        return false;

    const wxRect anchor_rect = anchor->GetScreenRect();
    const wxRect popup_rect  = popup->GetScreenRect();
    const int    left        = std::max(anchor_rect.GetLeft(), popup_rect.GetLeft());
    const int    right       = std::min(anchor_rect.GetRight(), popup_rect.GetRight());
    if (right < left)
        return false;

    // Screen y grows downward. Boundary pixels belong to the anchor or popup, so the clickable gap is
    // strictly between the two rectangles.
    if (popup_rect.GetTop() > anchor_rect.GetBottom())
    {
        return screen_point.x >= left && screen_point.x <= right && screen_point.y > anchor_rect.GetBottom() &&
               screen_point.y < popup_rect.GetTop();
    }

    if (popup_rect.GetBottom() < anchor_rect.GetTop())
    {
        return screen_point.x >= left && screen_point.x <= right && screen_point.y > popup_rect.GetBottom() &&
               screen_point.y < anchor_rect.GetTop();
    }

    return false;
}

} // namespace

FilamentDropDown::FilamentDropDown(const std::vector<Item> &items)
    : items(items)
    , state_handler(this)
    , text_color(0x363636)
    , border_color(0xDBDBDB)
    , selector_border_color(std::make_pair(0x009688, static_cast<int>(StateColor::Hovered)),
                            std::make_pair(*wxWHITE, static_cast<int>(StateColor::Normal)))
    , selector_background_color(std::make_pair(0xBFE1DE, static_cast<int>(StateColor::Checked)),
                                std::make_pair(*wxWHITE, static_cast<int>(StateColor::Normal)))
{
}

FilamentDropDown::~FilamentDropDown()
{
    submenu_motion_timer.Stop();

    // The submenu is a sibling window under the same wx parent, and wx destroys children in creation
    // order, so it is still alive here; drop its raw back-pointer before it can use it again.
    if (subDropDown != nullptr)
        subDropDown->mainDropDown = nullptr;
}

bool FilamentDropDown::Create(wxWindow *parent, long style)
{
    if (parent == nullptr || !PopupWindow::Create(parent, wxPU_CONTAINS_CONTROLS))
        return false;

    Bind(wxEVT_LEFT_DOWN, &FilamentDropDown::mouseDown, this);
    Bind(wxEVT_LEFT_UP, &FilamentDropDown::mouseReleased, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &FilamentDropDown::mouseCaptureLost, this);
    Bind(wxEVT_MOTION, &FilamentDropDown::mouseMove, this);
    Bind(wxEVT_MOUSEWHEEL, &FilamentDropDown::mouseWheelMoved, this);
    Bind(wxEVT_PAINT, &FilamentDropDown::paintEvent, this);
#ifdef __WXGTK__
    submenu_motion_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &FilamentDropDown::on_submenu_motion_timer, this, submenu_motion_timer.GetId());
#endif

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);
    state_handler.attach({&border_color, &text_color, &selector_border_color, &selector_background_color});
    state_handler.update_binds();
    if ((style & DD_NO_CHECK_ICON) == 0)
        check_bitmap = ScalableBitmap(this, "checked", 16);
    arrow_bitmap = ScalableBitmap(this, "hms_arrow", 16);
    text_off     = style & DD_NO_TEXT;

    // BBS set default font
    SetFont(Label::Body_14);
#ifdef __WXOSX__
    // PopupWindow releases mouse on idle, which may cause various problems,
    //  such as losting mouse move, and dismissing soon on first LEFT_DOWN event.
    Bind(wxEVT_IDLE, [](wxIdleEvent &evt) {});
#endif

    return true;
}

void FilamentDropDown::Invalidate(bool clear)
{
    if (clear)
    {
        selection = hover_item = -1;
        offset = wxPoint();
        SetToolTip(wxString());
    }
    if (selection >= 0 && static_cast<size_t>(selection) >= items.size())
        selection = -1;
    need_sync = true;
}

void FilamentDropDown::SetItems(const std::vector<Item> &new_items)
{
    items = new_items;
    if (use_flat_fallback)
        apply_flat_fallback();
    Invalidate(true);
    if (subDropDown != nullptr)
    {
        subDropDown->setGroup(wxString());
        subDropDown->SetItems(new_items);
    }
}

void FilamentDropDown::SetSelection(int n)
{
    if (n < 0 || static_cast<size_t>(n) >= items.size())
        n = -1;
    if (selection == n) return;
    selection = n;
    if (need_sync) // for icon Size
    {
        messureSize();
    }
    if (subDropDown)
        subDropDown->SetSelection(n);
    paintNow();
}

void FilamentDropDown::SetUseContentWidth(bool use, bool limit_max_content_width)
{
    if (use_content_width == use)
        return;
    use_content_width = use;
    this->limit_max_content_width = limit_max_content_width;
    need_sync = true;
    messureSize();
}

void FilamentDropDown::SetAlignIcon(bool align)
{
    align_icon = align;
}

void FilamentDropDown::Rescale()
{
    need_sync = true;
    if (subDropDown != nullptr)
        subDropDown->Rescale();
}

bool FilamentDropDown::HasDismissLongTime()
{
    auto now = boost::posix_time::microsec_clock::universal_time();
    return !IsShown() && (now - dismissTime).total_milliseconds() >= 20;
}

void FilamentDropDown::PopupForParent()
{
    prepare_submenu();
    autoPosition();
    Popup(this);
}

void FilamentDropDown::prepare_submenu()
{
    if (mainDropDown != nullptr || use_flat_fallback || subDropDown != nullptr)
        return;

    const bool has_grouped_items = std::any_of(items.begin(), items.end(), [](const Item &item)
                                               {
                                                   return !item.group.IsEmpty();
                                               });
    if (!has_grouped_items)
        return;

    std::unique_ptr<FilamentDropDown> new_sub_drop_down = std::make_unique<FilamentDropDown>(items);
    new_sub_drop_down->mainDropDown            = this;
    new_sub_drop_down->mainDropDownWeak        = this;
    new_sub_drop_down->check_bitmap            = check_bitmap;
    new_sub_drop_down->text_off                = text_off;
    new_sub_drop_down->use_content_width       = true;
    new_sub_drop_down->limit_max_content_width = true;
    // One width for every group: switching groups must not resize the popup under the pointer. This value
    // wins over the two switches above and is read as "the width is fixed" by the messureSize width
    // override and the +6 padding branch, so set it here only.
    new_sub_drop_down->fixed_width_dip         = g_filament_submenu_width_dip;
    new_sub_drop_down->max_visible_rows        = 8;
    if (!new_sub_drop_down->Create(GetParent()))
    {
        BOOST_LOG_TRIVIAL(warning)
            << "Could not create the filament submenu; falling back to a flat filament list without the "
               "fixed submenu width.";
        apply_flat_fallback();
        return;
    }

    subDropDown = new_sub_drop_down.release();
    wxWeakRef<FilamentDropDown> weak_root(this);
    subDropDown->Bind(wxEVT_COMBOBOX, [weak_root](wxCommandEvent &e)
                      {
                          FilamentDropDown *root = weak_root.get();
                          if (root == nullptr)
                              return;
                          e.SetEventObject(root);
                          e.SetId(root->GetId());
                          root->GetEventHandler()->ProcessEvent(e);
                      });
}

void FilamentDropDown::apply_flat_fallback()
{
    use_flat_fallback = true;
    for (Item &item : items)
        item.group.clear();

    hover_item = -1;
    offset     = wxPoint();
    SetToolTip(wxString());
    need_sync = true;
}

void FilamentDropDown::DismissAll()
{
    dismiss_outside = false;
    if (subDropDown != nullptr)
    {
        subDropDown->dismiss_outside = false;
        subDropDown->submenu_motion_timer.Stop();
        // The child override intentionally keeps the root open while the
        // pointer is over it. An owner-driven close must close both windows.
        subDropDown->PopupWindow::Dismiss();
        subDropDown->Hide();
    }
    submenu_motion_timer.Stop();
    PopupWindow::Dismiss();
    Hide();
}

int FilamentDropDown::group_row_of(const wxString &target) const
{
    if (target.IsEmpty())
        return -1;

    const std::vector<VisibleRow> rows = visible_rows();
    for (size_t row = 0; row < rows.size(); ++row)
    {
        const VisibleRow &visible_row = rows[row];
        if (!visible_row.group_header || items[visible_row.item_index].group != target)
            continue;
        if (row > static_cast<size_t>(std::numeric_limits<int>::max()))
            return -1;
        return static_cast<int>(row);
    }
    return -1;
}

std::vector<FilamentDropDown::VisibleRow> FilamentDropDown::visible_rows() const
{
    return build_visible_rows(items, group);
}

void FilamentDropDown::ensure_row_visible(int row)
{
    if (row < 0 || rowSize.y <= 0)
        return;

    const size_t max_rows      = max_visible_row_count(max_visible_rows);
    size_t       visible_count = std::min(max_rows, std::max(count, size_t{1}));
    const int     client_height = GetClientSize().y;
    if (client_height > 0)
    {
        const size_t client_rows = static_cast<size_t>(client_height / rowSize.y);
        if (client_rows > 0)
            visible_count = std::min(visible_count, client_rows);
    }
    if (count <= visible_count)
    {
        offset.y = 0;
        return;
    }

    const size_t row_index = static_cast<size_t>(row);
    if (row_index >= count)
        return;

    const int viewport_height = multiply_to_int(rowSize.y, visible_count);
    const int content_height  = multiply_to_int(rowSize.y, count);
    const int row_top         = multiply_to_int(rowSize.y, row_index);
    const int row_bottom      = multiply_to_int(rowSize.y, row_index + 1);

    if (add_to_int(offset.y, row_top) < 0)
        offset.y = -row_top;
    else if (add_to_int(offset.y, row_bottom) > viewport_height)
        offset.y = viewport_height - row_bottom;

    const int minimum_offset = viewport_height - content_height;
    if (offset.y < minimum_offset)
        offset.y = minimum_offset;
    if (offset.y > 0)
        offset.y = 0;
}

void FilamentDropDown::ensure_selection_at_top()
{
    const int selected_row = selectedItem();
    if (selected_row < 0 || rowSize.y <= 0)
        return;

    const size_t max_rows      = max_visible_row_count(max_visible_rows);
    size_t       visible_count = std::min(max_rows, std::max(count, size_t{1}));
    const int     client_height = GetClientSize().y;
    if (client_height > 0)
    {
        const size_t client_rows = static_cast<size_t>(client_height / rowSize.y);
        if (client_rows > 0)
            visible_count = std::min(visible_count, client_rows);
    }
    if (count <= visible_count)
    {
        offset.y = 0;
        return;
    }

    const int viewport_height = multiply_to_int(rowSize.y, visible_count);
    const int content_height  = multiply_to_int(rowSize.y, count);
    const int selected_top    = multiply_to_int(rowSize.y, static_cast<size_t>(selected_row));
    const int minimum_offset  = viewport_height - content_height;
    offset.y                  = std::max(minimum_offset, -selected_top);
    if (offset.y > 0)
        offset.y = 0;
}

void FilamentDropDown::show_submenu()
{
    if (subDropDown == nullptr)
        return;

    if (!subDropDown->IsShown())
        subDropDown->Popup(subDropDown);

#ifdef __WXGTK__
    if (subDropDown->IsShown() && !subDropDown->submenu_motion_timer.IsRunning())
        subDropDown->submenu_motion_timer.Start(30, wxTIMER_CONTINUOUS);
#endif
}

void FilamentDropDown::on_submenu_motion_timer(wxTimerEvent &event)
{
#ifdef __WXGTK__
    static_cast<void>(event);
    FilamentDropDown *root = mainDropDownWeak.get();
    if (root == nullptr || !IsShown())
    {
        submenu_motion_timer.Stop();
        return;
    }

    const wxPoint mouse_pos = wxGetMousePosition();
    if (GetScreenRect().Contains(mouse_pos))
        return;

    const wxPoint      local_pt = root->ScreenToClient(mouse_pos);
    wxMouseEvent       mouse_evt(wxEVT_MOTION);
    mouse_evt.SetX(local_pt.x);
    mouse_evt.SetY(local_pt.y);
    wxPostEvent(root, mouse_evt);
#else
    event.Skip();
#endif
}

bool FilamentDropDown::openSelectionGroup()
{
    if (!group.IsEmpty() || subDropDown == nullptr || selection < 0 || static_cast<size_t>(selection) >= items.size())
        return false;

    const wxString target = items[selection].group;
    const int      row    = group_row_of(target);
    if (row < 0)
        return false;

    hover_item = row;
    ensure_row_visible(row);
    paintNow();

    auto &drop = *subDropDown;
    if (drop.group != target)
    {
        drop.setGroup(target);
        drop.messureSize();
    }
    drop.SetSelection(selection);
    drop.autoPosition();
    drop.ensure_selection_at_top();
    drop.paintNow();
    show_submenu();
    return true;
}

void FilamentDropDown::paintEvent(wxPaintEvent &evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxBufferedPaintDC dc(this);
    render(dc);
}

void FilamentDropDown::paintNow()
{
    Refresh();
}

static wxSize GetBmpSize(const wxBitmap &bmp)
{
    if (!bmp.IsOk())
        return wxSize(0, 0);
#ifdef __APPLE__
    return bmp.GetScaledSize();
#else
    return bmp.GetSize();
#endif
}

void FilamentDropDown::render(wxDC &dc)
{
    if (items.empty())
        return;

    int states = state_handler.states();
    if (subDropDown != nullptr)
        states |= subDropDown->state_handler.states();

    const wxSize size = GetSize();
    render_background(dc, size, states);

    const int selected_item = drawn_row_for_item(items, group, selection);
    const int hover_index   = hoverIndex();
    wxRect    content        = {{0, offset.y}, rowSize};
    SelectionRenderContext selection_context{size, states, selected_item, hover_index, content};
    render_selection(dc, selection_context);

    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    const wxSize text_offset = (rowSize - textSize) / 2;
    content.Deflate(0, text_offset.y);
    render_scroll_bar(dc, size, content);

    content.x += 5;
    content.width -= 5;
    if (check_bitmap.bmp().IsOk())
    {
        const wxSize bitmap_size = check_bitmap.GetBmpSize();
        if (selected_item >= 0)
        {
            wxPoint point = content.GetLeftTop();
            point.y += (content.height - bitmap_size.y) / 2;
            point.y = add_to_int(point.y, multiply_to_int(rowSize.y, static_cast<size_t>(selected_item)));
            if (point.y + bitmap_size.y > 0 && point.y < size.y)
                dc.DrawBitmap(check_bitmap.bmp(), point);
        }
        content.x += bitmap_size.x + 5;
        content.width -= bitmap_size.x + 5;
    }

    render_items(dc, size, states, content);
}

void FilamentDropDown::render_background(wxDC &dc, const wxSize &size, int states)
{
    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(GetBackgroundColour())));
    if (radius == 0)
        dc.DrawRectangle(0, 0, size.x, size.y);
    else
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, radius);
}

void FilamentDropDown::render_selection(wxDC &dc, SelectionRenderContext &context)
{
    const wxSize &size = context.size;
    if (hover_item >= 0 && (context.states & StateColor::Hovered) &&
        (context.hover_index < 0 ||
         !(items[context.hover_index].style & DD_ITEM_STYLE_DISABLED)))
    {
        context.content.y = add_to_int(context.content.y, multiply_to_int(rowSize.y, static_cast<size_t>(hover_item)));
        if (context.content.GetBottom() > 0 && context.content.y < size.y)
        {
            if (context.selected_item == hover_item)
                dc.SetBrush(wxBrush(selector_background_color.colorForStates(context.states | StateColor::Checked)));
            dc.SetPen(wxPen(selector_border_color.colorForStates(context.states)));
            context.content.Deflate(4, 1);
            dc.DrawRectangle(context.content);
            context.content.Inflate(4, 1);
        }
        context.content.y = offset.y;
    }
    if (context.selected_item >= 0 &&
        (context.selected_item != hover_item || (context.states & StateColor::Hovered) == 0))
    {
        context.content.y = add_to_int(context.content.y,
                                       multiply_to_int(rowSize.y, static_cast<size_t>(context.selected_item)));
        if (context.content.GetBottom() > 0 && context.content.y < size.y)
        {
            dc.SetBrush(wxBrush(selector_background_color.colorForStates(context.states | StateColor::Checked)));
            dc.SetPen(wxPen(selector_background_color.colorForStates(context.states)));
            context.content.Deflate(4, 1);
            dc.DrawRectangle(context.content);
            context.content.Inflate(4, 1);
        }
        context.content.y = offset.y;
    }
}

void FilamentDropDown::render_scroll_bar(wxDC &dc, const wxSize &size, wxRect &content)
{
    const int total_height = multiply_to_int(rowSize.y, count);
    if (total_height > size.y)
    {
        const int height = total_height;
        const wxRect rect = {size.x - 6, -offset.y * size.y / height, 4, size.y * size.y / height};
        dc.SetPen(wxPen(border_color.defaultColor()));
        dc.SetBrush(wxBrush(*wxLIGHT_GREY));
        dc.DrawRoundedRectangle(rect, 2);
        content.width -= 6;
    }
}

void FilamentDropDown::render_items(wxDC &dc, const wxSize &size, int states, wxRect &rcContent)
{
    const std::vector<VisibleRow> rows = visible_rows();
    for (size_t index = 0; index < rows.size(); ++index)
    {
        const VisibleRow &visible_row = rows[index];
        const Item       &item        = items[visible_row.item_index];
        int               states2     = states;
        const bool is_dimmed = (item.style & DD_ITEM_STYLE_DIMMED) != 0;
        if ((item.style & DD_ITEM_STYLE_DISABLED) != 0)
            states2 &= ~StateColor::Enabled;
        if (visible_row.group_header)
            states2 |= StateColor::Enabled;
        const bool is_hover = hover_item >= 0 && index == static_cast<size_t>(hover_item);
        if (rcContent.GetBottom() < 0)
        {
            rcContent.y += rowSize.y;
            continue;
        }
        if (rcContent.y > size.y) break;
        wxPoint pt = rcContent.GetLeftTop();

        const bool is_top_level_group = visible_row.group_header;
        auto &     icon               = item.icon;
        auto       size2              = GetBmpSize(icon);
        if (iconSize.x > 0)
        {
            if (!is_top_level_group && icon.IsOk())
            {
                pt.y += (rcContent.height - size2.y) / 2;
                dc.DrawBitmap(icon, pt);
            }
            pt.x += iconSize.x + 5;
            pt.y = rcContent.y;
        }
        else if (!is_top_level_group && icon.IsOk())
        {
            pt.y += (rcContent.height - size2.y) / 2;
            dc.DrawBitmap(icon, pt);
            pt.x += size2.x + 5;
            pt.y = rcContent.y;
        }
        // Full-row bitmap (icon height >> text height) already contains the text.
        const bool icon_fills_row = !is_top_level_group && icon.IsOk() && size2.y > textSize.y * 2;
        auto       text           = group.IsEmpty() ? (item.group.IsEmpty() ? item.text : item.group)
                                                    : strip_group_prefix(item.text, group);
        if (!text_off && !text.IsEmpty() && !icon_fills_row)
        {
            // The popup paints through a buffered DC, which arrives without the popup font while the row is
            // drawn with it: measuring first tests the overflow against a narrower font and clips the row
            // instead of eliding it.
            dc.SetFont(GetFont());
            wxSize tSize = dc.GetMultiLineTextExtent(text);
            if (pt.x + tSize.x > rcContent.GetRight())
            {
                if (is_hover && item.tip.IsEmpty())
                    SetToolTip(text);
                text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, rcContent.GetRight() - pt.x);
            }
            pt.y += (rcContent.height - textSize.y) / 2;
            dc.SetTextForeground(is_dimmed ? wxColour(0xCE, 0xCE, 0xCE) : text_color.colorForStates(states2));
            dc.DrawText(text, pt);
            if (group.IsEmpty() && !item.group.IsEmpty())
            {
                auto szBmp = arrow_bitmap.GetBmpSize();
                pt.x       = rcContent.GetRight() - szBmp.x - 5;
                pt.y       = rcContent.y + (rcContent.height - szBmp.y) / 2;
                dc.DrawBitmap(arrow_bitmap.bmp(), pt);
            }
        }
        rcContent.y += rowSize.y;
    }
}

int FilamentDropDown::hoverIndex()
{
    return item_index_for_visible_row(visible_rows(), hover_item);
}

int FilamentDropDown::selectedItem()
{
    return selected_row_for_item(items, group, selection);
}

void FilamentDropDown::messureSize()
{
    if (!need_sync) return;
    textSize = wxSize();
    iconSize = wxSize();
    count    = 0;
    wxClientDC dc(GetParent() ? GetParent() : this);
    dc.SetFont(GetFont());
    const std::vector<VisibleRow> rows = visible_rows();
    for (const VisibleRow &visible_row : rows)
    {
        const Item &item = items[visible_row.item_index];
        ++count;
        wxSize size1;
        if (!text_off)
        {
            auto text = group.IsEmpty() ? (item.group.IsEmpty() ? item.text : item.group)
                                        : strip_group_prefix(item.text, group);
            size1     = dc.GetMultiLineTextExtent(text);
            if (group.IsEmpty() && !item.group.IsEmpty())
                size1.x += 5 + arrow_bitmap.GetBmpWidth();
        }
        const bool is_top_level_group = visible_row.group_header;
        if (!is_top_level_group && item.icon.IsOk())
        {
            wxSize size2 = GetBmpSize(item.icon);
            if (size2.x > iconSize.x) iconSize = size2;
            if (!align_icon)
            {
                // Full-row bitmap (icon height >> text height): width = bitmap width only.
                if (size2.y > size1.y * 2)
                    size1.x = size2.x;
                else
                    size1.x += size2.x + (text_off ? 0 : 5);
            }
        }
        if (size1.x > textSize.x) textSize = size1;
    }
    if (!align_icon) iconSize.x = 0;
    wxSize szContent = textSize;
    szContent.x += 10;
    if (check_bitmap.bmp().IsOk())
    {
        auto szBmp = check_bitmap.GetBmpSize();
        szContent.x += szBmp.x + 5;
    }
    if (iconSize.x > 0) szContent.x += iconSize.x + (text_off ? 0 : 5);
    if (iconSize.y > szContent.y) szContent.y = iconSize.y;
    szContent.y += 10;
    const size_t max_rows = max_visible_row_count(max_visible_rows);
    if (count > max_rows)
        szContent.x = add_to_int(szContent.x, 6);
    if (GetParent() && group.IsEmpty())
    {
        auto x = GetParent()->GetSize().x;
        if (x > 0 && (!use_content_width || x > szContent.x))
            szContent.x = x;
    }
    rowSize = szContent;
    if (limit_max_content_width)
    {
        wxSize parent_size = GetParent()->GetSize();
        const int max_width = add_to_int(parent_size.x, parent_size.x);
        if (rowSize.x > max_width)
        {
            rowSize.x = max_width;
            szContent = rowSize;
        }
    }
    if (fixed_width_dip > 0)
    {
        // A fixed width outranks the content extent, the parent-width cap above and the scroll-bar padding
        // for `count > max_rows`. Scale it with the anchor's display: the popup still sits at its creation
        // position here, so its own DPI can belong to another monitor.
        rowSize.x   = GetParent()->FromDIP(fixed_width_dip);
        szContent.x = rowSize.x;
    }
    if (mainDropDown != nullptr)
    {
        // The root row is the shared vertical rhythm for both popup levels. Measure it first so
        // rescaling and font changes cannot leave the child with a stale row height.
        mainDropDown->messureSize();
        rowSize.y   = mainDropDown->rowSize.y;
        szContent.y = rowSize.y;
    }
    const size_t visible_rows = std::min(max_rows, std::max(count, size_t{1}));
    szContent.y                = multiply_to_int(szContent.y, visible_rows);
    if (count > max_rows)
        szContent.y = add_to_int(szContent.y, rowSize.y / 2);
    wxWindow::SetSize(szContent);
#ifdef __WXGTK__
    // Gtk has a wrapper window for popup widget
    // Fix for GNOME Platform 48 X11 backend: ensure size is valid before calling gtk_window_resize
    int gtk_width  = szContent.x;
    int gtk_height = szContent.y;
    if (gtk_width <= 0) gtk_width = 100;
    if (gtk_height <= 0) gtk_height = 100;
    gtk_window_resize(GTK_WINDOW(m_widget), gtk_width, gtk_height);
#endif
    need_sync = false;
}

void FilamentDropDown::position_submenu()
{
    if (mainDropDown == nullptr || GetParent() == nullptr)
        return;

    const wxRect parent_rect  = mainDropDown->GetScreenRect();
    const wxRect display_rect = wxDisplay(GetParent()).GetGeometry();
    const int    parent_left  = parent_rect.GetLeft();
    const int    parent_right = parent_left + parent_rect.GetWidth();
    const int    popup_width  = GetSize().x;
    const int    popup_height = GetSize().y;
    const int    row_index    = std::max(mainDropDown->hover_item, 0);
    const int    row_top      = add_to_int(parent_rect.GetTop(),
                                            add_to_int(multiply_to_int(mainDropDown->rowSize.y,
                                                                        static_cast<size_t>(row_index)),
                                                       mainDropDown->offset.y));
    const int right_x         = parent_right;
    const int left_x          = parent_left - popup_width;
    const int right_space     = display_rect.GetRight() + 1 - right_x;
    const int left_space      = parent_left - display_rect.GetLeft();
    int       popup_x         = right_x;
    if (right_space < popup_width && left_space >= popup_width)
        popup_x = left_x;
    else if (right_space < popup_width && left_space < popup_width && left_space > right_space)
        popup_x = left_x;

    int popup_y = row_top;
    const int display_bottom = display_rect.GetBottom() + 1;
    if (popup_y + popup_height > display_bottom)
        popup_y = std::max(display_rect.GetTop(), display_bottom - popup_height);

    const wxPoint popup_position(popup_x, popup_y);
    if (popup_position != GetPosition())
        SetPosition(popup_position);
}

void FilamentDropDown::autoPosition()
{
    messureSize();
    const size_t max_rows = max_visible_row_count(max_visible_rows);
    wxPoint pos;
    wxSize  off;
    if (mainDropDown != nullptr)
    {
        pos = mainDropDown->ClientToScreen(wxPoint(0, 0));
        pos.y = add_to_int(pos.y,
                           add_to_int(multiply_to_int(mainDropDown->rowSize.y,
                                                      static_cast<size_t>(std::max(mainDropDown->hover_item, 0))),
                                     mainDropDown->offset.y));
    }
    else
    {
        pos   = GetParent()->ClientToScreen(wxPoint(0, -6));
        off   = GetParent()->GetSize();
        off.x = 0;
        off.y += 12;
    }
    wxPoint old  = GetPosition();
    wxSize  size = GetSize();
    if (mainDropDown != nullptr)
        position_submenu();
    else
        Position(pos, off);
    if (old != GetPosition())
    {
        size              = rowSize;
        size.y             = multiply_to_int(rowSize.y, std::min(max_rows, count));
        if (count > max_rows)
            size.y = add_to_int(size.y, rowSize.y / 2);
#ifdef __WXGTK__
        if (size.x < 1) size.x = 1;
        if (size.y < 1) size.y = 1;
#endif
        if (size != GetSize())
        {
            wxWindow::SetSize(size);
            offset = wxPoint();
            if (mainDropDown != nullptr)
                position_submenu();
            else
                Position(pos, off);
        }
    }
    if (GetPosition().y > pos.y)
    {
        // may exceed
        auto drect = wxDisplay(GetParent()).GetGeometry();
        if (GetPosition().y + size.y + 10 > drect.GetBottom())
        {
            // Mirror of the scroll-bar padding in messureSize(): +6 here while the list does not scroll, +6
            // there while it does. A fixed width must survive both.
            if (use_content_width && fixed_width_dip == 0 && count <= max_rows)
                size.x = add_to_int(size.x, 6);
            size.y = drect.GetBottom() - GetPosition().y - 10;
#ifdef __WXGTK__
            if (size.y < 1) size.y = 1;
            if (size.x < 1) size.x = 1;
#endif
            wxWindow::SetSize(size);
            // Scroll by the same rule as the group-switch path. Group folding and submenu filtering
            // make the raw item index differ from the visible row index, so clamping against
            // `selection` here could push every row above the viewport and paint an empty popup.
            ensure_selection_at_top();
            const int minimum_offset = size.y - multiply_to_int(rowSize.y, count);
            if (offset.y < minimum_offset)
                offset.y = minimum_offset;
            if (offset.y > 0)
                offset.y = 0;
        }
    }

}

void FilamentDropDown::setGroup(const wxString &value)
{
    if (group == value)
        return;

    group       = value;
    hover_item  = -1;
    offset      = wxPoint();
    need_sync   = true;
    SetToolTip(wxString());
}

void FilamentDropDown::mouseDown(wxMouseEvent &event)
{
    // Receivce unexcepted LEFT_DOWN on Mac after OnDismiss
    if (!IsShown())
        return;
    // force calc hover item again
    mouseMove(event);
    pressedDown = true;
    CaptureMouse();
    dragStart = event.GetPosition();
}

void FilamentDropDown::mouseReleased(wxMouseEvent &event)
{
    if (pressedDown)
    {
        dragStart = wxPoint();
        pressedDown = false;
        if (HasCapture())
            ReleaseMouse();
        if (hover_item < 0)
            return;

        // A top-level group header opens (or focuses) the drill-down submenu instead of
        // dismissing, so a narrow one-row group is still reachable by click.
        int idx = hoverIndex();
        if (idx < -1 && subDropDown)
        {
            const wxString &target_group = items[-idx - 2].group;
            auto &          drop         = *subDropDown;
            if (drop.group != target_group)
            {
                drop.setGroup(target_group);
                drop.messureSize();
                drop.autoPosition();
                drop.paintNow();
            }
            if (!drop.IsShown())
                show_submenu();
            return;
        }

        if (hover_item >= 0 && (subDropDown == nullptr || subDropDown->group.empty())) // not moved
        {
            sendDropDownEvent();
            if (mainDropDown)
                mainDropDown->hover_item = -1; // To Dismiss mainDropDown
            DismissAndNotify();
        }
        else if (subDropDown)
            show_submenu();
    }
}

void FilamentDropDown::mouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    wxMouseEvent evt;
    mouseReleased(evt);
}

void FilamentDropDown::mouseMove(wxMouseEvent &event)
{
    wxPoint pt = event.GetPosition();
#ifdef __WXOSX__
    if (mainDropDown)
    {
        auto size = GetSize();
        if (pt.x < 0 || pt.y < 0 || pt.x >= size.x || pt.y >= size.y)
        {
            auto diff = GetPosition() - mainDropDown->GetPosition();
            event.SetX(pt.x + diff.x);
            event.SetY(pt.y + diff.y);
            mainDropDown->mouseMove(event);
            return;
        }
    }
#endif
    if (pressedDown)
    {
        wxPoint pt2 = offset + pt - dragStart;
        wxSize  size = GetSize();
        dragStart    = pt;
        if (pt2.y > 0)
            pt2.y = 0;
        else
        {
            const int total_height = multiply_to_int(rowSize.y, count);
            if (add_to_int(pt2.y, total_height) < size.y)
                pt2.y = size.y - total_height;
        }
        if (pt2.y != offset.y)
        {
            offset     = pt2;
            hover_item = -1; // moved
            // DismissAll, not Dismiss: the child's pointer guard would veto while the pointer is over the popup tree.
            if (subDropDown != nullptr && subDropDown->IsShown())
                subDropDown->DismissAll();
        }
        else
        {
            return;
        }
    }
    if (rowSize.y > 0 && (!pressedDown || hover_item >= 0))
    {
        int hover = (pt.y - offset.y) / rowSize.y;
        if (hover < 0 || static_cast<size_t>(hover) >= count)
            hover = -1;
        if (hover == hover_item) return;
        hover_item = hover;
        int index  = hoverIndex();
        if (index < -1 && subDropDown)
        {
            SetToolTip(wxString());
            auto &drop     = *subDropDown;
            drop.setGroup(items[-index - 2].group);
            drop.messureSize();
            drop.autoPosition();
            drop.paintNow();
            show_submenu();
        }
        else if (index >= 0)
        {
            if (subDropDown)
            {
                subDropDown->setGroup(wxString());
                if (subDropDown->IsShown())
                    subDropDown->Dismiss();
            }
            SetToolTip(items[index].tip);
        }
        else
        {
            SetToolTip(wxString());
        }
    }
    paintNow();
}

void FilamentDropDown::mouseWheelMoved(wxMouseEvent &event)
{
    auto    delta = event.GetWheelRotation();
    wxSize  size  = GetSize();
    wxPoint pt2   = offset + wxPoint{0, delta};
    if (pt2.y > 0)
        pt2.y = 0;
    else
    {
        const int total_height = multiply_to_int(rowSize.y, count);
        if (add_to_int(pt2.y, total_height) < size.y)
            pt2.y = size.y - total_height;
    }
    if (pt2.y != offset.y)
    {
        offset = pt2;
        // DismissAll, not Dismiss: the child's pointer guard would veto while the pointer is over the popup tree.
        if (subDropDown != nullptr && subDropDown->IsShown())
            subDropDown->DismissAll();
    }
    else
    {
        return;
    }
    int hover = (event.GetPosition().y - offset.y) / rowSize.y;
    if (hover < 0 || static_cast<size_t>(hover) >= count)
        hover = -1;
    if (hover != hover_item)
    {
        hover_item = hover;
        const int index = hoverIndex();
        if (index >= 0)
            SetToolTip(items[index].tip);
    }
    paintNow();
}

void FilamentDropDown::sendDropDownEvent()
{
    int index = hoverIndex();
    if (index < 0 || static_cast<size_t>(index) >= items.size() || (items[index].style & DD_ITEM_STYLE_DISABLED))
        return;
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(index);
    event.SetString(items[index].text);
    GetEventHandler()->ProcessEvent(event);
}

bool FilamentDropDown::ProcessLeftDown(wxMouseEvent &event)
{
#ifdef __WXOSX__
    if (IsShown() && HitTest(event.GetPosition()) == wxHT_WINDOW_OUTSIDE)
    {
        const wxPoint screen_point = ClientToScreen(event.GetPosition());
        if (point_in_anchor_gap(GetParent(), this, screen_point))
        {
            DismissAndNotify();

            // wxOSX reposts an outside click to the control below the popup. The anchor/popup gap is not
            // an activation target, so consume it after dismissing.
            return true;
        }

        // The popup handler will call DismissAndNotify() next. Force that path to release
        // native mouse capture even when the submenu is still visible.
        if (!is_pointer_over_popup_tree())
        {
            dismiss_outside = true;
        }
    }
#endif

    return PopupWindow::ProcessLeftDown(event);
}

bool FilamentDropDown::is_pointer_over_popup_tree() const
{
    const wxPoint mouse_pos = wxGetMousePosition();

    // A submenu is a sibling of the root popup, not a child of it: only its own window and the
    // shared anchor belong to its tree. Counting the root popup here kept the submenu open when
    // the pointer moved onto the root, leaving a stale list behind and blocking root-item clicks.
    if (mainDropDown != nullptr)
    {
        if (GetScreenRect().Contains(mouse_pos))
            return true;
        const wxWindow *anchor = GetParent();
        return anchor != nullptr && anchor->GetScreenRect().Contains(mouse_pos);
    }

    // Root popup: its tree is the root window, the visible submenu, and the shared anchor.
    if (GetScreenRect().Contains(mouse_pos))
        return true;
    if (subDropDown != nullptr && subDropDown->IsShown() && subDropDown->GetScreenRect().Contains(mouse_pos))
        return true;
    const wxWindow *anchor = GetParent();
    return anchor != nullptr && anchor->GetScreenRect().Contains(mouse_pos);
}

void FilamentDropDown::Dismiss()
{
    if (!dismiss_outside && is_pointer_over_popup_tree())
    {
        return;
    }
    if (dismiss_outside && subDropDown != nullptr && subDropDown->IsShown())
    {
        // The root popup is dismissed by wx's handler on an outside click. Close the
        // sibling submenu explicitly as it has its own popup handler and capture.
        subDropDown->dismiss_outside = false;
        subDropDown->PopupWindow::Dismiss();
        subDropDown->Hide();
    }
    PopupWindow::Dismiss();
}

void FilamentDropDown::OnDismiss()
{
    const bool was_dismissed_outside = dismiss_outside;
    dismiss_outside = false;
    submenu_motion_timer.Stop();
    hover_item = -1;
    SetToolTip(wxString());

    if (mainDropDown)
    {
        const wxPoint &mouse_pos = wxGetMousePosition();
        if (!mainDropDown->GetScreenRect().Contains(mouse_pos))
            mainDropDown->DismissAndNotify();
        else
#ifdef __WIN32__
            SetActiveWindow(mainDropDown->GetHandle());
#else
            ;
#endif
        return;
    }
    if (!was_dismissed_outside && is_pointer_over_popup_tree())
    {
        return;
    }
    dismissTime = boost::posix_time::microsec_clock::universal_time();
    wxCommandEvent e(EVT_DISMISS);
    GetEventHandler()->ProcessEvent(e);
}
