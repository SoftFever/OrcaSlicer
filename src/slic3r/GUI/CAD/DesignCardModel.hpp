#ifndef slic3r_DesignCardModel_hpp_
#define slic3r_DesignCardModel_hpp_

// A tool card of the Design panel as DATA: a title, an icon and an ordered list of rows, each a label and a field. The ImGui
// sidebar draws a card straight from this table (DesignSidebarMirror.cpp); there is no wx control behind it.
//
// The fields are wxEvtHandlers with the small slice of the wxSpinCtrlDouble / ComboBox / CheckBox API the panel's code uses
// (GetValue, SetValue, Append, SetSelection, Enable, ...), so the handlers written against those controls bind and read
// them unchanged. A change made by the USER (user_set / user_select) is queued as the event the wx control would have sent
// and then handed on to the next handler, the panel's form, which is where its refresh-the-preview handlers are bound.
// A change made by the code (SetValue ...) sends nothing, as with the wx controls.
//
// To add a control to a card: one line in the card's builder (card->spin(label, ...), card->combo(label), ...). Nothing else
// has to know about it.

#include <wx/event.h>
#include <wx/string.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>
#include <wx/tglbtn.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

class CardField : public wxEvtHandler
{
public:
    enum class Kind { Num, Choice, Toggle, Text };
    virtual Kind kind() const = 0;

    bool IsEnabled() const { return m_enabled; }
    void Enable(bool on = true) { m_enabled = on; }
    bool IsShown() const { return m_shown; }
    void Show(bool on = true) { m_shown = on; }
    const wxString& tip() const { return m_tip; }
    void SetToolTip(const wxString& t) { m_tip = t; }

protected:
    // What the wx control would have sent, queued: the handlers run on the next turn of the loop, outside the ImGui frame.
    void post(wxEvent& e) { e.SetEventObject(this); wxQueueEvent(this, e.Clone()); }

private:
    bool     m_enabled{true};
    bool     m_shown{true};
    wxString m_tip;
};

class NumField : public CardField
{
public:
    NumField(double v, double lo, double hi) : m_v(v), m_lo(lo), m_hi(hi) {}
    Kind kind() const override { return Kind::Num; }

    double GetValue() const { return m_v; }
    void   SetValue(double v) { m_v = std::clamp(v, m_lo, m_hi); }
    double GetMin() const { return m_lo; }
    double GetMax() const { return m_hi; }
    double GetIncrement() const { return m_inc; }
    int    GetDigits() const { return m_digits; }
    void   SetRange(double lo, double hi) { m_lo = lo; m_hi = hi; m_v = std::clamp(m_v, lo, hi); }
    void   SetDigits(int d) { m_digits = d; }
    void   SetIncrement(double i) { m_inc = i; }

    // the user typed or stepped to `v`
    void user_set(double v)
    {
        v = std::clamp(v, m_lo, m_hi);
        if (v == m_v) return;
        m_v = v;
        wxSpinDoubleEvent e(wxEVT_SPINCTRLDOUBLE, 0);
        e.SetValue(v);
        post(e);
    }

private:
    double m_v, m_lo, m_hi, m_inc{1.0};
    int    m_digits{2};
};

class ChoiceField : public CardField
{
public:
    Kind kind() const override { return Kind::Choice; }

    int  Append(const wxString& s) { m_items.push_back(s); return int(m_items.size()) - 1; }
    void Clear() { m_items.clear(); m_sel = -1; }
    unsigned GetCount() const { return unsigned(m_items.size()); }
    wxString GetString(unsigned i) const { return i < m_items.size() ? m_items[i] : wxString(); }
    int  GetSelection() const { return m_sel; }
    void SetSelection(int i) { m_sel = i; }

    void user_select(int i)
    {
        if (i == m_sel || i < 0 || i >= int(m_items.size())) return;
        m_sel = i;
        wxCommandEvent e(wxEVT_COMBOBOX, 0);
        e.SetInt(i);
        e.SetString(m_items[size_t(i)]);
        post(e);
    }

private:
    std::vector<wxString> m_items;
    int                   m_sel{-1};
};

class ToggleField : public CardField
{
public:
    Kind kind() const override { return Kind::Toggle; }
    bool GetValue() const { return m_v; }
    void SetValue(bool v) { m_v = v; }

    void user_toggle()
    {
        m_v = !m_v;
        wxCommandEvent e(wxEVT_TOGGLEBUTTON, 0);
        e.SetInt(m_v ? 1 : 0);
        post(e);
    }

private:
    bool m_v{false};
};

// A line of text the code changes (a card title, a note, the picked edge's name).
class CardText : public CardField
{
public:
    explicit CardText(const wxString& s = wxString()) : m_text(s) {}
    Kind kind() const override { return Kind::Text; }
    void     SetLabel(const wxString& s) { m_text = s; }
    wxString GetLabel() const { return m_text; }
    void     Refresh() {}   // drawn every frame

private:
    wxString m_text;
};

struct CardRow {
    enum class Kind { Field, Note };   // Field: a label and its control; Note: a text across the card
    Kind        kind{Kind::Field};
    wxString    label;
    CardField*  field{nullptr};        // owned by the card
};

class CardModel
{
public:
    CardModel(std::string icon, const wxString& title) : m_icon(std::move(icon)), m_title(new CardText(title)) {}

    // The panel's form takes the events the fields queue (its handlers refresh the preview).
    void set_sink(wxEvtHandler* sink) { m_sink = sink; for (auto& f : m_fields) f->SetNextHandler(sink); }

    CardText*    title() { return m_title.get(); }
    const std::string& icon() const { return m_icon; }
    const std::vector<CardRow>& rows() const { return m_rows; }

    // A control of the card's row builders is a row: label on the left, field on the right.
    NumField*    spin(const wxString& label, double v, double lo = 0.1, double hi = 1000.0)
    { return add_field(label, new NumField(v, lo, hi)); }
    ChoiceField* combo(const wxString& label) { return add_field(label, new ChoiceField()); }
    ToggleField* check(const wxString& label) { return add_field(label, new ToggleField()); }
    CardText*    text(const wxString& label, const wxString& s = wxString()) { return add_field(label, new CardText(s)); }
    // a line across the card
    CardText*    note(const wxString& s)
    {
        CardText* t = add_field(wxString(), new CardText(s));
        m_rows.back().kind = CardRow::Kind::Note;
        return t;
    }

private:
    template<class T> T* add_field(const wxString& label, T* f)
    {
        m_fields.emplace_back(f);
        f->SetNextHandler(m_sink);
        m_rows.push_back({CardRow::Kind::Field, label, f});
        return f;
    }

    std::string                             m_icon;
    std::unique_ptr<CardText>               m_title;
    std::vector<std::unique_ptr<CardField>> m_fields;
    std::vector<CardRow>                    m_rows;
    wxEvtHandler*                           m_sink{nullptr};
};

}} // namespace Slic3r::GUI

#endif
