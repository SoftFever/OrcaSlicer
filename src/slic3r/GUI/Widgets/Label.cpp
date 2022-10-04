#include "Label.hpp"
#include "StaticBox.hpp"

wxFont Label::sysFont(int size, bool bold)
{
//#ifdef __linux__
//    return wxFont{};
//#endif
#ifndef __APPLE__
    size = size * 4 / 5;
#endif

    auto   face = wxString::FromUTF8("HarmonyOS Sans SC");
    wxFont font{size, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, face};
    font.SetFaceName(face);
    if (!font.IsOk()) {
        font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        if (bold) font.MakeBold();
        font.SetPointSize(size);
    }
    return font;
}
wxFont Label::Head_24;
wxFont Label::Head_20;
wxFont Label::Head_18;
wxFont Label::Head_16;
wxFont Label::Head_15;
wxFont Label::Head_14;
wxFont Label::Head_13;
wxFont Label::Head_12;
wxFont Label::Head_10;

wxFont Label::Body_16;
wxFont Label::Body_15;
wxFont Label::Body_14;
wxFont Label::Body_13;
wxFont Label::Body_12;
wxFont Label::Body_11;
wxFont Label::Body_10;
wxFont Label::Body_9;

void Label::initSysFont()
{
    Head_24 = Label::sysFont(24, true);
    Head_20 = Label::sysFont(20, true);
    Head_18 = Label::sysFont(18, true);
    Head_16 = Label::sysFont(16, true);
    Head_15 = Label::sysFont(15, true);
    Head_14 = Label::sysFont(14, true);
    Head_13 = Label::sysFont(13, true);
    Head_12 = Label::sysFont(12, true);
    Head_10 = Label::sysFont(10, true);

    Body_16 = Label::sysFont(16, false);
    Body_15 = Label::sysFont(15, false);
    Body_14 = Label::sysFont(14, false);
    Body_13 = Label::sysFont(13, false);
    Body_12 = Label::sysFont(12, false);
    Body_11 = Label::sysFont(11, false);
    Body_10 = Label::sysFont(10, false);
    Body_9  = Label::sysFont(9, false);
}

wxSize Label::split_lines(wxDC &dc, int width, const wxString &text, wxString &multiline_text)
{
    multiline_text = text;
    if (width > 0 && dc.GetTextExtent(text).x > width) {
        size_t start   = 0;
        while (true) {
            size_t idx = size_t(-1);
            for (size_t i = start; i < multiline_text.Len(); i++) {
                if (multiline_text[i] == ' ') {
                    if (dc.GetTextExtent(multiline_text.SubString(start, i)).x < width)
                        idx = i;
                    else {
                        if (idx == size_t(-1)) idx = i;
                        break;
                    }
                }
            }
            if (idx == size_t(-1)) break;
            multiline_text[idx] = '\n';
            start               = idx + 1;
            if (dc.GetTextExtent(multiline_text.Mid(start)).x < width) break;
        }
    }
    return dc.GetMultiLineTextExtent(multiline_text);
}

Label::Label(wxWindow *parent, wxString const &text, long style) : Label(parent, Body_14, text, style) {}

Label::Label(wxWindow *parent, wxFont const &font, wxString const &text, long style)
    : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style)
{
    this->font = font;
    SetFont(font);
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
    Bind(wxEVT_ENTER_WINDOW, [this](auto &e) {
        if (GetWindowStyle() & LB_HYPERLINK) {
            SetFont(this->font.Underlined());
            Refresh();
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) {
        SetFont(this->font);
        Refresh();
    });
}

void Label::SetWindowStyleFlag(long style)
{
    wxStaticText::SetWindowStyleFlag(style);
    if (style & LB_HYPERLINK) {
        this->color = GetForegroundColour();
        static wxColor clr_url("#00AE42");
        SetForegroundColour(clr_url);
    } else {
        SetForegroundColour(this->color);
        SetFont(this->font);
    }
    Refresh();
}
