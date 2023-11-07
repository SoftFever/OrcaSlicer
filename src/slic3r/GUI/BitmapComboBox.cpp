#include "BitmapComboBox.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/colordlg.h>
#include <wx/wupdlock.h>
#include <wx/menu.h>
#include <wx/odcombo.h>
#include <wx/listbook.h>
#include <wx/window.h>

#ifdef __WINDOWS__
#include <wx/msw/dcclient.h>
#include <wx/msw/private.h>
#ifdef _MSW_DARK_MODE
#include "dark_mode.hpp"
#endif //_MSW_DARK_MODE
#endif //__WINDOWS__

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

// A workaround for a set of issues related to text fitting into gtk widgets:
// See e.g.: https://github.com/prusa3d/PrusaSlicer/issues/4584
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <glib-2.0/glib-object.h>
    #include <pango-1.0/pango/pango-layout.h>
    #include <gtk/gtk.h>
#endif

using Slic3r::GUI::format_wxstr;

#define BORDER_W 10

// ---------------------------------
// ***  BitmapComboBox  ***
// ---------------------------------

namespace Slic3r {
namespace GUI {

BitmapComboBox::BitmapComboBox(wxWindow* parent,
                                wxWindowID id/* = wxID_ANY*/,
                                const wxString& value/* = wxEmptyString*/,
                                const wxPoint& pos/* = wxDefaultPosition*/,
                                const wxSize& size/* = wxDefaultSize*/,
                                int n/* = 0*/,
                                const wxString choices[]/* = NULL*/,
                                    long style/* = 0*/) :
    wxBitmapComboBox(parent, id, value, pos, size, n, choices, style)
{
    SetFont(Slic3r::GUI::wxGetApp().normal_font());
#ifdef _WIN32
    // Workaround for ignoring CBN_EDITCHANGE events, which are processed after the content of the combo box changes, so that
    // the index of the item inside CBN_EDITCHANGE may no more be valid.
    EnableTextChangedEvents(false);
    wxGetApp().UpdateDarkUI(this);
    if (!HasFlag(wxCB_READONLY))
        wxTextEntry::SetMargins(0,0);
#endif /* _WIN32 */
}

BitmapComboBox::~BitmapComboBox()
{
}

#ifdef _WIN32

int BitmapComboBox::Append(const wxString& item)
{
    // Workaround for a correct rendering of the control without Bitmap (under MSW):
    //1. We should create small Bitmap to fill Bitmaps RefData,
    //   ! in this case wxBitmap.IsOK() return true.
    //2. But then set width to 0 value for no using of bitmap left and right spacing
    //3. Set this empty bitmap to the at list one item and BitmapCombobox will be recreated correct

    wxBitmapBundle bitmap = *get_empty_bmp_bundle(1, 16);
    OnAddBitmap(bitmap);

    const int n = wxComboBox::Append(item);

    return n;
}

enum OwnerDrawnComboBoxPaintingFlags
{
    ODCB_PAINTING_DISABLED = 0x0004,
};

bool BitmapComboBox::MSWOnDraw(WXDRAWITEMSTRUCT* item)
{
    LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)item;
    int pos = lpDrawItem->itemID;

    // Draw default for item -1, which means 'focus rect only'
    if (pos == -1)
        return false;

    int flags = 0;
    if (lpDrawItem->itemState & ODS_COMBOBOXEDIT)
        flags |= wxODCB_PAINTING_CONTROL;
    if (lpDrawItem->itemState & ODS_SELECTED)
        flags |= wxODCB_PAINTING_SELECTED;
    if (lpDrawItem->itemState & ODS_DISABLED)
        flags |= ODCB_PAINTING_DISABLED;

    wxPaintDCEx dc(this, lpDrawItem->hDC);
    wxRect rect = wxRectFromRECT(lpDrawItem->rcItem);

    DrawBackground_(dc, rect, pos, flags);

    wxString text;

    if (flags & wxODCB_PAINTING_CONTROL)
    {
        // Don't draw anything in the editable selection field.
        //if (!HasFlag(wxCB_READONLY))
        //    return true;

        pos = GetSelection();
        // Skip drawing if there is nothing selected.
        if (pos < 0)
            return true;

        text = GetValue();
    }
    else
    {
        text = GetString(pos);
    }

    wxBitmapComboBoxBase::DrawItem(dc, rect, pos, text, flags);

    return true;
}

void BitmapComboBox::DrawBackground_(wxDC& dc, const wxRect& rect, int WXUNUSED(item), int flags) const
{
    if (flags & wxODCB_PAINTING_SELECTED)
    {
        const int vSizeDec = 0;  // Vertical size reduction of selection rectangle edges

        dc.SetTextForeground(wxGetApp().get_label_highlight_clr());

        wxColour selCol = wxGetApp().get_highlight_default_clr();
        dc.SetPen(selCol);
        dc.SetBrush(selCol);
        dc.DrawRectangle(rect.x,
            rect.y + vSizeDec,
            rect.width,
            rect.height - (vSizeDec * 2));
    }
    else
    {
        dc.SetTextForeground(flags & ODCB_PAINTING_DISABLED ? wxColour(108,108,108) : wxGetApp().get_label_clr_default());

        wxColour selCol = flags & ODCB_PAINTING_DISABLED ? 
//#ifdef _MSW_DARK_MODE
            //wxRGBToColour(NppDarkMode::GetSofterBackgroundColor()) :
//#else
            wxGetApp().get_highlight_default_clr() :
//#endif
            wxGetApp().get_window_default_clr();
        dc.SetPen(selCol);
        dc.SetBrush(selCol);
        dc.DrawRectangle(rect);
    }
}

void BitmapComboBox::Rescale()
{
    // Next workaround: To correct scaling of a BitmapCombobox
    // we need to refill control with new bitmaps
    const wxString selection = this->GetValue();
    std::vector<wxString> items;
    for (size_t i = 0; i < GetCount(); i++)
        items.push_back(GetString(i));

    this->Clear();
    for (const wxString& item : items)
        Append(item);
    this->SetValue(selection);
}
#endif

}}

