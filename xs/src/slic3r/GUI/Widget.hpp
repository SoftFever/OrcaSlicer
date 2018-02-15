#ifndef WIDGET_HPP
#define WIDGET_HPP
#include <wx/wxprec.h>
#ifndef WX_PRECOM
#include <wx/wx.h>
#endif

class Widget {
protected:
    wxSizer* _sizer;
public:
    Widget(): _sizer(nullptr) { }
    bool valid() const { return _sizer != nullptr; } 
    wxSizer* sizer() const { return _sizer; } 
};
#endif
