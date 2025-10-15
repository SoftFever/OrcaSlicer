#include "BBLTopbar.hpp"
#include "wx/artprov.h"
#include "wx/aui/framemanager.h"
#include "wx/display.h"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "WebViewDialog.hpp"
#include "PartPlate.hpp"

#include <boost/log/trivial.hpp>

#ifdef _WIN32
// Windows accessibility headers and COM interface support
#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleacc.h>
#include <comdef.h>
#include <winerror.h>
#include <objbase.h>

// Defines Windows accessibility constants
#ifndef STATE_SYSTEM_NORMAL
#define STATE_SYSTEM_NORMAL 0x00000000
#endif

#ifndef CHILDID_SELF
#define CHILDID_SELF 0
#endif

// Defines COM constants
#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif

#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif

#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif

#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif

#endif

#define TOPBAR_ICON_SIZE  18
#define TOPBAR_TITLE_WIDTH  300

using namespace Slic3r;

enum CUSTOM_ID
{
    ID_TOP_MENU_TOOL = 3100,
    ID_LOGO,
    ID_TOP_FILE_MENU,
    ID_TOP_DROPDOWN_MENU,
    ID_TITLE,
    ID_MODEL_STORE,
    ID_PUBLISH,
    ID_CALIB,
    ID_TOOL_BAR = 3200,
    ID_AMS_NOTEBOOK,
};

class BBLTopbarArt : public wxAuiDefaultToolBarArt
{
public:
    virtual void DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) wxOVERRIDE;
    virtual void DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) wxOVERRIDE;
    virtual void DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) wxOVERRIDE;
};

void BBLTopbarArt::DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect)
{
    dc.SetFont(m_font);
#ifdef __WINDOWS__
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
#else
    dc.SetTextForeground(*wxWHITE);
#endif

    int textWidth = 0, textHeight = 0;
    dc.GetTextExtent(item.GetLabel(), &textWidth, &textHeight);

    wxRect clipRect = rect;
    clipRect.width -= 1;
    dc.SetClippingRegion(clipRect);

    int textX, textY;
    if (textWidth < rect.GetWidth()) {
        textX = rect.x + 1 + (rect.width - textWidth) / 2;
    }
    else {
        textX = rect.x + 1;
    }
    textY = rect.y + (rect.height - textHeight) / 2;
    dc.DrawText(item.GetLabel(), textX, textY);
    dc.DestroyClippingRegion();
}

void BBLTopbarArt::DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect)
{
    dc.SetBrush(wxBrush(wxColour(38, 46, 48)));
    wxRect clipRect = rect;
    clipRect.y -= 8;
    clipRect.height += 8;
    dc.SetClippingRegion(clipRect);
    dc.DrawRectangle(rect);
    dc.DestroyClippingRegion();
}

void BBLTopbarArt::DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect)
{
    int textWidth = 0, textHeight = 0;

    if (m_flags & wxAUI_TB_TEXT)
    {
        dc.SetFont(m_font);
        int tx, ty;

        dc.GetTextExtent(wxT("ABCDHgj"), &tx, &textHeight);
        textWidth = 0;
        dc.GetTextExtent(item.GetLabel(), &textWidth, &ty);
    }

    int bmpX = 0, bmpY = 0;
    int textX = 0, textY = 0;

    const wxBitmap& bmp = item.GetState() & wxAUI_BUTTON_STATE_DISABLED
        ? item.GetDisabledBitmap()
        : item.GetBitmap();

    const wxSize bmpSize = bmp.IsOk() ? bmp.GetScaledSize() : wxSize(0, 0);

    if (m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM)
    {
        bmpX = rect.x +
            (rect.width / 2) -
            (bmpSize.x / 2);

        bmpY = rect.y +
            ((rect.height - textHeight) / 2) -
            (bmpSize.y / 2);

        textX = rect.x + (rect.width / 2) - (textWidth / 2) + 1;
        textY = rect.y + rect.height - textHeight - 1;
    }
    else if (m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT)
    {
        bmpX = rect.x + wnd->FromDIP(3);

        bmpY = rect.y +
            (rect.height / 2) -
            (bmpSize.y / 2);

        textX = bmpX + wnd->FromDIP(3) + bmpSize.x;
        textY = rect.y +
            (rect.height / 2) -
            (textHeight / 2);
    }


    if (!(item.GetState() & wxAUI_BUTTON_STATE_DISABLED))
    {
        if (item.GetState() & wxAUI_BUTTON_STATE_PRESSED)
        {
            dc.SetPen(wxPen(StateColor::darkModeColorFor("#009688"))); // ORCA
            dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#009688"))); // ORCA
            dc.DrawRectangle(rect);
        }
        else if ((item.GetState() & wxAUI_BUTTON_STATE_HOVER) || item.IsSticky())
        {
            dc.SetPen(wxPen(StateColor::darkModeColorFor("#009688"))); // ORCA
            dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#009688"))); // ORCA

            // draw an even lighter background for checked item hovers (since
            // the hover background is the same color as the check background)
            if (item.GetState() & wxAUI_BUTTON_STATE_CHECKED)
                dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#009688"))); // ORCA

            dc.DrawRectangle(rect);
        }
        else if (item.GetState() & wxAUI_BUTTON_STATE_CHECKED)
        {
            // it's important to put this code in an else statement after the
            // hover, otherwise hovers won't draw properly for checked items
            dc.SetPen(wxPen(StateColor::darkModeColorFor("#009688"))); // ORCA
            dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#009688"))); // ORCA
            dc.DrawRectangle(rect);
        }
    }

    if (bmp.IsOk())
        dc.DrawBitmap(bmp, bmpX, bmpY, true);

    // set the item's text color based on if it is disabled
#ifdef __WINDOWS__
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
#else
    dc.SetTextForeground(*wxWHITE);
#endif
    if (item.GetState() & wxAUI_BUTTON_STATE_DISABLED)
    {
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    }

    if ((m_flags & wxAUI_TB_TEXT) && !item.GetLabel().empty())
    {
        dc.DrawText(item.GetLabel(), textX, textY);
    }
}

#ifdef _WIN32
// Windows accessibility COM interface implementation for BBL toolbar
class BBLTopbarAccessible : public IAccessible
{
private:
    ULONG m_refCount;
    wxAuiToolBar* m_pToolBar;
    BBLTopbar* m_pBBLTopbar;
    long m_currentFocusedChild;

public:
    BBLTopbarAccessible(wxAuiToolBar* pToolBar, BBLTopbar* pBBLTopbar)
        : m_refCount(1), m_pToolBar(pToolBar), m_pBBLTopbar(pBBLTopbar), m_currentFocusedChild(CHILDID_SELF)
    {
        printf("BBLTopbarAccessible object created\n");
        fflush(stdout);
    }

    virtual ~BBLTopbarAccessible()
    {
        printf("BBLTopbarAccessible object destroyed\n");
        fflush(stdout);
    }

    // COM interface methods for reference counting and interface querying
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible)
        {
            *ppv = static_cast<IAccessible*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_refCount);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        ULONG refCount = InterlockedDecrement(&m_refCount);
        if (refCount == 0)
            delete this;
        return refCount;
    }

    // IDispatch methods
    STDMETHODIMP GetTypeInfoCount(UINT* pctinfo) { *pctinfo = 0; return S_OK; }
    STDMETHODIMP GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) { return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId) { return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr) { return E_NOTIMPL; }

    // IAccessible interface implementation for screen reader support
    STDMETHODIMP get_accParent(IDispatch** ppdispParent) { *ppdispParent = NULL; return S_FALSE; }
    STDMETHODIMP get_accChildCount(long* pcountChildren) { *pcountChildren = 9; return S_OK; }
    STDMETHODIMP get_accChild(VARIANT varChild, IDispatch** ppdispChild) { *ppdispChild = NULL; return S_FALSE; }
    
    // Provides accessible names for toolbar elements to screen readers
    STDMETHODIMP get_accName(VARIANT varChild, BSTR* pszName)
    {
        if (varChild.vt == VT_I4)
        {
            // Maps child index to toolbar element names
            switch (varChild.lVal)
            {
                case 1: *pszName = SysAllocString(L"File"); break;
                case 2: *pszName = SysAllocString(L"Menu"); break;
                case 3: *pszName = SysAllocString(L"Save"); break;
                case 4: *pszName = SysAllocString(L"Undo"); break;
                case 5: *pszName = SysAllocString(L"Redo"); break;
                case 6: *pszName = SysAllocString(L"Calibration"); break;
                case 7: *pszName = SysAllocString(L"Minimize"); break;
                case 8: *pszName = SysAllocString(L"Maximize"); break;
                case 9: *pszName = SysAllocString(L"Close"); break;
                default: *pszName = SysAllocString(L"BBLTopbar"); break;
            }
        }
        else
        {
            *pszName = SysAllocString(L"BBLTopbar");
        }
        return S_OK;
    }

    STDMETHODIMP get_accValue(VARIANT varChild, BSTR* pszValue) { *pszValue = NULL; return S_FALSE; }
    STDMETHODIMP get_accDescription(VARIANT varChild, BSTR* pszDescription) { *pszDescription = NULL; return S_FALSE; }
    STDMETHODIMP get_accRole(VARIANT varChild, VARIANT* pvarRole) 
    { 
        pvarRole->vt = VT_I4; 
        pvarRole->lVal = ROLE_SYSTEM_TOOLBAR; 
        return S_OK; 
    }
    STDMETHODIMP get_accState(VARIANT varChild, VARIANT* pvarState) 
    { 
        pvarState->vt = VT_I4; 
        pvarState->lVal = STATE_SYSTEM_NORMAL; 
        return S_OK; 
    }
    STDMETHODIMP get_accHelp(VARIANT varChild, BSTR* pszHelp) { *pszHelp = NULL; return S_FALSE; }
    STDMETHODIMP get_accHelpTopic(BSTR* pszHelpFile, VARIANT varChild, long* pidTopic) { return E_NOTIMPL; }
    STDMETHODIMP get_accKeyboardShortcut(VARIANT varChild, BSTR* pszKeyboardShortcut) { *pszKeyboardShortcut = NULL; return S_FALSE; }
    
    // Returns currently focused toolbar element to screen readers
    STDMETHODIMP get_accFocus(VARIANT* pvarChild)
    {
        printf("BBLTopbarAccessible::get_accFocus returning focused child %ld\n", m_currentFocusedChild);
        fflush(stdout);
        pvarChild->vt = VT_I4;
        pvarChild->lVal = m_currentFocusedChild;
        return S_OK;
    }

    STDMETHODIMP get_accSelection(VARIANT* pvarChildren) { pvarChildren->vt = VT_EMPTY; return S_FALSE; }
    STDMETHODIMP get_accDefaultAction(VARIANT varChild, BSTR* pszDefaultAction) { *pszDefaultAction = SysAllocString(L"Click"); return S_OK; }
    STDMETHODIMP accSelect(long flagsSelect, VARIANT varChild) { return S_OK; }
    // Provides screen coordinates of toolbar for screen reader positioning
    STDMETHODIMP accLocation(long* pxLeft, long* pyTop, long* pcxWidth, long* pcyHeight, VARIANT varChild) 
    { 
        if (m_pToolBar) {
            wxRect rect = m_pToolBar->GetRect();
            *pxLeft = rect.x;
            *pyTop = rect.y;
            *pcxWidth = rect.width;
            *pcyHeight = rect.height;
        }
        return S_OK; 
    }
    STDMETHODIMP accNavigate(long navDir, VARIANT varStart, VARIANT* pvarEndUpAt) { pvarEndUpAt->vt = VT_EMPTY; return S_FALSE; }
    STDMETHODIMP accHitTest(long xLeft, long yTop, VARIANT* pvarChild) { pvarChild->vt = VT_EMPTY; return S_FALSE; }
    STDMETHODIMP accDoDefaultAction(VARIANT varChild) { return S_OK; }
    STDMETHODIMP put_accName(VARIANT varChild, BSTR szName) { return E_NOTIMPL; }
    STDMETHODIMP put_accValue(VARIANT varChild, BSTR szValue) { return E_NOTIMPL; }

    // Updates the currently focused child element
    void SetCurrentFocusedChild(long childId)
    {
        m_currentFocusedChild = childId;
        printf("BBLTopbarAccessible focused child set to %ld\n", childId);
        fflush(stdout);
    }
};
#endif

BBLTopbar::BBLTopbar(wxFrame* parent) 
    : wxAuiToolBar(parent, ID_TOOL_BAR, wxDefaultPosition, wxDefaultSize, wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT)
{ 
    FILE* debugFile = fopen("C:\\temp\\bbltopbar_debug.txt", "a");
    if (debugFile) { fprintf(debugFile, "BBLTopbar constructor (wxFrame*) called - this=%p\n", this); fflush(debugFile); fclose(debugFile); }
    Init(parent);
    debugFile = fopen("C:\\temp\\bbltopbar_debug.txt", "a");
    if (debugFile) { fprintf(debugFile, "BBLTopbar constructor (wxFrame*) completed\n"); fflush(debugFile); fclose(debugFile); }
}

BBLTopbar::BBLTopbar(wxWindow* pwin, wxFrame* parent)
    : wxAuiToolBar(pwin, ID_TOOL_BAR, wxDefaultPosition, wxDefaultSize, wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT) 
{ 
    FILE* debugFile = fopen("C:\\temp\\bbltopbar_debug.txt", "a");
    if (debugFile) { fprintf(debugFile, "BBLTopbar constructor (wxWindow*, wxFrame*) called - this=%p\n", this); fflush(debugFile); fclose(debugFile); }
    Init(parent);
    debugFile = fopen("C:\\temp\\bbltopbar_debug.txt", "a");
    if (debugFile) { fprintf(debugFile, "BBLTopbar constructor (wxWindow*, wxFrame*) completed\n"); fflush(debugFile); fclose(debugFile); }
}

void BBLTopbar::Init(wxFrame* parent) 
{
    // Write debug information to file to track crash location
    FILE* debugFile = fopen("C:\\temp\\bbltopbar_debug.txt", "w");
    if (debugFile) {
        fprintf(debugFile, "BBLTopbar::Init() START - parent=%p, this=%p\n", parent, this);
        fflush(debugFile);
    }
    
    if (debugFile) { fprintf(debugFile, "Setting art provider\n"); fflush(debugFile); }
    SetArtProvider(new BBLTopbarArt());
    
    if (debugFile) { fprintf(debugFile, "Setting member variables\n"); fflush(debugFile); }
    m_frame = parent;
    m_skip_popup_file_menu = false;
    m_skip_popup_dropdown_menu = false;
    m_skip_popup_calib_menu    = false;

    if (debugFile) { fprintf(debugFile, "Calling wxInitAllImageHandlers()\n"); fflush(debugFile); }
    wxInitAllImageHandlers();

    if (debugFile) { fprintf(debugFile, "Adding initial spacer\n"); fflush(debugFile); }
    this->AddSpacer(5);

    if (debugFile) { fprintf(debugFile, "Creating logo bitmap\n"); fflush(debugFile); }
    // Logo bitmap creation occurs at silent crash
    // wxBitmap logo_bitmap = create_scaled_bitmap("topbar_logo", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Adding logo tool\n"); fflush(debugFile); }
    // wxAuiToolBarItem* logo_item = this->AddTool(ID_LOGO, "", logo_bitmap);
    // logo_item->SetHoverBitmap(logo_bitmap);
    // logo_item->SetActive(false);
    
    if (debugFile) { fprintf(debugFile, "About to call create_scaled_bitmap for file\n"); fflush(debugFile); }
    wxBitmap file_bitmap = create_scaled_bitmap("topbar_file", nullptr, TOPBAR_ICON_SIZE);
    
    if (debugFile) { fprintf(debugFile, "Adding file tool\n"); fflush(debugFile); }
    m_file_menu_item = this->AddTool(ID_TOP_FILE_MENU, _L("File"), file_bitmap, wxEmptyString, wxITEM_NORMAL);
    
    if (debugFile) { fprintf(debugFile, "Setting foreground color\n"); fflush(debugFile); }
    this->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
    
    if (debugFile) { fprintf(debugFile, "Adding spacer\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(5));
    
    if (debugFile) { fprintf(debugFile, "Creating dropdown bitmap\n"); fflush(debugFile); }
    wxBitmap dropdown_bitmap = create_scaled_bitmap("topbar_dropdown", nullptr, TOPBAR_ICON_SIZE);
    
    if (debugFile) { fprintf(debugFile, "Adding dropdown tool\n"); fflush(debugFile); }
    m_dropdown_menu_item = this->AddTool(ID_TOP_DROPDOWN_MENU, "", dropdown_bitmap, wxEmptyString);

    if (debugFile) { fprintf(debugFile, "Adding spacer, separator, spacer sequence\n"); fflush(debugFile); }
    
    this->AddSpacer(FromDIP(5));
    this->AddSeparator();
    this->AddSpacer(FromDIP(5));
    
    wxBitmap open_bitmap = create_scaled_bitmap("topbar_open", nullptr, TOPBAR_ICON_SIZE);
    wxAuiToolBarItem* tool_item = this->AddTool(wxID_OPEN, "", open_bitmap);

    if (debugFile) { fprintf(debugFile, "Adding 10 DIP spacer\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(10));
    
    if (debugFile) { fprintf(debugFile, "Creating save bitmap\n"); fflush(debugFile); }
    wxBitmap save_bitmap = create_scaled_bitmap("topbar_save", nullptr, TOPBAR_ICON_SIZE);
    
    if (debugFile) { fprintf(debugFile, "Adding save tool\n"); fflush(debugFile); }
    wxAuiToolBarItem* save_btn = this->AddTool(wxID_SAVE, "", save_bitmap);
    
    if (debugFile) { fprintf(debugFile, "Adding 10 DIP spacer before undo\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(10));
    
    if (debugFile) { fprintf(debugFile, "Creating undo bitmap\n"); fflush(debugFile); }
    wxBitmap undo_bitmap = create_scaled_bitmap("topbar_undo", nullptr, TOPBAR_ICON_SIZE);
    
    if (debugFile) { fprintf(debugFile, "create_scaled_bitmap for undo completed\n"); fflush(debugFile); }
    
    if (debugFile) { fprintf(debugFile, "Adding undo tool\n"); fflush(debugFile); }
    m_undo_item = this->AddTool(wxID_UNDO, "", undo_bitmap);
    
    if (debugFile) { fprintf(debugFile, "Creating undo inactive bitmap\n"); fflush(debugFile); }
    wxBitmap undo_inactive_bitmap = create_scaled_bitmap("topbar_undo_inactive", nullptr, TOPBAR_ICON_SIZE);
    
    if (debugFile) { fprintf(debugFile, "Setting undo disabled bitmap\n"); fflush(debugFile); }
    m_undo_item->SetDisabledBitmap(undo_inactive_bitmap);
    
    if (debugFile) { fprintf(debugFile, "Adding spacer before redo\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(10));

    if (debugFile) { fprintf(debugFile, "Creating redo bitmap\n"); fflush(debugFile); }
    wxBitmap redo_bitmap = create_scaled_bitmap("topbar_redo", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Adding redo tool\n"); fflush(debugFile); }
    m_redo_item = this->AddTool(wxID_REDO, "", redo_bitmap);
    if (debugFile) { fprintf(debugFile, "Creating redo inactive bitmap\n"); fflush(debugFile); }
    wxBitmap redo_inactive_bitmap = create_scaled_bitmap("topbar_redo_inactive", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Setting redo disabled bitmap\n"); fflush(debugFile); }
    m_redo_item->SetDisabledBitmap(redo_inactive_bitmap);

    if (debugFile) { fprintf(debugFile, "Adding spacer before calib\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(10));

    if (debugFile) { fprintf(debugFile, "Creating calibration bitmaps\n"); fflush(debugFile); }
    wxBitmap calib_bitmap          = create_scaled_bitmap("calib_sf", nullptr, TOPBAR_ICON_SIZE);
    wxBitmap calib_bitmap_inactive = create_scaled_bitmap("calib_sf_inactive", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Adding calibration tool\n"); fflush(debugFile); }
    m_calib_item                   = this->AddTool(ID_CALIB, _L("Calibration"), calib_bitmap);
    if (debugFile) { fprintf(debugFile, "Setting calibration disabled bitmap\n"); fflush(debugFile); }
    m_calib_item->SetDisabledBitmap(calib_bitmap_inactive);

    if (debugFile) { fprintf(debugFile, "Adding spacer and stretch spacer\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(10));
    this->AddStretchSpacer(1);

    if (debugFile) { fprintf(debugFile, "Adding title label\n"); fflush(debugFile); }
    m_title_item = this->AddLabel(ID_TITLE, "", FromDIP(TOPBAR_TITLE_WIDTH));
    m_title_item->SetAlignment(wxALIGN_CENTRE);

    if (debugFile) { fprintf(debugFile, "Adding spacer and stretch spacer after title\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(10));
    this->AddStretchSpacer(1);

    if (debugFile) { fprintf(debugFile, "Creating publish bitmaps\n"); fflush(debugFile); }
    m_publish_bitmap = create_scaled_bitmap("topbar_publish", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Adding publish tool\n"); fflush(debugFile); }
    m_publish_item = this->AddTool(ID_PUBLISH, "", m_publish_bitmap);
    if (debugFile) { fprintf(debugFile, "Creating publish disable bitmap\n"); fflush(debugFile); }
    m_publish_disable_bitmap = create_scaled_bitmap("topbar_publish_disable", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Setting publish disabled bitmap\n"); fflush(debugFile); }
    m_publish_item->SetDisabledBitmap(m_publish_disable_bitmap);
    if (debugFile) { fprintf(debugFile, "Enabling/disabling publish tool\n"); fflush(debugFile); }
    this->EnableTool(m_publish_item->GetId(), false);
    this->AddSpacer(FromDIP(4));

    /*wxBitmap model_store_bitmap = create_scaled_bitmap("topbar_store", nullptr, TOPBAR_ICON_SIZE);
    m_model_store_item = this->AddTool(ID_MODEL_STORE, "", model_store_bitmap);
    this->AddSpacer(12);
    */

    //this->AddSeparator();
    if (debugFile) { fprintf(debugFile, "Adding final spacer\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(4));

    if (debugFile) { fprintf(debugFile, "Creating iconize bitmap\n"); fflush(debugFile); }
    wxBitmap iconize_bitmap = create_scaled_bitmap("topbar_min", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Adding iconize tool\n"); fflush(debugFile); }
    wxAuiToolBarItem* iconize_btn = this->AddTool(wxID_ICONIZE_FRAME, "", iconize_bitmap);

    if (debugFile) { fprintf(debugFile, "Adding spacer before maximize\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(4));

    if (debugFile) { fprintf(debugFile, "Creating maximize/window bitmaps\n"); fflush(debugFile); }
    maximize_bitmap = create_scaled_bitmap("topbar_max", nullptr, TOPBAR_ICON_SIZE);
    window_bitmap = create_scaled_bitmap("topbar_win", nullptr, TOPBAR_ICON_SIZE);
    if (debugFile) { fprintf(debugFile, "Adding maximize tool (checking frame state)\n"); fflush(debugFile); }
    if (m_frame->IsMaximized()) {
        maximize_btn = this->AddTool(wxID_MAXIMIZE_FRAME, "", window_bitmap);
    }
    else {
        maximize_btn = this->AddTool(wxID_MAXIMIZE_FRAME, "", maximize_bitmap);
    }

    if (debugFile) { fprintf(debugFile, "Adding spacer before close\n"); fflush(debugFile); }
    this->AddSpacer(FromDIP(4));

    if (debugFile) { fprintf(debugFile, "Creating close bitmap\n"); fflush(debugFile); }
    wxBitmap close_bitmap = create_scaled_bitmap("topbar_close", nullptr, TOPBAR_ICON_SIZE);
    
    if (debugFile) { fprintf(debugFile, "Adding close tool\n"); fflush(debugFile); }
    wxAuiToolBarItem* close_btn = this->AddTool(wxID_CLOSE_FRAME, "", close_bitmap);
    
    if (debugFile) { fprintf(debugFile, "Calling Realize()\n"); fflush(debugFile); }
    Realize();
    
    if (debugFile) { fprintf(debugFile, "Setting toolbar dimensions\n"); fflush(debugFile); }
    // Sets toolbar dimensions
    m_toolbar_h = FromDIP(30);
    int client_w = parent->GetClientSize().GetWidth();
    this->SetSize(client_w, m_toolbar_h);

    if (debugFile) { fprintf(debugFile, "Binding mouse events\n"); fflush(debugFile); }
    // Binds mouse events for window dragging
    this->Bind(wxEVT_MOTION, &BBLTopbar::OnMouseMotion, this);
    this->Bind(wxEVT_MOUSE_CAPTURE_LOST, &BBLTopbar::OnMouseCaptureLost, this);
    this->Bind(wxEVT_MENU_CLOSE, &BBLTopbar::OnMenuClose, this);
    
    if (debugFile) { fprintf(debugFile, "Binding toolbar tool events\n"); fflush(debugFile); }
    // Binds toolbar tool events
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnFileToolItem, this, ID_TOP_FILE_MENU);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnDropdownToolItem, this, ID_TOP_DROPDOWN_MENU);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnCalibToolItem, this, ID_CALIB);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnIconize, this, wxID_ICONIZE_FRAME);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnFullScreen, this, wxID_MAXIMIZE_FRAME);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnCloseFrame, this, wxID_CLOSE_FRAME);
    
    if (debugFile) { fprintf(debugFile, "Binding mouse click events\n"); fflush(debugFile); }
    // Binds mouse click events
    this->Bind(wxEVT_LEFT_DCLICK, &BBLTopbar::OnMouseLeftDClock, this);
    this->Bind(wxEVT_LEFT_DOWN, &BBLTopbar::OnMouseLeftDown, this);
    this->Bind(wxEVT_LEFT_UP, &BBLTopbar::OnMouseLeftUp, this);
    
    if (debugFile) { fprintf(debugFile, "Binding remaining toolbar events\n"); fflush(debugFile); }
    // Binds remaining toolbar events  
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnOpenProject, this, wxID_OPEN);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnSaveProject, this, wxID_SAVE);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnRedo, this, wxID_REDO);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnUndo, this, wxID_UNDO);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnPublishClicked, this, ID_PUBLISH);

    if (debugFile) { fprintf(debugFile, "Event binding completed, initializing accessibility\n"); fflush(debugFile); }

#ifdef _WIN32
    // Initializes Windows accessibility support for screen readers
    if (debugFile) { fprintf(debugFile, "Setting accessibility variables\n"); fflush(debugFile); }
    m_screenReaderDetected = false;
    m_focusedToolIndex = 1;
    /*
    if (debugFile) { fprintf(debugFile, "BBLTopbar screen reader detection initialized\n"); fflush(debugFile); }
    
    // Setup Windows accessibility interface
    if (debugFile) { fprintf(debugFile, "About to call SetupAccessibility()\n"); fflush(debugFile); }
    SetupAccessibility();
    if (debugFile) { fprintf(debugFile, "SetupAccessibility() completed\n"); fflush(debugFile); }
    
    // Binds keyboard event for accessibility Tab navigatoin
    this->Bind(wxEVT_CHAR_HOOK, &BBLTopbar::OnCharHook, this);
    if (debugFile) { fprintf(debugFile, "wxEVT_CHAR_HOOK event bound successfully\n"); fflush(debugFile); }
    */
#endif
    if (debugFile) { fprintf(debugFile, "BBLTopbar::Init() COMPLETED\n"); fflush(debugFile); }
    if (debugFile) { fclose(debugFile); }
}

BBLTopbar::~BBLTopbar()
{
#ifdef _WIN32
    // Clean up Windows accessibility resources
    if (m_pAccessible) {
        delete m_pAccessible;
        m_pAccessible = nullptr;
    }
    
    // Restores original window procedure and cleans up accessibility properties
    if (m_originalWndProc && GetHWND()) {
        SetWindowLongPtr((HWND)GetHWND(), GWLP_WNDPROC, (LONG_PTR)m_originalWndProc);
        RemoveProp((HWND)GetHWND(), L"AccessibleObjectFromWindow");
    }
#endif
    
    m_file_menu_item = nullptr;
    m_dropdown_menu_item = nullptr;
    m_file_menu = nullptr;
}

void BBLTopbar::OnOpenProject(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->load_project();
}

void BBLTopbar::show_publish_button(bool show)
{
    this->EnableTool(m_publish_item->GetId(), show);
    Refresh();
}

void BBLTopbar::OnSaveProject(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->save_project();
}

void BBLTopbar::OnUndo(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->undo();
}

void BBLTopbar::OnRedo(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->redo();
}

void BBLTopbar::EnableUndoRedoItems()
{
    this->EnableTool(m_undo_item->GetId(), true);
    this->EnableTool(m_redo_item->GetId(), true);
    this->EnableTool(m_calib_item->GetId(), true);
    Refresh();
}

void BBLTopbar::DisableUndoRedoItems()
{
    this->EnableTool(m_undo_item->GetId(), false);
    this->EnableTool(m_redo_item->GetId(), false);
    this->EnableTool(m_calib_item->GetId(), false);
    Refresh();
}

void BBLTopbar::SaveNormalRect()
{
    m_normalRect = m_frame->GetRect();
}

void BBLTopbar::ShowCalibrationButton(bool show)
{
    m_calib_item->GetSizerItem()->Show(show);
    m_sizer->Layout();
    if (!show)
        m_calib_item->GetSizerItem()->SetDimension({-1000, 0}, {0, 0});
    Refresh();
}

void BBLTopbar::OnModelStoreClicked(wxAuiToolBarEvent& event)
{
    //GUI::wxGetApp().load_url(wxString(wxGetApp().app_config->get_web_host_url() + MODEL_STORE_URL));
}

void BBLTopbar::OnPublishClicked(wxAuiToolBarEvent& event)
{
    if (!wxGetApp().getAgent()) {
        BOOST_LOG_TRIVIAL(info) << "publish: no agent";
        return;
    }

    //no more check
    //if (GUI::wxGetApp().plater()->model().objects.empty()) return;

#ifdef ENABLE_PUBLISHING
    wxGetApp().plater()->show_publish_dialog();
#endif
    wxGetApp().open_publish_page_dialog();
}

void BBLTopbar::SetFileMenu(wxMenu* file_menu)
{
    m_file_menu = file_menu;
}

void BBLTopbar::AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title)
{
    m_top_menu.AppendSubMenu(sub_menu, title);
}

void BBLTopbar::AddDropDownMenuItem(wxMenuItem* menu_item)
{
    m_top_menu.Append(menu_item);
}

wxMenu* BBLTopbar::GetTopMenu()
{
    return &m_top_menu;
}

wxMenu* BBLTopbar::GetCalibMenu()
{
    return &m_calib_menu;
}

void BBLTopbar::SetTitle(wxString title)
{
    wxGCDC dc(this);
    title = wxControl::Ellipsize(title, dc, wxELLIPSIZE_END, FromDIP(TOPBAR_TITLE_WIDTH));

    m_title_item->SetLabel(title);
    m_title_item->SetAlignment(wxALIGN_CENTRE);
    this->Refresh();
}

void BBLTopbar::SetMaximizedSize()
{
    maximize_btn->SetBitmap(maximize_bitmap);
}

void BBLTopbar::SetWindowSize()
{
    maximize_btn->SetBitmap(window_bitmap);
}

void BBLTopbar::UpdateToolbarWidth(int width)
{
    this->SetSize(width, m_toolbar_h);
}

void BBLTopbar::Rescale() {
    int em = em_unit(this);
    wxAuiToolBarItem* item;

    /*item = this->FindTool(ID_LOGO);
    item->SetBitmap(create_scaled_bitmap("topbar_logo", nullptr, TOPBAR_ICON_SIZE));*/

    item = this->FindTool(ID_TOP_FILE_MENU);
    item->SetBitmap(create_scaled_bitmap("topbar_file", this, TOPBAR_ICON_SIZE));

    item = this->FindTool(ID_TOP_DROPDOWN_MENU);
    item->SetBitmap(create_scaled_bitmap("topbar_dropdown", this, TOPBAR_ICON_SIZE));

    //item = this->FindTool(wxID_OPEN);
    //item->SetBitmap(create_scaled_bitmap("topbar_open", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_SAVE);
    item->SetBitmap(create_scaled_bitmap("topbar_save", this, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_UNDO);
    item->SetBitmap(create_scaled_bitmap("topbar_undo", this, TOPBAR_ICON_SIZE));
    item->SetDisabledBitmap(create_scaled_bitmap("topbar_undo_inactive", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_REDO);
    item->SetBitmap(create_scaled_bitmap("topbar_redo", this, TOPBAR_ICON_SIZE));
    item->SetDisabledBitmap(create_scaled_bitmap("topbar_redo_inactive", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(ID_CALIB);
    item->SetBitmap(create_scaled_bitmap("calib_sf", nullptr, TOPBAR_ICON_SIZE));
    item->SetDisabledBitmap(create_scaled_bitmap("calib_sf_inactive", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(ID_TITLE);

    /*item = this->FindTool(ID_PUBLISH);
    item->SetBitmap(create_scaled_bitmap("topbar_publish", this, TOPBAR_ICON_SIZE));
    item->SetDisabledBitmap(create_scaled_bitmap("topbar_publish_disable", nullptr, TOPBAR_ICON_SIZE));*/

    /*item = this->FindTool(ID_MODEL_STORE);
    item->SetBitmap(create_scaled_bitmap("topbar_store", this, TOPBAR_ICON_SIZE));
    */

    item = this->FindTool(wxID_ICONIZE_FRAME);
    item->SetBitmap(create_scaled_bitmap("topbar_min", this, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_MAXIMIZE_FRAME);
    maximize_bitmap = create_scaled_bitmap("topbar_max", this, TOPBAR_ICON_SIZE);
    window_bitmap   = create_scaled_bitmap("topbar_win", this, TOPBAR_ICON_SIZE);
    if (m_frame->IsMaximized()) {
        item->SetBitmap(window_bitmap);
    }
    else {
        item->SetBitmap(maximize_bitmap);
    }

    item = this->FindTool(wxID_CLOSE_FRAME);
    item->SetBitmap(create_scaled_bitmap("topbar_close", this, TOPBAR_ICON_SIZE));

    Realize();
}

void BBLTopbar::OnIconize(wxAuiToolBarEvent& event)
{
    m_frame->Iconize();
}

void BBLTopbar::OnFullScreen(wxAuiToolBarEvent& event)
{
    if (m_frame->IsMaximized()) {
        m_frame->Restore();
    }
    else {
        m_normalRect = m_frame->GetRect();
        m_frame->Maximize();
    }
}

void BBLTopbar::OnCloseFrame(wxAuiToolBarEvent& event)
{
    m_frame->Close();
}

void BBLTopbar::OnMouseLeftDClock(wxMouseEvent& mouse)
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    // check whether mouse is not on any tool item
    if (this->FindToolByCurrentPosition() != NULL &&
        this->FindToolByCurrentPosition() != m_title_item) {
        mouse.Skip();
        return;
    }
#ifdef __W1XMSW__
    ::PostMessage((HWND) m_frame->GetHandle(), WM_NCLBUTTONDBLCLK, HTCAPTION, MAKELPARAM(mouse_pos.x, mouse_pos.y));
    return;
#endif //  __WXMSW__

    wxAuiToolBarEvent evt;
    OnFullScreen(evt);
}

void BBLTopbar::OnFileToolItem(wxAuiToolBarEvent& evt)
{
    wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

    tb->SetToolSticky(evt.GetId(), true);

    if (!m_skip_popup_file_menu) {
        GetParent()->PopupMenu(m_file_menu, wxPoint(FromDIP(1), this->GetSize().GetHeight() - 2));
    }
    else {
        m_skip_popup_file_menu = false;
    }

    // make sure the button is "un-stuck"
    tb->SetToolSticky(evt.GetId(), false);
}

void BBLTopbar::OnDropdownToolItem(wxAuiToolBarEvent& evt)
{
    wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

    tb->SetToolSticky(evt.GetId(), true);

    if (!m_skip_popup_dropdown_menu) {
        GetParent()->PopupMenu(&m_top_menu, wxPoint(FromDIP(1), this->GetSize().GetHeight() - 2));
    }
    else {
        m_skip_popup_dropdown_menu = false;
    }

    // make sure the button is "un-stuck"
    tb->SetToolSticky(evt.GetId(), false);
}

void BBLTopbar::OnCalibToolItem(wxAuiToolBarEvent &evt)
{
    wxAuiToolBar *tb = static_cast<wxAuiToolBar *>(evt.GetEventObject());

    tb->SetToolSticky(evt.GetId(), true);

    if (!m_skip_popup_calib_menu) {
        auto rec = this->GetToolRect(ID_CALIB);
        GetParent()->PopupMenu(&m_calib_menu, wxPoint(rec.GetLeft(), this->GetSize().GetHeight() - 2));
    } else {
        m_skip_popup_calib_menu = false;
    }

    // make sure the button is "un-stuck"
    tb->SetToolSticky(evt.GetId(), false);
}

void BBLTopbar::OnMouseLeftDown(wxMouseEvent& event)
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    wxPoint frame_pos = m_frame->GetScreenPosition();
    m_delta = mouse_pos - frame_pos;

    if (FindToolByCurrentPosition() == NULL 
        || this->FindToolByCurrentPosition() == m_title_item)
    {
        CaptureMouse();
#ifdef __WXMSW__
        ReleaseMouse();
        ::PostMessage((HWND) m_frame->GetHandle(), WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(mouse_pos.x, mouse_pos.y));
        return;
#endif //  __WXMSW__
    }
    
    event.Skip();
}

void BBLTopbar::OnMouseLeftUp(wxMouseEvent& event)
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    if (HasCapture())
    {
        ReleaseMouse();
    }

    event.Skip();
}

void BBLTopbar::OnMouseMotion(wxMouseEvent& event)
{
    wxPoint mouse_pos = ::wxGetMousePosition();

    if (!HasCapture()) {
        //m_frame->OnMouseMotion(event);

        event.Skip();
        return;
    }

    if (event.Dragging() && event.LeftIsDown())
    {
        // leave max state and adjust position 
        if (m_frame->IsMaximized()) {
            wxRect rect = m_frame->GetRect();
            // Filter unexcept mouse move
            if (m_delta + rect.GetLeftTop() != mouse_pos) {
                m_delta = mouse_pos - rect.GetLeftTop();
                m_delta.x = m_delta.x * m_normalRect.width / rect.width;
                m_delta.y = m_delta.y * m_normalRect.height / rect.height;
                m_frame->Restore();
            }
        }
        m_frame->Move(mouse_pos - m_delta);
    }
    event.Skip();
}

void BBLTopbar::OnMouseCaptureLost(wxMouseCaptureLostEvent& event)
{
}

void BBLTopbar::OnMenuClose(wxMenuEvent& event)
{
    wxAuiToolBarItem* item = this->FindToolByCurrentPosition();
    if (item == m_file_menu_item) {
        m_skip_popup_file_menu = true;
    }
    else if (item == m_dropdown_menu_item) {
        m_skip_popup_dropdown_menu = true;
    }
}

wxAuiToolBarItem* BBLTopbar::FindToolByCurrentPosition()
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    wxPoint client_pos = this->ScreenToClient(mouse_pos);
    return this->FindToolByPosition(client_pos.x, client_pos.y);
}

#ifdef _WIN32
// Windows message handler override for accessibility support
WXLRESULT BBLTopbar::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // Detects screen reader activity through WM_GETOBJECT messages
    if (nMsg == WM_GETOBJECT) {
        if (!m_screenReaderDetected) {
            m_screenReaderDetected = true;
            printf("BBLTopbar - Screen reader detected via WM_GETOBJECT\n");
            fflush(stdout);
        }
    }
    
    return wxAuiToolBar::MSWWindowProc(nMsg, wParam, lParam);
}

// Handles keyboard input for accessibility navigation
void BBLTopbar::OnCharHook(wxKeyEvent& event)
{
    printf("BBLTopbar::OnCharHook called with key code: %d\n", event.GetKeyCode());
    printf("BBLTopbar m_screenReaderDetected = %s\n", m_screenReaderDetected ? "true" : "false");
    fflush(stdout);
    
    // Only process accessibility navigation when hte screen reader is active
    if (!m_screenReaderDetected) {
        printf("BBLTopbar OnCharHook - Screen reader not detected, skipping\n");
        fflush(stdout);
        event.Skip();
        return;
    }
    
    // Handles Tab key for toolbar navigation
    if (event.GetKeyCode() == WXK_TAB) {
        bool reverse = event.ShiftDown();
        printf("BBLTopbar Tab key detected, reverse=%s\n", reverse ? "true" : "false");
        fflush(stdout);
        
        if (HandleAccessibilityTabNavigation(reverse)) {
            printf("BBLTopbar OnCharHook - Tab navigation handled successfully\n");
            fflush(stdout);
            return;  // Events handled, don't propagate
        } else {
            printf("BBLTopbar OnCharHook - Tab navigation failed\n");
            fflush(stdout);
        }
    }
    
    // Passes through unhandled events
    printf("BBLTopbar OnCharHook - Skipping event (key not handled or Tab navigation failed)\n");
    fflush(stdout);
    event.Skip();
    printf("BBLTopbar OnCharHook COMPLETED\n");
    fflush(stdout);
}

// Handles Tab navigation between toolbar elements for accessibility
bool BBLTopbar::HandleAccessibilityTabNavigation(bool reverse)
{
    printf("BBLTopbar HandleAccessibilityTabNavigation called, current focus: %d, reverse: %s\n", 
           m_focusedToolIndex, reverse ? "true" : "false");
    fflush(stdout);
    
    // Calculates next focus index with wrapping
    int nextIndex;
    if (reverse) {
        // Navigates to previous tool (Shift+Tab)
        nextIndex = (m_focusedToolIndex <= 1) ? 9 : m_focusedToolIndex - 1;
        printf("Shift+Tab navigation: %d -> %d\n", m_focusedToolIndex, nextIndex);
    } else {
        // Navigates to next tool (Tab)
        nextIndex = (m_focusedToolIndex >= 9) ? 1 : m_focusedToolIndex + 1;
        printf("Tab navigation: %d -> %d\n", m_focusedToolIndex, nextIndex);
    }
    
    m_focusedToolIndex = nextIndex;
    
    // Tool names for debugging output
    const char* toolNames[] = {
        "", "File", "Menu", "Save", "Undo", "Redo", "Calibration", "Minimize", "Maximize", "Exit"
    };
    
    printf("BBLTopbar focused on tool %d: %s\n", m_focusedToolIndex, 
           m_focusedToolIndex >= 1 && m_focusedToolIndex <= 9 ? toolNames[m_focusedToolIndex] : "Unknown");
    fflush(stdout);
    
    // Updatea accessibility object and notifies Windows accessibility system
    if (m_pAccessible) {
        m_pAccessible->SetCurrentFocusedChild(m_focusedToolIndex);
        
        if (GetHWND()) {
            NotifyWinEvent(EVENT_OBJECT_FOCUS, (HWND)GetHWND(), OBJID_CLIENT, m_focusedToolIndex);
        }
    } else {
        printf("ERROR - BBLTopbar m_pAccessible is NULL!\n");
        fflush(stdout);
    }
    
    printf("BBLTopbar HandleAccessibilityTabNavigation COMPLETED successfully\n");
    fflush(stdout);
    return true;
}

// Initializes Windows accessibility interface for screen reader support
void BBLTopbar::SetupAccessibility()
{
    printf("BBLTopbar::SetupAccessibility() called\n");
    fflush(stdout);
    
    // Initializes accessibility state
    m_screenReaderDetected = false;
    m_pAccessible = NULL;
    m_originalWndProc = NULL;
    
    // Creates COM accessibility object
    m_pAccessible = new BBLTopbarAccessible(this, this);
    
    printf("BBLTopbarAccessible object created at %p\n", m_pAccessible);
    fflush(stdout);
    
    // Subclasses window to intercept accessibility messages
    HWND hwnd = (HWND)GetHWND();
    if (hwnd)
    {
        printf("BBLTopbar HWND: %p\n", hwnd);
        fflush(stdout);
        
        // Installs custom window procedure for WM_GETOBJECT handling
        m_originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)BBLTopbarWndProc);
        
        printf("Window subclassed, original WndProc: %p\n", m_originalWndProc);
        fflush(stdout);
        
        // Stores object pointer for window procedure access
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
        
        // Marks window as accessibility-enabled
        SetProp(hwnd, L"AccessibleObjectFromWindow", (HANDLE)1);
        printf("Window property set for accessibility\n");
        fflush(stdout);
    } else {
        printf("ERROR - Could not get HWND for BBLTopbar\n");
        fflush(stdout);
    }
    
    printf("BBLTopbar::SetupAccessibility() COMPLETED\n");
    fflush(stdout);
}

// Custom window procedure for handling accessibility messages
LRESULT CALLBACK BBLTopbar::BBLTopbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Retrieves BBLTopbar object from window user data
    BBLTopbar* pThis = (BBLTopbar*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    if (msg == WM_GETOBJECT)
    {
        printf("BBLTopbarWndProc received WM_GETOBJECT\n");
        fflush(stdout);
        
        // Sets screen reader detection flag on first accessibility request
        if (pThis && !pThis->m_screenReaderDetected) {
            pThis->m_screenReaderDetected = true;
            printf("BBLTopbar - Screen reader detected via WM_GETOBJECT in WndProc\n");
            fflush(stdout);
        }
        
        // Returns accessibility object for client area requests
        if (lParam == OBJID_CLIENT && pThis && pThis->m_pAccessible)
        {
            printf("OBJID_CLIENT requested, returning IAccessible object\n");
            fflush(stdout);
            
            LRESULT result = LresultFromObject(IID_IAccessible, wParam, pThis->m_pAccessible);
            printf("LresultFromObject returned: %lld\n", (long long)result);
            fflush(stdout);
            
            return result;
        }
        else
        {
            printf("Non-OBJID_CLIENT request, returning 0\n");
            fflush(stdout);
            return 0;
        }
    }
    
    // Forwards all other messages to original window procedure
    if (pThis && pThis->m_originalWndProc)
    {
        return CallWindowProc(pThis->m_originalWndProc, hwnd, msg, wParam, lParam);
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif
