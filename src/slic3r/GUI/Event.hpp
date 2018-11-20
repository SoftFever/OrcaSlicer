#ifndef slic3r_Events_hpp_
#define slic3r_Events_hpp_

#include <array>
#include <wx/event.h>


namespace Slic3r {

namespace GUI {


struct SimpleEvent : public wxEvent
{
    SimpleEvent(wxEventType type, wxObject* origin = nullptr) : wxEvent(0, type)
    {
        m_propagationLevel = wxEVENT_PROPAGATE_MAX;
        SetEventObject(origin);
    }

    virtual wxEvent* Clone() const
    {
        return new SimpleEvent(GetEventType(), GetEventObject());
    }
};

template<class T, size_t N> struct ArrayEvent : public wxEvent
{
    std::array<T, N> data;

    ArrayEvent(wxEventType type, std::array<T, N> data, wxObject* origin = nullptr)
        : wxEvent(0, type), data(std::move(data))
    {
        m_propagationLevel = wxEVENT_PROPAGATE_MAX;
        SetEventObject(origin);
    }

    virtual wxEvent* Clone() const
    {
        return new ArrayEvent<T, N>(GetEventType(), data, GetEventObject());
    }
};
template<class T> struct ArrayEvent<T, 1> : public wxEvent
{
    T data;

    ArrayEvent(wxEventType type, T data, wxObject* origin = nullptr)
        : wxEvent(0, type), data(std::move(data))
    {
        m_propagationLevel = wxEVENT_PROPAGATE_MAX;
        SetEventObject(origin);
    }

    virtual wxEvent* Clone() const
    {
        return new ArrayEvent<T, 1>(GetEventType(), data, GetEventObject());
    }
};

template <class T> using Event = ArrayEvent<T, 1>;


}
}

#endif // slic3r_Events_hpp_
