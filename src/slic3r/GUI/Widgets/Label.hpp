#ifndef slic3r_GUI_Label_hpp_
#define slic3r_GUI_Label_hpp_

#include <wx/stattext.h>

#define LB_HYPERLINK 0x0001


class Label : public wxStaticText
{
public:
    Label(wxWindow *parent, wxString const &text = {}, long style = 0);

	Label(wxWindow *parent, wxFont const &font, wxString const &text = {}, long style = 0);

    void SetWindowStyleFlag(long style) override;

private:
    wxFont font;
    wxColour color;

public:
	static wxFont Head_24;
	static wxFont Head_20;
	static wxFont Head_18;
	static wxFont Head_16;
	static wxFont Head_15;
	static wxFont Head_14;
	static wxFont Head_13;
	static wxFont Head_12;
    static wxFont Head_10;

	static wxFont Body_16;
	static wxFont Body_15;
	static wxFont Body_14;
    static wxFont Body_13;
	static wxFont Body_12;
	static wxFont Body_10;
	static wxFont Body_11;
	static wxFont Body_9;

    static wxFont sysFont(int size, bool bold = false);

    static wxSize split_lines(wxDC &dc, int width, const wxString &text, wxString &multiline_text);
};

#endif // !slic3r_GUI_Label_hpp_
