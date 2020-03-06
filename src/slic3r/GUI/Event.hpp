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

template<class T> struct Event : public wxEvent
{
    T data;

    Event(wxEventType type, const T &data, wxObject* origin = nullptr)
        : wxEvent(0, type), data(std::move(data))
    {
        m_propagationLevel = wxEVENT_PROPAGATE_MAX;
        SetEventObject(origin);
    }

    Event(wxEventType type, T&& data, wxObject* origin = nullptr)
        : wxEvent(0, type), data(std::move(data))
    {
        m_propagationLevel = wxEVENT_PROPAGATE_MAX;
        SetEventObject(origin);
    }

    virtual wxEvent* Clone() const
    {
        return new Event<T>(GetEventType(), data, GetEventObject());
    }
};

}
}

#endif // slic3r_Events_hpp_
