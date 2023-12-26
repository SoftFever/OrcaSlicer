#ifndef slic3r_GUI_ComboBox_hpp_
#define slic3r_GUI_ComboBox_hpp_

#include "TextInput.hpp"
#include "DropDown.hpp"

#define CB_NO_DROP_ICON DD_NO_CHECK_ICON
#define CB_NO_TEXT DD_NO_TEXT

class ComboBox : public wxWindowWithItems<TextInput, wxItemContainer>
{
    std::vector<wxString>         texts;
    std::vector<wxBitmap>         icons;
    std::vector<void *>           datas;
    std::vector<wxClientDataType> types;

    DropDown               drop;
    bool     drop_down = false;
    bool     text_off = false;

public:
    ComboBox(wxWindow *      parent,
             wxWindowID      id,
             const wxString &value     = wxEmptyString,
             const wxPoint & pos       = wxDefaultPosition,
             const wxSize &  size      = wxDefaultSize,
             int             n         = 0,
             const wxString  choices[] = NULL,
             long            style     = 0);

    DropDown & GetDropDown() { return drop; }

    virtual bool SetFont(wxFont const & font) override;

public:
    int Append(const wxString &item, const wxBitmap &bitmap = wxNullBitmap);

    int Append(const wxString &item, const wxBitmap &bitmap, void *clientData);

    unsigned int GetCount() const override;

    int  GetSelection() const override;

    void SetSelection(int n) override;

    void SelectAndNotify(int n);

    virtual void Rescale() override;

    wxString GetValue() const;
    void     SetValue(const wxString &value);

    void SetLabel(const wxString &label) override;
    wxString GetLabel() const override;

    void SetTextLabel(const wxString &label);
    wxString GetTextLabel() const;

    wxString GetString(unsigned int n) const override;
    void     SetString(unsigned int n, wxString const &value) override;

    wxBitmap GetItemBitmap(unsigned int n);
    void     SetItemBitmap(unsigned int n, wxBitmap const &bitmap);
    bool     is_drop_down(){return drop_down;}
protected:
    virtual int  DoInsertItems(const wxArrayStringsAdapter &items,
                               unsigned int                 pos,
                               void **                      clientData,
                               wxClientDataType             type) override;
    virtual void DoClear() override;

    void DoDeleteOneItem(unsigned int pos) override;

    void *DoGetItemClientData(unsigned int n) const override;
    void  DoSetItemClientData(unsigned int n, void *data) override;
    
    void OnEdit() override;

    void sendComboBoxEvent();

#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:

    // some useful events
    void mouseDown(wxMouseEvent &event);
    void mouseWheelMoved(wxMouseEvent &event);
    void keyDown(wxKeyEvent &event);
    void onMove(wxMoveEvent &event);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_ComboBox_hpp_
