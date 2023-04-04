#include "libslic3r/Utils.hpp"
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
#ifdef __linux__
    const std::string& resource_path = Slic3r::resources_dir();
    wxString font_path = wxString::FromUTF8(resource_path+"/fonts/HarmonyOS_Sans_SC_Bold.ttf");
    bool result = wxFont::AddPrivateFont(font_path);
    //BOOST_LOG_TRIVIAL(info) << boost::format("add font of HarmonyOS_Sans_SC_Bold returns %1%")%result;
    printf("add font of HarmonyOS_Sans_SC_Bold returns %d\n", result);
    font_path = wxString::FromUTF8(resource_path+"/fonts/HarmonyOS_Sans_SC_Regular.ttf");
    result = wxFont::AddPrivateFont(font_path);
    //BOOST_LOG_TRIVIAL(info) << boost::format("add font of HarmonyOS_Sans_SC_Regular returns %1%")%result;
    printf("add font of HarmonyOS_Sans_SC_Regular returns %d\n", result);
#endif

    Head_48 = Label::sysFont(48, true);
    Head_32 = Label::sysFont(32, true);
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

class WXDLLIMPEXP_CORE wxTextWrapper2
{
public:
    wxTextWrapper2() { m_eol = false; }

    // win is used for getting the font, text is the text to wrap, width is the
    // max line width or -1 to disable wrapping
    void Wrap(wxWindow *win, const wxString &text, int widthMax)
    {
        const wxClientDC dc(win);

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
    void WrapLabel(wxWindow *text, int widthMax)
    {
        m_text.clear();
        Wrap(text, text->GetLabel(), widthMax);
        text->SetLabel(m_text);
    }

protected:
    virtual void OnOutputLine(const wxString &line) wxOVERRIDE { m_text += line; }

    virtual void OnNewLine() wxOVERRIDE { m_text += wxT('\n'); }

private:
    wxString m_text;
};


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
    SetForegroundColour(wxColour("#262E30"));
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
    SetForegroundColour("#262E30");
    if (style & LB_PROPAGATE_MOUSE_EVENT) {
        for (auto evt : {
            wxEVT_LEFT_UP, wxEVT_LEFT_DOWN})
            Bind(evt, [this] (auto & e) { GetParent()->GetEventHandler()->ProcessEventLocally(e); });
        };
    }

void Label::SetLabel(const wxString& label)
{
    if (GetLabel() == label)
        return;
    wxStaticText::SetLabel(label);
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
        this->color = GetForegroundColour();
        static wxColor clr_url("#009688");
        SetFont(this->font.Underlined());
        SetForegroundColour(clr_url);
        SetCursor(wxCURSOR_HAND);
#ifdef __WXOSX__
        SetLabelMarkup(GetLabel());
#endif
    } else {
        SetForegroundColour(this->color);
        SetFont(this->font);
        SetCursor(wxCURSOR_ARROW);
#ifdef __WXOSX__
        auto label = GetLabel();
        wxStaticText::SetLabel({});
        wxStaticText::SetLabel(label);
#endif
    }
    Refresh();
}

void Label::Wrap(int width)
{
    wxLabelWrapper2 wrapper;
    wrapper.WrapLabel(this, width);
}
