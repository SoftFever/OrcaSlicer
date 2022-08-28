#include "Label.hpp"
#include "StaticBox.hpp"

wxFont Label::sysFont(int size, bool bold)
{
#ifdef __linux__
    return wxFont{};
#endif
#ifdef __WIN32__
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
wxFont Label::Head_24 = Label::sysFont(24, true);
wxFont Label::Head_20 = Label::sysFont(20, true);
wxFont Label::Head_18 = Label::sysFont(18, true);
wxFont Label::Head_16 = Label::sysFont(16, true);
wxFont Label::Head_15 = Label::sysFont(15, true);
wxFont Label::Head_14 = Label::sysFont(14, true);
wxFont Label::Head_13 = Label::sysFont(13, true);
wxFont Label::Head_12 = Label::sysFont(12, true);
wxFont Label::Head_10 = Label::sysFont(10, true);

wxFont Label::Body_16 = Label::sysFont(16, false);
wxFont Label::Body_15 = Label::sysFont(15, false);
wxFont Label::Body_14 = Label::sysFont(14, false);
wxFont Label::Body_13 = Label::sysFont(13, false);
wxFont Label::Body_12 = Label::sysFont(12, false);
wxFont Label::Body_10 = Label::sysFont(10, false);
wxFont Label::Body_9 = Label::sysFont(9, false);

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

Label::Label(wxWindow *parent, wxString const &text) : Label(parent, Body_14, text) {}

Label::Label(wxWindow *parent, wxFont const &font, wxString const &text)
    : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, 0)
{
    SetFont(font);
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
}
