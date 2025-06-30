#ifndef slic3r_GUI_FilamentPickerDialog_hpp_
#define slic3r_GUI_FilamentPickerDialog_hpp_

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "FilamentBitmapUtils.hpp"
#include "Widgets/Button.hpp"
#include "EncodedFilament.hpp"
#include <wx/dialog.h>
#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/bitmap.h>
#include <wx/region.h>
#include <vector>
#include <string>

namespace Slic3r { namespace GUI {

class FilamentPickerDialog : public DPIDialog
{
public:
    FilamentPickerDialog(wxWindow *parent, const wxString &fila_id, const FilamentColor &fila_color, const std::string &fila_type);
    virtual ~FilamentPickerDialog();

    // Public interface methods
    bool IsDataLoaded() const { return m_is_data_loaded; }
    wxColour GetSelectedColour() const;
    const FilamentColor& GetSelectedFilamentColor() const { return m_current_filament_color; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

    // Event handlers
#ifdef __WXGTK__
    void OnWindowCreate(wxWindowCreateEvent& event);
#endif
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnButtonPaint(wxPaintEvent& event);

private:
    // UI creation methods
    wxBoxSizer* CreatePreviewPanel(const FilamentColor& fila_color, const std::string& fila_type);
    wxScrolledWindow* CreateColorGrid();
    wxBoxSizer* CreateSeparatorLine();
    void CreateMoreInfoButton();
    wxBoxSizer* CreateButtonPanel();
    void BindEvents();

    // Preview panel helper methods
    void CreateColorBitmap(const FilamentColor& fila_color);
    wxBoxSizer* CreateInfoSection();
    void SetupLabelsContent(const FilamentColor& fila_color, const std::string& fila_type);

    // UI update methods
    void UpdatePreview(const FilamentColorCode& filament);
    void UpdateButtonStates(wxBitmapButton* selected_btn);

    // Shaped window methods
    void SetWindowShape();
    void CreateShapedBitmap();

    // Data loading
    bool LoadFilamentData(const wxString& fila_id);

    // UI elements
    wxStaticBitmap* m_color_demo{nullptr};
    wxStaticText* m_label_preview_color{nullptr};
    wxStaticText* m_label_preview_idx{nullptr};
    wxStaticText* m_label_preview_type{nullptr};
    Button* m_more_btn{nullptr};
    Button* m_ok_btn{nullptr};
    Button* m_cancel_btn{nullptr};
    wxString* m_cur_color_name{nullptr};

    // Data members
    FilamentColorCodeQuery* m_color_query{nullptr};
    FilamentColorCodes* m_current_color_codes{nullptr};
    bool m_is_data_loaded{false};
    wxBitmapButton* m_currently_selected_btn{nullptr};
    FilamentColor m_current_filament_color;

    // Shaped window members
    wxBitmap m_shape_bmp;
    int m_corner_radius{8};

    // Mouse drag members
    wxPoint m_drag_delta;
};

}} // namespace Slic3r::GUI

#endif
