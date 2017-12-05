#ifndef WIDGET_HPP
#define WIDGET_HPP
#include "wxinit.h"
class Widget {
protected:
    wxSizer* _sizer;
public:
    Widget(): _sizer(nullptr) { }
    bool valid() const { return _sizer != nullptr; } 
    wxSizer* sizer() const { return _sizer; } 
};
#endif
