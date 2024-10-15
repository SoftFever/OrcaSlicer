#include "libslic3r/Utils.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"

#include "../GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/dcclient.h>
#include <wx/settings.h>

wxFont Label::sysFont(int size, bool bold, std::string lang_code)
{
//#ifdef __linux__
//    return wxFont{};
//#endif
#ifndef __APPLE__
    size = size * 4 / 5;
#endif

    wxString face;
    if (lang_code == "ja") {
        face = wxString::FromUTF8("Source Han Sans JP Normal");
    } else if (lang_code == "ko") {
        face = wxString::FromUTF8("NanumGothic");
    }
    else {
        face = wxString::FromUTF8("HarmonyOS Sans SC");
    }

    wxFont font{size, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL, false, face};
    font.SetFaceName(face);
    if (!font.IsOk()) {
        font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        if (bold) font.MakeBold();
        font.SetPointSize(size);
    }

    return font;
}
wxFont Label::Head_48;
wxFont Label::Head_32;
wxFont Label::Head_24;
wxFont Label::Head_20;
wxFont Label::Head_18;
wxFont Label::Head_16;
wxFont Label::Head_15;
wxFont Label::Head_14;
wxFont Label::Head_13;
wxFont Label::Head_12;
wxFont Label::Head_11;
wxFont Label::Head_10;

wxFont Label::Body_16;
wxFont Label::Body_15;
wxFont Label::Body_14;
wxFont Label::Body_13;
wxFont Label::Body_12;
wxFont Label::Body_11;
wxFont Label::Body_10;
wxFont Label::Body_9;
wxFont Label::Body_8;

void Label::initSysFont(std::string lang_code, bool load_font_resource)
{
#ifdef __linux__
    if (load_font_resource) {
        const std::string& resource_path = Slic3r::resources_dir();
        wxString font_path = wxString::FromUTF8(resource_path+"/fonts/HarmonyOS_Sans_SC_Bold.ttf");
        bool result = wxFont::AddPrivateFont(font_path);
        //BOOST_LOG_TRIVIAL(info) << boost::format("add font of HarmonyOS_Sans_SC_Bold returns %1%")%result;
        printf("add font of HarmonyOS_Sans_SC_Bold returns %d\n", result);
        font_path = wxString::FromUTF8(resource_path+"/fonts/HarmonyOS_Sans_SC_Regular.ttf");
        result = wxFont::AddPrivateFont(font_path);
        //BOOST_LOG_TRIVIAL(info) << boost::format("add font of HarmonyOS_Sans_SC_Regular returns %1%")%result;
        printf("add font of HarmonyOS_Sans_SC_Regular returns %d\n", result);
    }
#endif
    Head_48 = Label::sysFont(48, true, lang_code);
    Head_32 = Label::sysFont(32, true, lang_code);
    Head_24 = Label::sysFont(24, true, lang_code);
    Head_20 = Label::sysFont(20, true, lang_code);
    Head_18 = Label::sysFont(18, true, lang_code);
    Head_16 = Label::sysFont(16, true, lang_code);
    Head_15 = Label::sysFont(15, true, lang_code);
    Head_14 = Label::sysFont(14, true, lang_code);
    Head_13 = Label::sysFont(13, true, lang_code);
    Head_12 = Label::sysFont(12, true, lang_code);
    Head_11 = Label::sysFont(11, true, lang_code);
    Head_10 = Label::sysFont(10, true, lang_code);

    Body_16 = Label::sysFont(16, false, lang_code);
    Body_15 = Label::sysFont(15, false, lang_code);
    Body_14 = Label::sysFont(14, false, lang_code);
    Body_13 = Label::sysFont(13, false, lang_code);
    Body_12 = Label::sysFont(12, false, lang_code);
    Body_11 = Label::sysFont(11, false, lang_code);
    Body_10 = Label::sysFont(10, false, lang_code);
    Body_9  = Label::sysFont(9, false, lang_code);
    Body_8  = Label::sysFont(8, false, lang_code);
}

class WXDLLIMPEXP_CORE wxTextWrapper2
{
public:
    wxTextWrapper2() { m_eol = false; }

    // win is used for getting the font, text is the text to wrap, width is the
    // max line width or -1 to disable wrapping
    void Wrap(wxWindow *win, const wxString &text, int widthMax)
    {
        const wxClientDC dc(win);
        Wrap(dc, text, widthMax);
    }

    void Wrap(wxDC const & dc, const wxString &text, int widthMax)
    {
        const wxArrayString ls = wxSplit(text, '\n', '\0');
        for (wxArrayString::const_iterator i = ls.begin(); i != ls.end(); ++i) {
            wxString line = *i;

            if (i != ls.begin()) {
                // Do this even if the line is empty, except if it's the first one.
                OnNewLine();
            }

            // Is this a special case when wrapping is disabled?
            if (widthMax < 0) {
                DoOutputLine(line);
                continue;
            }

            for (bool newLine = false; !line.empty(); newLine = true) {
                if (newLine) OnNewLine();

                if (1 == line.length()) {
                    DoOutputLine(line);
                    break;
                }

                wxArrayInt widths;
                dc.GetPartialTextExtents(line, widths);

                const size_t posEnd = std::lower_bound(widths.begin(), widths.end(), widthMax) - widths.begin();

                // Does the entire remaining line fit?
                if (posEnd == line.length()) {
                    DoOutputLine(line);
                    break;
                }

                // Find the last word to chop off.
                size_t lastSpace = posEnd;
                while (lastSpace > 0) {
                    auto c = line[lastSpace];
                    if (c == ' ')
                        break;
                    if (c > 0x4E00) {
                        if (lastSpace != posEnd)
                            ++lastSpace;
                        break;
                    }
                    --lastSpace;
                }
                if (lastSpace == 0) {
                    // No spaces, so can't wrap.
                    lastSpace = posEnd;
                }
                if (lastSpace == 0) {
                    // Break at least one char
                    lastSpace = 1;
                }

                // Output the part that fits.
                DoOutputLine(line.substr(0, lastSpace));

                // And redo the layout with the rest.
                if (line[lastSpace] == ' ') ++lastSpace;
                line = line.substr(lastSpace);
            }
        }
    }

    // we don't need it, but just to avoid compiler warnings
    virtual ~wxTextWrapper2() {}

protected:
    // line may be empty
    virtual void OnOutputLine(const wxString &line) = 0;

    // called at the start of every new line (except the very first one)
    virtual void OnNewLine() {}

private:
    // call OnOutputLine() and set m_eol to true
    void DoOutputLine(const wxString &line)
    {
        OnOutputLine(line);

        m_eol = true;
    }

    // this function is a destructive inspector: when it returns true it also
    // resets the flag to false so calling it again wouldn't return true any
    // more
    bool IsStartOfNewLine()
    {
        if (!m_eol) return false;

        m_eol = false;

        return true;
    }

    bool m_eol;
};

class wxLabelWrapper2 : public wxTextWrapper2
{
public:
    void WrapLabel(wxDC const & dc, wxString const & label, int widthMax)
    {
        m_text.clear();
        Wrap(dc, label, widthMax);
    }

    void WrapLabel(wxWindow *text, wxString const & label, int widthMax)
    {
        m_text.clear();
        Wrap(text, label, widthMax);
    }

    wxString GetText() const { return m_text; }

protected:
    virtual void OnOutputLine(const wxString &line) wxOVERRIDE { m_text += line; }

    virtual void OnNewLine() wxOVERRIDE { m_text += wxT('\n'); }

private:
    wxString m_text;
};


wxSize Label::split_lines(wxDC &dc, int width, const wxString &text, wxString &multiline_text)
{
    wxLabelWrapper2 wrap;
    wrap.Wrap(dc, text, width);
    multiline_text = wrap.GetText();
    return dc.GetMultiLineTextExtent(multiline_text);
}

Label::Label(wxWindow *parent, wxString const &text, long style) : Label(parent, Body_14, text, style) {}

Label::Label(wxWindow *parent, wxFont const &font, wxString const &text, long style)
    : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style)
{
    this->m_font = font;
    this->m_text = text;
    SetFont(font);
    SetForegroundColour(*wxBLACK);
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
    SetForegroundColour("#262E30");
    if (style & LB_PROPAGATE_MOUSE_EVENT) {
        for (auto evt : { wxEVT_LEFT_UP, wxEVT_LEFT_DOWN })
            Bind(evt, [this] (auto & e) { GetParent()->GetEventHandler()->ProcessEventLocally(e); });
    };
    if (style & LB_AUTO_WRAP) {
        Bind(wxEVT_SIZE, &Label::OnSize, this);
        Wrap(GetSize().x);
    }
}

void Label::SetLabel(const wxString& label)
{
    if (m_text == label)
        return;
    m_text = label;
    if ((GetWindowStyle() & LB_AUTO_WRAP)) {
        Wrap(GetSize().x);
    } else {
        wxStaticText::SetLabel(label);
    }
#ifdef __WXOSX__
    if ((GetWindowStyle() & LB_HYPERLINK)) {
        SetLabelMarkup(label);
        return;
    }
#endif
}

void Label::SetWindowStyleFlag(long style)
{
    if (style == GetWindowStyle())
        return;
    wxStaticText::SetWindowStyleFlag(style);
    if (style & LB_HYPERLINK) {
        this->m_color = GetForegroundColour();
        static wxColor clr_url("#00AE42");
        SetFont(this->m_font.Underlined());
        SetForegroundColour(clr_url);
        SetCursor(wxCURSOR_HAND);
#ifdef __WXOSX__
        SetLabelMarkup(m_text);
#endif
    } else {
        SetForegroundColour(this->m_color);
        SetFont(this->m_font);
        SetCursor(wxCURSOR_ARROW);
#ifdef __WXOSX__
        wxStaticText::SetLabel({});
        SetLabel(m_text);
#endif
    }
    Refresh();
}

void Label::Wrap(int width)
{
    wxLabelWrapper2 wrapper;
    wrapper.Wrap(this, m_text, width);
    m_skip_size_evt = true;
    wxStaticText::SetLabel(wrapper.GetText());
    m_skip_size_evt = false;
}

void Label::OnSize(wxSizeEvent &evt)
{
    evt.Skip();
    if (m_skip_size_evt) return;
    Wrap(evt.GetSize().x);
}
