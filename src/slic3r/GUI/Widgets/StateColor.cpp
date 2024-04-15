#include "StateColor.hpp"

static bool gDarkMode = false;

static bool operator<(wxColour const &l, wxColour const &r) { return l.GetRGBA() < r.GetRGBA(); }

// ORCA: Added descriptions for which areas they used
static std::map<wxColour, wxColour> gDarkColors{
    {"#009688", "#00675b"}, // rgb(0, 150, 136)		ORCA color
	{"#26A69A", "#267E73"}, // rgb(38, 166, 154)	ORCA button hover color
    {"#1F8EEA", "#2778D2"},	// rgb(31, 142, 234)	???
    {"#FF6F00", "#D15B00"}, // rgb(255, 111, 0)
    {"#D01B1B", "#BB2A3A"}, // rgb(208, 27, 27)		???
    {"#262E30", "#EFEFF0"}, // rgb(38, 46, 48)		Button text color | Input Text Color
    {"#2C2C2E", "#B3B3B4"}, // rgb(44, 44, 46)		???
    {"#6B6B6B", "#818183"}, // rgb(107, 107, 107)	Disabled Text
    {"#ACACAC", "#65656A"}, // rgb(172, 172, 172)	Disabled Text | Dimmed Elements
    {"#EEEEEE", "#4C4C55"}, // rgb(238, 238, 238)	Separator Line | Title Line Color
    {"#E8E8E8", "#3E3E45"}, // rgb(232, 232, 232)	???
    {"#323A3D", "#E5E5E4"}, // rgb(50, 58, 61)		Softer text color
    {"#FFFFFF", "#2D2D31"},	// rgb(255, 255, 255)	Window background
    {"#F8F8F8", "#36363C"}, // rgb(248, 248, 248)	Sidebar > Titlebar > Gradient Top
    {"#F1F1F1", "#36363B"},	// rgb(241, 241, 241)	Sidebar > Titlebar > Gradient Bottom
    {"#3B4446", "#2D2D30"}, // rgb(59, 68, 78)		???
	{"#265C58", "#1F4947"}, // rgb(38, 92, 88)		Topbar tab background hover color
    {"#CECECE", "#54545B"},	// rgb(206, 206, 206)	???
    {"#DBFDD5", "#3B3B40"}, // rgb(219, 253, 213)	???
    {"#000000", "#FFFFFE"}, // rgb(0, 0, 0)			Mostly Text color wxBlack
    {"#F4F4F4", "#36363D"}, // rgb(244, 244, 244)	???
    {"#DBDBDB", "#4A4A51"}, // rgb(219, 219, 219)	Input Box Border Color
    {"#E5F0EE", "#283232"},	// rgb(229, 240, 238)	Combo / Dropdown focused background color
    {"#323A3C", "#E5E5E6"}, // rgb(50, 58, 60)		???
    {"#6B6B6A", "#B3B3B5"}, // rgb(107, 107, 106)	Button Dimmed text
    {"#303A3C", "#E5E5E5"}, // rgb(48, 58, 60)		Object Table > Column header text color | StaticBox Border Color
    {"#FEFFFF", "#242428"}, // rgb(254, 255, 255)	???
    {"#A6A9AA", "#2D2D29"}, // rgb(166, 169, 170)	Seperator color
    {"#363636", "#B2B3B5"}, // rgb(54, 54, 54)		Sidebar > Label color | Create Filament window text
    {"#F0F0F1", "#333337"}, // rgb(240, 240, 241)	Disabled element background
    {"#9E9E9E", "#53545A"}, // Not used
    {"#D7E8DE", "#1F2B27"}, // Not used
    {"#2B3436", "#808080"},
    //{"#ABABAB", "#ABABAB"},
    {"#D9D9D9", "#2D2D32"},
	{"#F2F2F2", "#333337"}, // ORCA: Sidebar title background. Uptated this to get better visibility on buttons
							// ORCA: This color can be used as secondary / highlight color for backgrounds
    {"#DFDFDF", "#3E3E45"}, // ORCA: Button bg color
    {"#D4D4D4", "#4D4D54"}, // ORCA: Button hover bg color
    {"#F2F3F2", "#CCCDCC"}, // ORCA: Toggle Normal Thumb
    {"#BFE1DE", "#223C3C"},	// ORCA: ORCA color with %25 opacity
    {"#62696B", "#3E3E45"}, // ORCA: Topbar > Split button > disabled background
    {"#B1B4B5", "#A3A3A6"}, // ORCA: Topbar > Split button > disabled foreground
    {"#FEFEFE", "#EFEFF0"}, // ORCA: Confirm Button text | Text on ORCA color 

    //{"#F0F0F0", "#4C4C54"},
};

std::map<wxColour, wxColour> const & StateColor::GetDarkMap() 
{
    return gDarkColors;
}

void StateColor::SetDarkMode(bool dark) { gDarkMode = dark; }

inline wxColour darkModeColorFor2(wxColour const &color)
{
    if (!gDarkMode)
        return color;
    auto iter = gDarkColors.find(color);
    wxASSERT(iter != gDarkColors.end());
    if (iter != gDarkColors.end()) return iter->second;
    return color;
}

std::map<wxColour, wxColour> revert(std::map<wxColour, wxColour> const & map)
{
    std::map<wxColour, wxColour> map2;
    for (auto &p : map) map2.emplace(p.second, p.first);
    return map2;
}

wxColour StateColor::lightModeColorFor(wxColour const &color)
{
    static std::map<wxColour, wxColour> gLightColors = revert(gDarkColors);
    auto iter = gLightColors.find(color);
    wxASSERT(iter != gLightColors.end());
    if (iter != gLightColors.end()) return iter->second;
    return color;
}

wxColour StateColor::darkModeColorFor(wxColour const &color) { return darkModeColorFor2(color); }

StateColor::StateColor(wxColour const &color) { append(color, 0); }

StateColor::StateColor(wxString const &color) { append(color, 0); }

StateColor::StateColor(unsigned long color) { append(color, 0); }

void StateColor::append(wxColour const & color, int states)
{
    statesList_.push_back(states);
    colors_.push_back(color);
}

void StateColor::append(wxString const & color, int states)
{
    wxColour c1(color);
    append(c1, states);
}

void StateColor::append(unsigned long color, int states)
{
    if ((color & 0xff000000) == 0)
        color |= 0xff000000;
    wxColour cl; cl.SetRGBA(color & 0xff00ff00 | ((color & 0xff) << 16) | ((color >> 16) & 0xff));
    append(cl, states);
}

void StateColor::clear()
{
    statesList_.clear();
    colors_.clear();
}

int StateColor::states() const
{
    int states = 0;
    for (auto s : statesList_) states |= s;
    states = (states & 0xffff) | (states >> 16);
    if (takeFocusedAsHovered_ && (states & Hovered))
        states |= Focused;
    return states;
}

wxColour StateColor::defaultColor() {
    return colorForStates(0);
}

wxColour StateColor::colorForStates(int states)
{
    bool focused = takeFocusedAsHovered_ && (states & Focused);
    for (int i = 0; i < statesList_.size(); ++i) {
        int s = statesList_[i];
        int on = s & 0xffff;
        int off = s >> 16;
        if ((on & states) == on && (off & ~states) == off) {
            return darkModeColorFor2(colors_[i]);
        }
        if (focused && (on & Hovered)) {
            on |= Focused;
            on &= ~Hovered;
            if ((on & states) == on && (off & ~states) == off) {
                return darkModeColorFor2(colors_[i]);
            }
        }
    }
    return wxColour(0, 0, 0, 0);
}

wxColour StateColor::colorForStatesNoDark(int states)
{
    bool focused = takeFocusedAsHovered_ && (states & Focused);
    for (int i = 0; i < statesList_.size(); ++i) {
        int s = statesList_[i];
        int on = s & 0xffff;
        int off = s >> 16;
        if ((on & states) == on && (off & ~states) == off) {
            return colors_[i];
        }
        if (focused && (on & Hovered)) {
            on |= Focused;
            on &= ~Hovered;
            if ((on & states) == on && (off & ~states) == off) {
                return colors_[i];
            }
        }
    }
    return wxColour(0, 0, 0, 0);
}

int StateColor::colorIndexForStates(int states)
{
    for (int i = 0; i < statesList_.size(); ++i) {
        int s   = statesList_[i];
        int on  = s & 0xffff;
        int off = s >> 16;
        if ((on & states) == on && (off & ~states) == off) { return i; }
    }
    return -1;
}

bool StateColor::setColorForStates(wxColour const &color, int states)
{
    for (int i = 0; i < statesList_.size(); ++i) {
        if (statesList_[i] == states) {
            colors_[i] = color;
            return true;
        }
    }
    return false;
}

void StateColor::setTakeFocusedAsHovered(bool set) { takeFocusedAsHovered_ = set; }
