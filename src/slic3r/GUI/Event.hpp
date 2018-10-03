#ifndef slic3r_Events_hpp_
#define slic3r_Events_hpp_

#include <wx/event.h>


namespace Slic3r {

namespace GUI {


struct SimpleEvent : public wxEvent
{
    SimpleEvent(wxEventType type, int id = 0) : wxEvent(id, type) {}

    virtual wxEvent* Clone() const
    {
        return new SimpleEvent(GetEventType(), GetId());
    }
};

template<class T, size_t N> struct ArrayEvent : public wxEvent
{
    std::array<T, N> data;

    ArrayEvent(wxEventType type, std::array<T, N> data, int id = 0)
        : wxEvent(id, type), data(std::move(data))
    {}

    virtual wxEvent* Clone() const
    {
        return new ArrayEvent<T, N>(GetEventType(), data, GetId());
    }
};
template<class T> struct ArrayEvent<T, 1> : public wxEvent
{
    T data;

    ArrayEvent(wxEventType type, T data, int id = 0)
        : wxEvent(id, type), data(std::move(data))
    {}

    virtual wxEvent* Clone() const
    {
        return new ArrayEvent<T, 1>(GetEventType(), data, GetId());
    }
};

template <class T> using Event = ArrayEvent<T, 1>;


}
}

#endif // slic3r_Events_hpp_
