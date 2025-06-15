#ifndef slic3r_GUI_CheckBox_hpp_
#define slic3r_GUI_CheckBox_hpp_

#include "../wxExtensions.hpp"

#include "Label.hpp"

#include "StateColor.hpp"

#include <string>
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/tglbtn.h> // to keep wxEVT_TOGGLEBUTTON
#include <wx/gdicmn.h> // Added for wxGCDC

class CheckBox : public wxPanel
{
public:
	CheckBox(wxWindow* parent, wxString label = wxEmptyString);

public:
	void SetValue(bool value);

    void SetHalfChecked(bool value = true){
        m_half_checked = value;
        Refresh();
    };

    bool GetValue(){return m_value;};

    bool IsChecked() const {return m_value;};

    wxStaticText* GetTextCtrl(){return m_text;};

	void Rescale();

    void SetTooltip(wxString label);

    bool Enable(bool enable = true) override {
        m_enabled = enable;
        Refresh();
        if (enable)
            AcceptsFocusFromKeyboard();
        else
            DisableFocusFromKeyboard();

        bool result = m_check->Enable(enable);
        if(m_has_text)
            m_text->Enable(enable);
        if (result)
            wxCommandEvent e(wxEVT_ACTIVATE);
        UpdateIcon();
        return result;

    };

    bool Disable(){return Enable(false);};

    bool IsEnabled(){return m_enabled;};

private:

    class FocusRect : public wxPanel {
    public:
        FocusRect(wxWindow* parent, wxStaticText* target);
        void UpdatePosition();

    private:
        wxStaticText* m_target;
    };

    void UpdateIcon();

    void OnClick();

    void DrawFocusBorder();

    wxWindow* GetScrollParent(wxWindow *pWindow);

    ScalableBitmap m_on;
    ScalableBitmap m_off;
    ScalableBitmap m_half;
    ScalableBitmap m_on_hover;
    ScalableBitmap m_off_hover;
    ScalableBitmap m_half_hover;
    ScalableBitmap m_on_disabled;
    ScalableBitmap m_off_disabled;
    ScalableBitmap m_half_disabled;
    ScalableBitmap m_on_focused;
    ScalableBitmap m_off_focused;
    ScalableBitmap m_half_focused;
    ScalableBitmap m_on_hvrfcs;
    ScalableBitmap m_off_hvrfcs;
    ScalableBitmap m_half_hvrfcs;
    bool m_half_checked = false;
    bool m_focused      = false;
    bool m_value        = false;
    bool m_enabled      = true;
    bool m_hovered      = false;
    bool m_has_text     = false;
    wxString        m_label;
    wxStaticBitmap* m_check = nullptr;
    wxStaticText*   m_text  = nullptr;
    wxFont          m_font;
    FocusRect*      m_focus_rect;
    wxColour        m_bg_track;
};

#endif // !slic3r_GUI_CheckBox_hpp_
