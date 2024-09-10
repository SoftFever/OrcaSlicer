#ifndef slic3r_GUI_SwitchButton_hpp_
#define slic3r_GUI_SwitchButton_hpp_

#include "../wxExtensions.hpp"
#include "StateColor.hpp"

#include <wx/tglbtn.h>
#include "Label.hpp"
#include "Button.hpp"

class SwitchButton : public wxBitmapToggleButton
{
public:
	SwitchButton(wxWindow * parent = NULL, wxWindowID id = wxID_ANY);

public:
	void SetLabels(wxString const & lbl_on, wxString const & lbl_off);

	void SetTextColor(StateColor const &color);

	void SetTextColor2(StateColor const &color);

    void SetTrackColor(StateColor const &color);

	void SetThumbColor(StateColor const &color);

	void SetValue(bool value) override;

	void Rescale();

private:
	void update();

private:
	ScalableBitmap m_on;
	ScalableBitmap m_off;

	wxString labels[2];
    StateColor   text_color;
    StateColor   text_color2;
	StateColor   track_color;
	StateColor   thumb_color;
};

class SwitchBoard : public wxWindow
{
public:
    SwitchBoard(wxWindow *parent = NULL, wxString leftL = "", wxString right = "", wxSize size = wxDefaultSize);
    wxString leftLabel;
    wxString rightLabel;

	void updateState(wxString target);

	bool switch_left{false};
    bool switch_right{false};

public:
    void paintEvent(wxPaintEvent &evt);
    void render(wxDC &dc);
    void doRender(wxDC &dc);
    void on_left_down(wxMouseEvent &evt);
};

#endif // !slic3r_GUI_SwitchButton_hpp_
