#include "DragDropPanel.hpp"

namespace Slic3r { namespace GUI {
// Custom data object used to store information that needs to be backed up during drag and drop
class ColorDataObject : public wxDataObjectSimple
{
public:
    ColorDataObject(wxPanel *color_block = nullptr, wxPanel *parent = nullptr, const wxColour &color = *wxBLACK, int filament_id = 0)
        : wxDataObjectSimple(wxDF_PRIVATE)
        , m_parent(parent)
        , m_source_block(color_block)
        , m_color(color)
        , m_filament_id(filament_id) {}

    wxColour GetColor() const { return m_color; }
    void     SetColor(const wxColour &color) { m_color = color; }

    int      GetFilament() const { return m_filament_id; }
    void     SetFilament(int label) { m_filament_id = label; }

    wxPanel *GetParent() const { return m_parent; }
    void     SetParent(wxPanel * parent) { m_parent = parent; }

    wxPanel *GetSourceBlock() const { return m_source_block; }
    void     SetSourceBlock(wxPanel *source_block) { m_source_block = source_block; }

    virtual size_t GetDataSize() const override { return sizeof(m_color) + sizeof(int) + sizeof(m_parent) + sizeof(m_source_block); }
    virtual bool   GetDataHere(void *buf) const override
    {
        char *ptr = static_cast<char *>(buf);
        wxColour *  colorBuf = static_cast<wxColour *>(buf);
        *colorBuf            = m_color;

        std::memcpy(ptr + sizeof(m_color), &m_filament_id, sizeof(int));

        wxPanel **panelBuf = reinterpret_cast<wxPanel **>(static_cast<char *>(buf) + sizeof(m_color) + sizeof(int));
        *panelBuf          = m_parent;

        wxPanel **blockBuf = reinterpret_cast<wxPanel **>(static_cast<char *>(buf) + sizeof(m_color) + sizeof(int) + sizeof(m_parent));
        *blockBuf          = m_source_block;
        return true;
    }
    virtual bool SetData(size_t len, const void *buf) override
    {
        if (len == GetDataSize()) {
            const char *ptr = static_cast<const char *>(buf);
            m_color         = *static_cast<const wxColour *>(buf);

            std::memcpy(&m_filament_id, ptr + sizeof(m_color), sizeof(int));

            m_parent = *reinterpret_cast<wxPanel *const *>(static_cast<const char *>(buf) + sizeof(m_color) + sizeof(int));

            m_source_block = *reinterpret_cast<wxPanel *const *>(static_cast<const char *>(buf) + sizeof(m_color) + sizeof(int) + sizeof(m_parent));
            return true;
        }
        return false;
    }
private:
    int m_filament_id;
    wxColour m_color;
    wxPanel *m_parent;
    wxPanel *m_source_block;
};

///////////////   ColorPanel  start ////////////////////////
// The UI panel of drag item
class ColorPanel : public wxPanel
{
public:
    ColorPanel(DragDropPanel *parent, const wxColour &color, int filament_id)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(50, 50), wxBORDER_SIMPLE)
        , m_parent(parent)
        , m_color(color)
        , m_filament_id(filament_id)
    {
        SetBackgroundColour(color);
        Bind(wxEVT_LEFT_DOWN, &ColorPanel::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &ColorPanel::OnLeftUp, this);
        Bind(wxEVT_PAINT, &ColorPanel::OnPaint, this);
    }

    wxColour GetColor() const { return GetBackgroundColour(); }
    int      GetFilamentId() const { return m_filament_id; }

private:
    void OnLeftDown(wxMouseEvent &event);
    void OnLeftUp(wxMouseEvent &event);
    void OnPaint(wxPaintEvent &event);

    DragDropPanel *m_parent;
    wxColor        m_color;
    int            m_filament_id;
};

void ColorPanel::OnLeftDown(wxMouseEvent &event)
{
    m_parent->set_is_draging(true);
    m_parent->DoDragDrop(this, GetColor(), GetFilamentId());
}

void ColorPanel::OnLeftUp(wxMouseEvent &event) { m_parent->set_is_draging(false); }

void ColorPanel::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    wxSize   size  = GetSize();
    wxString label = wxString::Format(wxT("%d"), m_filament_id);
    dc.SetTextForeground(m_color.GetLuminance() < 0.51 ? *wxWHITE : *wxBLACK);  // set text color
    dc.DrawLabel(label, wxRect(0, 0, size.GetWidth(), size.GetHeight()), wxALIGN_CENTER);
}
///////////////   ColorPanel  end ////////////////////////


// Save the source object information to m_data when dragging
class ColorDropSource : public wxDropSource
{
public:
    ColorDropSource(wxPanel *parent, wxPanel *color_block, const wxColour &color, int filament_id) : wxDropSource()
    {
        m_data.SetColor(color);
        m_data.SetFilament(filament_id);
        m_data.SetParent(parent);
        m_data.SetSourceBlock(color_block);
        SetData(m_data);  // Set drag source data
    }

private:
    ColorDataObject m_data;
};

///////////////   ColorDropTarget  start ////////////////////////
// Get the data from the drag source when drop it
class ColorDropTarget : public wxDropTarget
{
public:
    ColorDropTarget(DragDropPanel *panel) : wxDropTarget(/*new wxDataObjectComposite*/), m_panel(panel)
    {
        m_data = new ColorDataObject();
        SetDataObject(m_data);
    }

    virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override;
    virtual bool         OnDrop(wxCoord x, wxCoord y) override {
        return true;
    }

private:
    DragDropPanel * m_panel;
    ColorDataObject* m_data;
};

wxDragResult ColorDropTarget::OnData(wxCoord x, wxCoord y, wxDragResult def)
{
    if (!GetData())
        return wxDragNone;

    if (m_data->GetParent() == m_panel) {
        return wxDragNone;
    }

    DragDropPanel *  parent_panel = dynamic_cast<DragDropPanel *>(m_data->GetParent());
    ColorPanel *   color_block  = dynamic_cast<ColorPanel *>(m_data->GetSourceBlock());
    assert(parent_panel && color_block);
    parent_panel->RemoveColorBlock(color_block);

    ColorDataObject *dataObject = dynamic_cast<ColorDataObject *>(GetDataObject());
    m_panel->AddColorBlock(m_data->GetColor(), m_data->GetFilament());

    return wxDragCopy;
}
///////////////   ColorDropTarget  end ////////////////////////


DragDropPanel::DragDropPanel(wxWindow *parent, const wxString &label)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE)
{
    SetBackgroundColour(*wxLIGHT_GREY);

    m_sizer    = new wxBoxSizer(wxVERTICAL);
    wxStaticText *staticText = new wxStaticText(this, wxID_ANY, label);
    m_sizer->Add(staticText, 0, wxALIGN_CENTER | wxALL, 5);
    m_sizer->AddSpacer(20);

    m_grid_item_sizer = new wxGridSizer(0, 3, 10, 10);   // row = 0, col = 3,  10 10 is space
    m_sizer->Add(m_grid_item_sizer);

    // set droptarget
    auto drop_target = new ColorDropTarget(this);
    SetDropTarget(drop_target);

    SetSizer(m_sizer);
    Fit();
}

void DragDropPanel::AddColorBlock(const wxColour &color, int filament_id)
{
    ColorPanel *panel = new ColorPanel(this, color, filament_id);
    panel->SetMinSize(wxSize(50, 50));
    m_grid_item_sizer->Add(panel, 0, wxALIGN_CENTER | wxALL, 5);
    Layout();
}

void DragDropPanel::RemoveColorBlock(ColorPanel *panel)
{
    m_sizer->Detach(panel);
    panel->Destroy();
    Layout();
}

void DragDropPanel::DoDragDrop(ColorPanel *panel, const wxColour &color, int filament_id)
{
    ColorDropSource source(this, panel, color, filament_id);
    source.DoDragDrop(wxDrag_CopyOnly);
}

std::vector<int> DragDropPanel::GetAllFilaments() const
{
    std::vector<int>          filaments;
    for (size_t i = 0; i < m_grid_item_sizer->GetItemCount(); ++i) {
        wxSizerItem *item = m_grid_item_sizer->GetItem(i);
        if (item != nullptr) {
            wxWindow *  window     = item->GetWindow();
            ColorPanel *colorPanel = dynamic_cast<ColorPanel *>(window);
            if (colorPanel != nullptr) {
                filaments.push_back(colorPanel->GetFilamentId());
            }
        }
    }

    return filaments;
}

}} // namespace Slic3r::GUI
