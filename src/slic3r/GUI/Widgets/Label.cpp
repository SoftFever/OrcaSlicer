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

Label::Label(wxString const &text, wxWindow *parent) : Label(Body_16, text, parent) {}

Label::Label(wxFont const &font, wxWindow *parent) : Label(font, "", parent) {}

Label::Label(wxFont const &font, wxString const &text, wxWindow *parent) : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, 0)
{
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
    SetFont(font);
}
