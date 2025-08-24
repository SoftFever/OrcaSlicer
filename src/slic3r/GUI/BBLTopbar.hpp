#pragma once

#include "wx/wxprec.h"
#include "wx/aui/auibar.h"

#include "SelectMachine.hpp"
#include "DeviceManager.hpp"


using namespace Slic3r::GUI;

class BBLTopbar : public wxAuiToolBar
{
public:
    BBLTopbar(wxWindow* pwin, wxFrame* parent);
    BBLTopbar(wxFrame* parent);
    void Init(wxFrame *parent);
    ~BBLTopbar();
    void UpdateToolbarWidth(int width);
    void Rescale();
    void OnIconize(wxAuiToolBarEvent& event);
    void OnFullScreen(wxAuiToolBarEvent& event);
    void OnCloseFrame(wxAuiToolBarEvent& event);
    void OnFileToolItem(wxAuiToolBarEvent& evt);
    void OnDropdownToolItem(wxAuiToolBarEvent& evt);
    void OnCalibToolItem(wxAuiToolBarEvent &evt);
    void OnMouseLeftDClock(wxMouseEvent& mouse);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);
    void OnMenuClose(wxMenuEvent& event);
    void OnOpenProject(wxAuiToolBarEvent& event);

#ifdef _WIN32
    // Accessibility keyboard navigation
    void OnCharHook(wxKeyEvent& event);
    bool HandleAccessibilityTabNavigation(bool reverse);
#endif
    void show_publish_button(bool show);
    void OnSaveProject(wxAuiToolBarEvent& event);
    void OnUndo(wxAuiToolBarEvent& event);
    void OnRedo(wxAuiToolBarEvent& event);
    void OnModelStoreClicked(wxAuiToolBarEvent& event);
    void OnPublishClicked(wxAuiToolBarEvent &event);

    wxAuiToolBarItem* FindToolByCurrentPosition();
	
    void SetFileMenu(wxMenu* file_menu);
    void AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title);
    void AddDropDownMenuItem(wxMenuItem* menu_item);
    wxMenu *GetTopMenu();
    wxMenu *GetCalibMenu();
    void SetTitle(wxString title);
    void SetMaximizedSize();
    void SetWindowSize();

    void EnableUndoRedoItems();
    void DisableUndoRedoItems();

    void SaveNormalRect();

    void ShowCalibrationButton(bool show = true);

#ifdef _WIN32
    // Windows message handling for screen reader detection
    virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
    // Accessibility setup and management
    void SetupAccessibility();
    static LRESULT CALLBACK BBLTopbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

private:
    wxFrame* m_frame;
    wxAuiToolBarItem* m_file_menu_item;
    wxAuiToolBarItem* m_dropdown_menu_item;
    wxRect m_normalRect;
    wxPoint m_delta;
    wxMenu m_top_menu;
    wxMenu* m_file_menu;
    wxMenu m_calib_menu;
    wxAuiToolBarItem* m_title_item;
    wxAuiToolBarItem* m_account_item;
    wxAuiToolBarItem* m_model_store_item;
    
    wxAuiToolBarItem *m_publish_item;
    wxAuiToolBarItem* m_undo_item;
    wxAuiToolBarItem* m_redo_item;
    wxAuiToolBarItem* m_calib_item;
    wxAuiToolBarItem* maximize_btn;

    wxBitmap m_publish_bitmap;
    wxBitmap m_publish_disable_bitmap;

    wxBitmap maximize_bitmap;
    wxBitmap window_bitmap;

    int m_toolbar_h;
    bool m_skip_popup_file_menu;
    bool m_skip_popup_dropdown_menu;
    bool m_skip_popup_calib_menu;

#ifdef _WIN32
    // Screen reader detection
    bool m_screenReaderDetected;
    
    // Accessibility focus management
    int m_focusedToolIndex;

    // Accessibility object and window subclassing
    class BBLTopbarAccessible* m_pAccessible;
    WNDPROC m_originalWndProc;
#endif
};
