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

#include <wx/bitmap.h>
#include <wx/checklst.h>
#include <wx/colour.h>
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
    enum class Kind { Num, Choice, Toggle, Text, Button, CheckList };
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

    int  Append(const wxString& s) { m_items.push_back(s); m_data.push_back(nullptr); return int(m_items.size()) - 1; }
    int  Append(const wxString& s, const wxBitmap&) { return Append(s); }
    void Clear() { m_items.clear(); m_data.clear(); m_sel = -1; }
    void  SetClientData(unsigned i, void* d) { if (i < m_data.size()) m_data[i] = d; }
    void* GetClientData(unsigned i) const { return i < m_data.size() ? m_data[i] : nullptr; }
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
    std::vector<void*>    m_data;
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
    // a status colour (green, red); the default is the panel's text colour
    void     SetForegroundColour(const wxColour& c) { m_fg = c; }
    const wxColour& colour() const { return m_fg; }

private:
    wxString m_text;
    wxColour m_fg;
};

class ButtonField : public CardField
{
public:
    explicit ButtonField(const wxString& s) : m_text(s) {}
    Kind kind() const override { return Kind::Button; }
    void     SetLabel(const wxString& s) { m_text = s; }
    wxString GetLabel() const { return m_text; }
    template<class A, class B> void SetStyle(A&&, B&&) {}
    wxEvtHandler* GetEventHandler() { return this; }   // the wx button's look; the ImGui one has one

    void user_click()
    {
        wxCommandEvent e(wxEVT_BUTTON, 0);
        post(e);
    }

private:
    wxString m_text;
};

// A list of items each with a check box (the features to repeat, the profiles of a loft).
class CheckListField : public CardField
{
public:
    Kind kind() const override { return Kind::CheckList; }
    int      Append(const wxString& s) { m_items.push_back(s); m_on.push_back(false); return int(m_items.size()) - 1; }
    void     Clear() { m_items.clear(); m_on.clear(); }
    unsigned GetCount() const { return unsigned(m_items.size()); }
    wxString GetString(unsigned i) const { return i < m_items.size() ? m_items[i] : wxString(); }
    bool     IsChecked(unsigned i) const { return i < m_on.size() && m_on[i]; }
    void     Check(unsigned i, bool on = true) { if (i < m_on.size()) m_on[i] = on; }

    void user_check(unsigned i)
    {
        if (i >= m_on.size()) return;
        m_on[i] = !m_on[i];
        wxCommandEvent e(wxEVT_CHECKLISTBOX, 0);
        e.SetInt(int(i));
        post(e);
    }

private:
    std::vector<wxString> m_items;
    std::vector<bool>     m_on;
};

// Rows that show and hide together (a mode of a card that needs only some of its inputs).
struct CardGroup {
    bool shown{true};
    void Show(bool on = true) { shown = on; }
    bool IsShown() const { return shown; }
};

struct CardRow {
    // Field: a label and its control. Note: a text across the card. Wide: a control across the card (a list).
    // Pick: a button in the label column and a text in the control column.
    enum class Kind { Field, Note, Wide, Pick };
    Kind        kind{Kind::Field};
    wxString    label;
    CardField*  field{nullptr};        // owned by the card
    CardField*  field2{nullptr};       // Pick: the text beside the button; Field: a label the code changes (else `label`)
    CardGroup*  group{nullptr};        // hidden when its group is
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
    // The wx-style two steps: make a field, set it up, then give it a row. (A field with no row is never drawn.)
    ChoiceField* make_combo() { return own(new ChoiceField()); }
    NumField*    make_spin(double v, double lo = 0.1, double hi = 1000.0) { return own(new NumField(v, lo, hi)); }
    ToggleField* make_check() { return own(new ToggleField()); }
    CardText*    make_text(const wxString& s = wxString()) { return own(new CardText(s)); }
    CheckListField* make_checklist() { return own(new CheckListField()); }
    ButtonField* make_button(const wxString& s) { return own(new ButtonField(s)); }
    void         row(const wxString& label, CardField* f) { m_rows.push_back({CardRow::Kind::Field, label, f, nullptr, m_group}); }
    void         row_t(CardText* label, CardField* f) { m_rows.push_back({CardRow::Kind::Field, wxString(), f, label, m_group}); }
    void         wide(CardField* f) { m_rows.push_back({CardRow::Kind::Wide, wxString(), f, nullptr, m_group}); }
    void         note_text(CardText* t) { m_rows.push_back({CardRow::Kind::Note, wxString(), t, nullptr, m_group}); }
    // a button in the control column (the label column stays empty)
    ButtonField* button(const wxString& label, const wxString& text) { return add_field(label, new ButtonField(text)); }
    // a list across the card, under a caption
    CheckListField* checklist(const wxString& caption)
    {
        note(caption);
        CheckListField* l = add_field(wxString(), new CheckListField());
        m_rows.back().kind = CardRow::Kind::Wide;
        return l;
    }
    // a button on the left and the text it fills in on the right
    void pick(const wxString& button_text, ButtonField*& b, CardText*& t, const wxString& initial)
    {
        b = new ButtonField(button_text);
        t = new CardText(initial);
        m_fields.emplace_back(b);
        m_fields.emplace_back(t);
        b->SetNextHandler(m_sink);
        t->SetNextHandler(m_sink);
        CardRow r{CardRow::Kind::Pick, wxString(), b, t, m_group};
        m_rows.push_back(r);
    }
    // The rows added until end_group() show and hide together.
    CardGroup*   begin_group() { m_groups.emplace_back(new CardGroup()); m_group = m_groups.back().get(); return m_group; }
    void         end_group() { m_group = nullptr; }

private:
    template<class T> T* own(T* f)
    {
        m_fields.emplace_back(f);
        f->SetNextHandler(m_sink);
        return f;
    }
    template<class T> T* add_field(const wxString& label, T* f)
    {
        m_fields.emplace_back(f);
        f->SetNextHandler(m_sink);
        m_rows.push_back({CardRow::Kind::Field, label, f, nullptr, m_group});
        return f;
    }

    std::string                             m_icon;
    std::unique_ptr<CardText>               m_title;
    std::vector<std::unique_ptr<CardField>> m_fields;
    std::vector<CardRow>                    m_rows;
    std::vector<std::unique_ptr<CardGroup>> m_groups;
    CardGroup*                              m_group{nullptr};
    wxEvtHandler*                           m_sink{nullptr};
};

}} // namespace Slic3r::GUI

#endif
