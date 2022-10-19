#include "ImageGrid.h"
#include "Printer/PrinterFileSystem.h"
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"

#include <wx/dcgraph.h>

#ifdef __WXMSW__
#include <shellapi.h>
#endif
#ifdef __APPLE__
#include "../Utils/MacDarkMode.hpp"
#endif

BEGIN_EVENT_TABLE(Slic3r::GUI::ImageGrid, wxPanel)

EVT_MOTION(Slic3r::GUI::ImageGrid::mouseMoved)
EVT_ENTER_WINDOW(Slic3r::GUI::ImageGrid::mouseEnterWindow)
EVT_LEAVE_WINDOW(Slic3r::GUI::ImageGrid::mouseLeaveWindow)
EVT_MOUSEWHEEL(Slic3r::GUI::ImageGrid::mouseWheelMoved)
EVT_LEFT_DOWN(Slic3r::GUI::ImageGrid::mouseDown)
EVT_LEFT_UP(Slic3r::GUI::ImageGrid::mouseReleased)
EVT_SIZE(Slic3r::GUI::ImageGrid::resize)

// catch paint events
EVT_PAINT(Slic3r::GUI::ImageGrid::paintEvent)

END_EVENT_TABLE()

namespace Slic3r {
namespace GUI {

ImageGrid::ImageGrid(wxWindow * parent)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_buttonTextColor(StateColor(
            std::make_pair(0x3B4446, (int) StateColor::Pressed),
            std::make_pair(*wxLIGHT_GREY, (int) StateColor::Hovered),
            std::make_pair(*wxWHITE, (int) StateColor::Normal)))
    , m_checked_icon(this, "check_on", 16)
    , m_unchecked_icon(this, "check_off", 16)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(0xEEEEEE);
    SetForegroundColour(*wxWHITE); // time text color
    SetFont(Label::Head_20);

    m_timer.Bind(wxEVT_TIMER, [this](auto & e) { Refresh(); });
    Rescale();
}

void ImageGrid::SetFileSystem(boost::shared_ptr<PrinterFileSystem> file_sys)
{
    if (m_file_sys) {
        m_file_sys->Unbind(EVT_MODE_CHANGED, &ImageGrid::changedEvent, this);
        m_file_sys->Unbind(EVT_FILE_CHANGED, &ImageGrid::changedEvent, this);
        m_file_sys->Unbind(EVT_THUMBNAIL, &ImageGrid::changedEvent, this);
        m_file_sys->Unbind(EVT_DOWNLOAD, &ImageGrid::changedEvent, this);
    }
    m_file_sys = file_sys;
    if (m_file_sys) {
        m_file_sys->Bind(EVT_MODE_CHANGED, &ImageGrid::changedEvent, this);
        m_file_sys->Bind(EVT_FILE_CHANGED, &ImageGrid::changedEvent, this);
        m_file_sys->Bind(EVT_THUMBNAIL, &ImageGrid::changedEvent, this);
        m_file_sys->Bind(EVT_DOWNLOAD, &ImageGrid::changedEvent, this);
    }
    m_row_count = 0;
    m_col_count = 1;
    m_row_offset = 0;
    UpdateFileSystem();
}

void ImageGrid::SetStatus(wxBitmap const & icon, wxString const &msg)
{
    int code     = m_file_sys ? m_file_sys->GetLastError() : 1;
    m_status_icon = icon;
    m_status_msg = wxString::Format(msg, code);
    BOOST_LOG_TRIVIAL(info) << "ImageGrid::SetStatus: " << m_status_msg.ToUTF8().data();
    Refresh();
}

void Slic3r::GUI::ImageGrid::SetFileType(int type)
{
    if (!m_file_sys)
        return;
    m_file_sys->SetFileType((PrinterFileSystem::FileType) type);
}

void Slic3r::GUI::ImageGrid::SetGroupMode(int mode)
{
    if (!m_file_sys || m_file_sys->GetCount() == 0)
        return;
    wxSize size = GetClientSize();
    int index = (m_row_offset + 1 < m_row_count || m_row_count == 0) 
        ? m_row_offset / 4 * m_col_count 
        : ((m_file_sys->GetCount() + m_col_count - 1) / m_col_count - (size.y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight()) * m_col_count;
    auto & file = m_file_sys->GetFile(index);
    m_file_sys->SetGroupMode((PrinterFileSystem::GroupMode) mode);
    index = m_file_sys->GetIndexAtTime(file.time);
    // UpdateFileSystem(); call by changed event
    m_row_offset = index / m_col_count * 4;
    if (m_row_offset >= m_row_count)
        m_row_offset = m_row_count == 0 ? 0 : m_row_count - 1;
}

void Slic3r::GUI::ImageGrid::SetSelecting(bool selecting)
{
    m_selecting = selecting;
    if (m_file_sys)
        m_file_sys->SelectAll(false);
    Refresh();
}

void Slic3r::GUI::ImageGrid::DoActionOnSelection(int action) { DoAction(-1, action); }

void Slic3r::GUI::ImageGrid::Rescale()
{
    UpdateFileSystem();
    auto em              = em_unit(this);
    wxSize size1{384 * em / 10, 4 * em};
    m_buttons_background = createAlphaBitmap(size1, *wxBLACK, 77, 77);
    m_buttons_background_checked = createAlphaBitmap(size1, wxColor("#FF2002"), 77, 77);
    //wxSize size2{128 * m_buttonBackgroundColor.count() * em_unit(this) / 10, 4 * em_unit(this)};
    //m_button_background = createAlphaBitmap(size2, *wxBLACK, 77, 77);
}

void Slic3r::GUI::ImageGrid::Select(size_t index)
{
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        if (m_selecting) {
            m_file_sys->ToggleSelect(index);
            Refresh();
        }
        return;
    }
    index = m_file_sys->EnterSubGroup(index);
    // UpdateFileSystem(); call by changed event
    m_row_offset = index / m_col_count * 4;
    if (m_row_offset >= m_row_count)
        m_row_offset = m_row_count == 0 ? 0 : m_row_count - 1;
    Refresh();
}

void Slic3r::GUI::ImageGrid::DoAction(size_t index, int action)
{
    if (action == 0) {
        m_file_sys->DeleteFiles(index);
    } else if (action == 1) {
        if (index != -1) {
            auto &file = m_file_sys->GetFile(index);
            if (file.IsDownload() && file.progress >= -1) {
                if (file.progress >= 100) {
                    if (!m_file_sys->DownloadCheckFile(index)) {
                        wxMessageBox(wxString::Format(_L("File '%s' was lost! Please download it again."), from_u8(file.name)), _L("Error"), wxOK);
                        Refresh();
                        return;
                    }
#ifdef __WXMSW__
                    auto wfile = boost::filesystem::path(file.path).wstring();
                    SHELLEXECUTEINFO info{sizeof(info), 0, NULL, L"open", wfile.c_str(), L"", SW_HIDE};
                    ::ShellExecuteEx(&info);
#else
                    wxShell("open " + file.path);
#endif
                } else {
                    m_file_sys->DownloadCancel(index);
                }
                return;
            }
        }
        m_file_sys->DownloadFiles(index, wxGetApp().app_config->get("download_path"));
    } else if (action == 2) {
        if (index != -1) {
            auto &file = m_file_sys->GetFile(index);
            if (file.IsDownload() && file.progress >= -1) {
                if (file.progress >= 100) {
                    if (!m_file_sys->DownloadCheckFile(index)) {
                        wxMessageBox(wxString::Format(_L("File '%s' was lost! Please download it again."), from_u8(file.name)), _L("Error"), wxOK);
                        Refresh();
                        return;
                    }
#ifdef __WIN32__
                    wxExecute(L"explorer.exe /select," + from_u8(file.path));
#elif __APPLE__
                    openFolderForFile(from_u8(file.path));
#else
#endif
                } else {
                    m_file_sys->DownloadCancel(index);
                }
                return;
            }
        }
        m_file_sys->DownloadFiles(index, wxGetApp().app_config->get("download_path"));
    }
}

void Slic3r::GUI::ImageGrid::UpdateFileSystem()
{
    if (!m_file_sys) return;
    wxSize mask_size{0, 60};
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        m_image_size.Set(384, 216);
        m_cell_size.Set(396, 228);
    }
    else {
        m_image_size.Set(480, 270);
        m_cell_size.Set(496, 296);
    }
    m_image_size = m_image_size * em_unit(this) / 10;
    m_cell_size = m_cell_size * em_unit(this) / 10;
    UpdateLayout();
}

void ImageGrid::UpdateLayout()
{
    if (!m_file_sys) return;
    wxSize size = GetClientSize();
    wxSize mask_size{0, 60 * em_unit(this) / 10};
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        mask_size.y = 20 * em_unit(this) / 10;
        size.y -= mask_size.y;
    }
    int cell_width = m_cell_size.GetWidth();
    int cell_height = m_cell_size.GetHeight();
    int ncol = (size.GetWidth() - cell_width + m_image_size.GetWidth()) / cell_width;
    if (ncol <= 0) ncol = 1;
    int total_height = (m_file_sys->GetCount() + ncol - 1) / ncol * cell_height + cell_height - m_image_size.GetHeight();
    int nrow = (total_height - size.GetHeight() + cell_height / 4 - 1) / (cell_height / 4);
    m_row_offset = m_row_offset * m_col_count / ncol;
    m_col_count = ncol;
    m_row_count = nrow > 0 ? nrow + 1 : 0;
    if (m_row_offset >= m_row_count)
        m_row_offset = m_row_count == 0 ? 0 : m_row_count - 1;
    // create mask
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        mask_size.x = (m_col_count - 1) * m_cell_size.GetWidth() + m_image_size.GetWidth();
    }
    else {
        mask_size.x = m_image_size.x;
    }
    if (!m_mask.IsOk() || m_mask.GetSize() != mask_size)
        m_mask = createAlphaBitmap(mask_size, 0x6f6f6f, 255, 0);
    UpdateFocusRange();
    Refresh();
}

void Slic3r::GUI::ImageGrid::UpdateFocusRange()
{
    wxSize  size = GetClientSize();
    wxPoint off;
    int     index = firstItem(size, off);
    int     count = m_col_count;
    while (off.y < size.y) {
        count += m_col_count;
        off.y += m_cell_size.y;
    }
    m_file_sys->SetFocusRange(index, count);
}

std::pair<int, size_t> Slic3r::GUI::ImageGrid::HitTest(wxPoint const &pt)
{
    if (!m_file_sys)
        return {HIT_NONE, -1};
    wxSize size  = GetClientSize();
    if (m_file_sys->GetCount() == 0) {
        if (wxRect({0, 0}, m_image_size).CenterIn(wxRect({0, 0}, size)).Contains(pt))
            return {HIT_STATUS, 0};
        return {HIT_NONE, -1};
    }
    wxPoint off;
    size_t index = firstItem(size, off);
    off          = pt - off;
    int n        = 0;
    while (off.x > m_cell_size.GetWidth() && n + 1 < m_col_count) {
        ++n;
        off.x -= m_cell_size.GetWidth();
    }
    if (off.x < 0 || off.x >= m_image_size.GetWidth()) { return {HIT_NONE, -1}; }
    index += n;
    while (off.y > m_cell_size.GetHeight()) {
        index += m_col_count;
        off.y -= m_cell_size.GetHeight();
    }
    if (index >= m_file_sys->GetCount()) { return {HIT_NONE, -1}; }
    if (!m_selecting) {
        wxRect  hover_rect{0, m_image_size.y - 40, m_image_size.GetWidth(), 40};
        auto & file = m_file_sys->GetFile(index);
        int    btn  = file.IsDownload() && file.progress >= 100 ? 3 : 2;
        if (hover_rect.Contains(off.x, off.y)) { return {HIT_ACTION, index * 4 + off.x * btn / hover_rect.GetWidth()}; } // Two buttons
    }
    return {HIT_ITEM, index};
}

void ImageGrid::mouseMoved(wxMouseEvent& event)
{
    if (!m_hovered || m_pressed)
        return;
    auto hit = HitTest(event.GetPosition());
    if (hit != std::make_pair(m_hit_type, m_hit_item)) {
        m_hit_type = hit.first;
        m_hit_item = hit.second;
        if (hit.first == HIT_ITEM)
            SetToolTip(m_file_sys->GetFile(hit.second).name);
        else
            SetToolTip({});
        Refresh();
    }
}

void ImageGrid::mouseEnterWindow(wxMouseEvent& event)
{
    if (!m_hovered)
        m_hovered = true;
}

void ImageGrid::mouseLeaveWindow(wxMouseEvent& event)
{
    if (m_hovered) {
        m_hovered = false;
        m_pressed = false;
        if (m_hit_type != HIT_NONE) {
            m_hit_type = HIT_NONE;
            m_hit_item = -1;
            Refresh();
        }
    }
}

void ImageGrid::mouseDown(wxMouseEvent& event)
{
    if (!m_pressed) {
        m_pressed = true;
        auto hit  = HitTest(event.GetPosition());
        m_hit_type = hit.first;
        m_hit_item = hit.second;
        if (m_hit_type >= HIT_ACTION)
            Refresh();
    }
}

void ImageGrid::mouseReleased(wxMouseEvent& event)
{
    if (!m_pressed)
        return;
    m_pressed = false;
    auto hit = HitTest(event.GetPosition());
    if (hit == std::make_pair(m_hit_type, m_hit_item)) {
        if (m_hit_type == HIT_ITEM)
            Select(m_hit_item);
        else if (m_hit_type == HIT_ACTION)
            DoAction(m_hit_item / 4, m_hit_item & 3);
        else if (m_hit_type == HIT_MODE)
            SetGroupMode(static_cast<PrinterFileSystem::GroupMode>(2 - m_hit_item));
        else if (m_hit_type == HIT_STATUS)
            m_file_sys->Retry();
        else
            Refresh();
    } else {
        Refresh();
    }
}

void ImageGrid::resize(wxSizeEvent& event)
{
    UpdateLayout();
}

void ImageGrid::mouseWheelMoved(wxMouseEvent &event)
{
    auto delta = (event.GetWheelRotation() < 0 == event.IsWheelInverted()) ? -1 : 1;
    int off = m_row_offset + delta;
    if (off >= 0 && off < m_row_count) {
        m_row_offset = off;
        m_timer.StartOnce(4000); // Show position bar
        UpdateFocusRange();
        Refresh();
    }
}

void Slic3r::GUI::ImageGrid::changedEvent(wxCommandEvent& evt)
{
    evt.Skip();
    BOOST_LOG_TRIVIAL(info) << "ImageGrid::changedEvent: " << evt.GetEventType() << " index: " << evt.GetInt() << " name: " << evt.GetString() << " extra: " << evt.GetExtraLong();
    if (evt.GetEventType() == EVT_FILE_CHANGED) {
        if (evt.GetInt() == -1)
            m_file_sys->DownloadCheckFiles(wxGetApp().app_config->get("download_path"));
        UpdateFileSystem();
    }
    else if (evt.GetEventType() == EVT_MODE_CHANGED)
        UpdateFileSystem();
    else
        Refresh();
}

void ImageGrid::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

size_t Slic3r::GUI::ImageGrid::firstItem(wxSize const &size, wxPoint &off)
{
    int size_y = size.y;
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE)
        size_y -= m_mask.GetHeight();
    int offx  = (size.x - (m_col_count - 1) * m_cell_size.GetWidth() - m_image_size.GetWidth()) / 2;
    int offy  = (m_row_offset + 1 < m_row_count || m_row_count == 0) ?
                    m_cell_size.GetHeight() - m_image_size.GetHeight() - m_row_offset * m_cell_size.GetHeight() / 4 + m_row_offset / 4 * m_cell_size.GetHeight() :
                    size_y - (size_y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight() * m_cell_size.GetHeight();
    int index = (m_row_offset + 1 < m_row_count || m_row_count == 0) ?
                    m_row_offset / 4 * m_col_count :
                    ((m_file_sys->GetCount() + m_col_count - 1) / m_col_count - (size_y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight()) * m_col_count;
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE)
        offy += m_mask.GetHeight();
    off = wxPoint{offx, offy};
    return index;
}

wxBitmap Slic3r::GUI::ImageGrid::createAlphaBitmap(wxSize size, wxColour color, int alpha1, int alpha2)
{
    wxImage image(size);
    image.InitAlpha();
    unsigned char *alpha = image.GetAlpha();
    image.SetRGB(wxRect({0, 0}, size), color.Red(), color.Green(), color.Blue());
    int d = alpha2 - alpha1;
    if (d == 0)
        memset(alpha, alpha1, size.GetWidth() * size.GetHeight());
    else
        for (int i = 0; i < size.GetHeight(); ++i)
            memset(alpha + i * size.GetWidth(), alpha1 + i * d / size.GetHeight(), size.GetWidth());
    return wxBitmap(std::move(image));
}

wxBitmap Slic3r::GUI::ImageGrid::createCircleBitmap(wxSize size, int borderWidth, int percent, wxColour fillColor, wxColour borderColor)
{
    wxImage image(size);
    image.InitAlpha();
    memset(image.GetAlpha(), 0, size.GetWidth() * size.GetHeight());
    wxBitmap   bmp(std::move(image));
    wxRect     rc(0, 0, size.x, size.y);
    wxMemoryDC memdc;
    memdc.SelectObject(bmp);
#ifdef __WXMSW__
    wxGCDC dc2(memdc);
#else
    wxDC &dc2(memdc);
#endif
    if (borderWidth && borderColor != wxTransparentColor) {
        int d = ceil(borderWidth / 2.0);
        rc.Deflate(d, d);
        dc2.SetPen(wxPen(borderColor, borderWidth));
    } else {
        dc2.SetPen(wxPen(fillColor));
    }
    dc2.SetBrush(wxBrush(fillColor));
    if (percent > 0)
        dc2.DrawEllipticArc(rc.GetTopLeft(), rc.GetSize(), 0, percent * 360.0 / 100);

    memdc.SelectObject(wxNullBitmap);
    return bmp;
}

/*
* Here we do the actual rendering. I put it in a separate
* method so that it can work no matter what type of DC
* (e.g. wxPaintDC or wxClientDC) is used.
*/
void ImageGrid::render(wxDC& dc)
{
    wxSize size = GetClientSize();
    dc.SetPen(wxPen(GetBackgroundColour()));
    dc.SetBrush(wxBrush(GetBackgroundColour()));
    if (!m_file_sys || m_file_sys->GetCount() == 0) {
        dc.DrawRectangle({ 0, 0, size.x, size.y });
        if (!m_status_msg.IsEmpty()) {
            auto   si = m_status_icon.GetSize();
            auto   st = dc.GetTextExtent(m_status_msg);
            auto   rect = wxRect{0, 0, max(st.x, si.x), si.y + 26 + st.y}.CenterIn(wxRect({0, 0}, size));
            dc.DrawBitmap(m_status_icon, rect.x + (rect.width - si.x) / 2, rect.y);
            dc.SetTextForeground(wxColor(0x909090));
            dc.DrawText(m_status_msg, rect.x + (rect.width - st.x) / 2, rect.GetBottom() - st.y);
        }
        return;
    }
    wxPoint off;
    size_t index = firstItem(size, off);
    // Draw background: left/right side
    dc.DrawRectangle({0, 0, off.x, size.y});
    dc.DrawRectangle({size.x - off.x - 1, 0, off.x + 1, size.y});
    // Draw line spacing at top
    if (off.y > 0)
        dc.DrawRectangle({0, 0, size.x, off.y});
    constexpr wchar_t const * formats[] = {_T("%Y-%m-%d"), _T("%Y-%m"), _T("%Y")};
    size_t start = index;
    size_t end = index;
    size_t hit_image = m_selecting ? size_t(-1) : m_hit_type == HIT_ITEM ? m_hit_item : m_hit_type == HIT_ACTION ? m_hit_item / 4 :size_t(-1);
    // Draw items with background
    while (off.y < size.y)
    {
        // Draw one line
        wxPoint pt{off.x, off.y};
        end = (index + m_col_count) < m_file_sys->GetCount() ? index + m_col_count : m_file_sys->GetCount();
        while (index < end) {
            auto & file = m_file_sys->GetFile(index);
            // Draw thumbnail
            if (file.thumbnail.IsOk()) {
                float hs = (float) m_image_size.GetWidth() / file.thumbnail.GetWidth();
                float vs = (float) m_image_size.GetHeight() / file.thumbnail.GetHeight();
                dc.SetUserScale(hs, vs);
                dc.DrawBitmap(file.thumbnail, {(int) (pt.x / hs), (int) (pt.y / vs)});
                dc.SetUserScale(1, 1);
                if (m_file_sys->GetGroupMode() != PrinterFileSystem::G_NONE) {
                    dc.DrawBitmap(m_mask, pt);
                }
            }
            bool show_download_state_always = true;
            // Draw checked icon
            if (m_selecting && !show_download_state_always)
                dc.DrawBitmap(file.IsSelect() ? m_checked_icon.bmp() : m_unchecked_icon.bmp(), 
                    pt + wxPoint{10, m_image_size.GetHeight() - m_checked_icon.GetBmpHeight() - 10});
            // can't handle alpha
            // dc.GradientFillLinear({pt.x, pt.y, m_image_size.GetWidth(), 60}, wxColour(0x6F, 0x6F, 0x6F, 0x99), wxColour(0x6F, 0x6F, 0x6F, 0), wxBOTTOM);
            else if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
                wxString nonHoverText;
                wxString secondAction = _L("Download");
                wxString thirdAction;
                int      states = 0;
                // Draw download progress
                if (file.IsDownload()) {
                    if (file.progress == -1) {
                        secondAction = _L("Cancel");
                        nonHoverText = _L("Download waiting...");
                    } else if (file.progress < 0) {
                        secondAction = _L("Retry");
                        nonHoverText = _L("Download failed");
                        states       = StateColor::Checked;
                    } else if (file.progress >= 100) {
                        secondAction = _L("Play");
                        thirdAction = _L("Open Folder");
                        nonHoverText = _L("Download finished");
                    } else {
                        secondAction = _L("Cancel");
                        nonHoverText = wxString::Format(_L("Downloading %d%%..."), file.progress);
                    }
                }
                // Draw buttons on hovered item
                wxRect rect{pt.x, pt.y + m_image_size.y - m_buttons_background.GetHeight(), m_image_size.GetWidth(), m_buttons_background.GetHeight()};
                if (hit_image == index) {
                    renderButtons(dc, {_L("Delete"), (wxChar const *) secondAction, thirdAction.IsEmpty() ? nullptr : (wxChar const *) thirdAction, nullptr}, rect,
                                  m_hit_type == HIT_ACTION ? m_hit_item & 3 : -1, states);
                } else if (!nonHoverText.IsEmpty()) {
                    renderButtons(dc, {(wxChar const *) nonHoverText, nullptr}, rect, -1, states);
                }
                if (m_selecting && show_download_state_always)
                    dc.DrawBitmap(file.IsSelect() ? m_checked_icon.bmp() : m_unchecked_icon.bmp(),
                                  pt + wxPoint{10, m_image_size.GetHeight() - m_checked_icon.GetBmpHeight() - 10});
            } else {
                auto date = wxDateTime((time_t) file.time).Format(_L(formats[m_file_sys->GetGroupMode()]));
                dc.DrawText(date, pt + wxPoint{24, 16});
            }
            // Draw colume spacing at right
            dc.DrawRectangle({pt.x + m_image_size.GetWidth(), pt.y, m_cell_size.GetWidth() - m_image_size.GetWidth(), m_image_size.GetHeight()});
            ++index;
            pt.x += m_cell_size.GetWidth();
        }
        // Draw line fill items
        if (end < index + m_col_count)
            dc.DrawRectangle({pt.x, pt.y, size.x - pt.x - off.x, m_image_size.GetHeight()});
        // Draw line spacing at bottom
        dc.DrawRectangle({off.x, pt.y + m_image_size.GetHeight(), size.x - off.x * 2, m_cell_size.GetHeight() - m_image_size.GetHeight()});
        off.y += m_cell_size.GetHeight();
    }
    // Draw floating date range for non-group list
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE && m_file_sys->GetCount() > 0) {
        //dc.DrawBitmap(m_mask, {off.x, 0});
        dc.DrawRectangle({off.x, 0}, m_mask.GetSize());
        auto & file1 = m_file_sys->GetFile(start);
        auto & file2 = m_file_sys->GetFile(end - 1);
        auto date1 = wxDateTime((time_t) file1.time).Format(_L(formats[m_file_sys->GetGroupMode()]));
        auto date2 = wxDateTime((time_t) file2.time).Format(_L(formats[m_file_sys->GetGroupMode()]));
        dc.SetFont(Label::Head_16);
        dc.SetTextForeground(wxColor("#262E30"));
        dc.DrawText(date1 + " - " + date2, wxPoint{off.x, 2});
    }
    // Draw bottom background
    if (off.y < size.y)
        dc.DrawRectangle({off.x, off.y, size.x - off.x * 2, size.y - off.y});
    // Draw position bar
    if (m_timer.IsRunning()) {
        int total_height = (m_file_sys->GetCount() + m_col_count - 1) / m_col_count * m_cell_size.GetHeight() + m_cell_size.GetHeight() - m_image_size.GetHeight();
        if (total_height > size.y) {
            int offset = (m_row_offset + 1 < m_row_count || m_row_count == 0) ? m_row_offset * (m_cell_size.GetHeight() / 4) : total_height - size.y;
            wxRect rect = {size.x - 16, offset * size.y / total_height, 8,
                size.y * size.y / total_height};
            dc.SetBrush(wxBrush(*wxLIGHT_GREY));
            dc.DrawRoundedRectangle(rect, 4);
        }
    }
}

void Slic3r::GUI::ImageGrid::renderButtons(wxDC &dc, wxStringList const &texts, wxRect const &rect2, size_t hit, int states)
{
    // Draw background
    {
        wxMemoryDC mdc(states & StateColor::Checked ? m_buttons_background_checked : m_buttons_background);
        dc.Blit(rect2.GetTopLeft(), rect2.GetSize(), &mdc, {0, 0});
    }
    // Draw buttons
    wxRect rect(rect2);
    rect.SetWidth(rect.GetWidth() / texts.size());
    int state = m_pressed ? StateColor::Pressed : StateColor::Hovered;
    dc.SetFont(Label::Body_14);
    //mdc.SelectObject(m_button_background);
    for (size_t i = 0; i < texts.size(); ++i) {
        int states2 = hit == i ? state : 0;
        // Draw button background
        //dc.Blit(rect.GetTopLeft(), rect.GetSize(), &mdc, {m_buttonBackgroundColor.colorIndexForStates(states) * 128, 0});
        //dc.DrawBitmap(m_button_background, rect2.GetTopLeft());
        // Draw button splitter
        if (i > 0) dc.DrawLine(rect.GetLeftTop(), rect.GetLeftBottom());
        // Draw button text
        rect.Deflate(10, 5);
        renderText(dc, texts[i], rect, states | states2);
        rect.Inflate(10, 5);
        rect.Offset(rect.GetWidth(), 0);
    }
    dc.SetTextForeground(*wxWHITE); // time text color
    dc.SetFont(GetFont());
}

void Slic3r::GUI::ImageGrid::renderText(wxDC &dc, wxString const &text, wxRect const &rect, int states)
{
    dc.SetTextForeground(m_buttonTextColor.colorForStates(states));
    wxRect rc({0, 0}, dc.GetTextExtent(text));
    rc = rc.CenterIn(rect);
    dc.DrawText(text, rc.GetTopLeft());
}

}}
