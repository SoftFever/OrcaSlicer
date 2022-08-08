#include "wx/clipbrd.h"

#include "SelectMachine.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
//#include "libslic3r/Model.hpp"
//#include "Plater.hpp"
#include "Widgets/Label.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "Tab.hpp"
#include "format.hpp"
#include "BitmapCache.hpp"
#include "GUI_ObjectTable.hpp"
#include "GUI_ObjectList.hpp"

//use wxGridWindow to compute position
//#include "wx/generic/private/grid.h"

#define HAS_COL_HEADER  1

namespace Slic3r {
namespace GUI {
static const int grid_cell_border_width = 2;
static const int grid_cell_border_height = 2;
static const int grid_cell_checkbox_size = 16;

//min row count
static const int g_min_row_count = 16;
//when row count is bigger than overflow row count, will compute the total height by row_count*g_min_row_size
//else will count the height one by one
static const int g_overflow_row_count = 50;
static const int g_min_row_size = 36;
static const int g_extra_height = 64;

static const int g_min_setting_width = 464;
static const int g_vscroll_width = 48;


static int g_dialog_width = 0;
static int g_dialog_height = 0;
static int g_dialog_max_width = 0;
static int g_dialog_max_height = 0;
static wxSize g_max_size_from_parent;

/* ObjectGridTable related class */
// ----------------------------------------------------------------------------
// GridCellIconRenderer
// ----------------------------------------------------------------------------
void GridCellIconRenderer::Draw(wxGrid& grid,
                              wxGridCellAttr& attr,
                              wxDC& dc,
                              const wxRect& rect,
                              int row, int col,
                              bool isSelected)
{
    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid.GetTable());

    wxGridCellRenderer::Draw(grid, attr, dc, rect, row, col, isSelected);
    if (table) {
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        if (!grid_col || !grid_col->b_icon) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": cell (%1%, %2%) not icon type") %row %col;
            return;
        }
        ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
        ConfigOption& orig_option = (*grid_row)[(ObjectGridTable::GridColType)col];
        ConfigOption& cur_option = (*grid_row)[(ObjectGridTable::GridColType)(col-1)];
        if (cur_option == orig_option) {
            //not changed
            return;
        }
        if (!table->m_icon_col_width) {
            table->m_icon_row_height = grid.GetRowSize(row);
            table->m_icon_col_width = grid.GetColSize(col);
        }
        wxBitmap& bitmap = table->get_undo_bitmap();
        int bitmap_width = bitmap.GetWidth();
        int bitmap_height = bitmap.GetHeight();
        int offset_x = (table->m_icon_col_width - bitmap_width)/2;
        int offset_y = (table->m_icon_row_height - bitmap_height)/2;
        dc.DrawBitmap(bitmap, wxPoint(rect.x + offset_x, rect.y + offset_y));

        //dc.SetPen(*wxGREEN_PEN);
        //dc.SetBrush(*wxTRANSPARENT_BRUSH);
        //dc.DrawEllipse(rect);
    }
    else {
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": no table found, should not happen" << std::endl;
    }
}

wxSize GridCellIconRenderer::GetBestSize(wxGrid& WXUNUSED(grid),
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col))
{
    wxSize size{32, 30};
    return size;
}

GridCellIconRenderer *GridCellIconRenderer::Clone() const
{
    return new GridCellIconRenderer();
}

// ----------------------------------------------------------------------------
// GridCellFilamentsEditor
// ----------------------------------------------------------------------------

GridCellFilamentsEditor::GridCellFilamentsEditor(const wxArrayString& choices,
                                               bool allowOthers,
                                               std::vector<wxBitmap*>* bitmaps)
    : wxGridCellChoiceEditor(choices, allowOthers), m_icons(bitmaps)
{
}

GridCellFilamentsEditor::GridCellFilamentsEditor(size_t count,
                                               const wxString choices[],
                                               bool allowOthers,
                                               std::vector<wxBitmap*>* bitmaps)
    : wxGridCellChoiceEditor(count, choices, allowOthers), m_icons(bitmaps)
{
}

wxGridCellEditor *GridCellFilamentsEditor::Clone() const
{
    GridCellFilamentsEditor *editor = new GridCellFilamentsEditor;
    editor->m_allowOthers = m_allowOthers;
    editor->m_choices = m_choices;
    editor->m_icons = m_icons;

    return editor;
}

void GridCellFilamentsEditor::Create(wxWindow* parent,
                                    wxWindowID id,
                                    wxEvtHandler* evtHandler)
{
    int style = wxTE_PROCESS_ENTER |
                wxTE_PROCESS_TAB |
                wxBORDER_NONE;

    if ( !m_allowOthers )
        style |= wxCB_READONLY;
    wxBitmapComboBox *bitmap_combo = new wxBitmapComboBox(parent, id, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize,
                               m_choices,
                               style);
    if (m_icons) {
        int array_count = m_choices.GetCount();
        int icon_count = m_icons->size();
        for (int i = 0; i < array_count; i++)
        {
            wxBitmap* bitmap = (i < icon_count) ? (*m_icons)[i] : (*m_icons)[0];
            bitmap_combo->SetItemBitmap(i, *bitmap);
        }
    }
    m_control = bitmap_combo;
    wxGridCellEditor::Create(parent, id, evtHandler);
}

void GridCellFilamentsEditor::SetSize(const wxRect& rect)
{
    wxGridCellChoiceEditor::SetSize(rect);
    /*wxASSERT_MSG(m_control,
                 wxT("The wxGridCellChoiceEditor must be created first!"));

    // Use normal wxChoice size, except for extending it to fill the cell
    // width: we can't be smaller because this could make the control unusable
    // and we don't want to be taller because this looks unusual and weird.
    wxSize size = m_control->GetBestSize();
    if ( size.x < rect.width )
        size.x = rect.width;

    DoPositionEditor(size, rect);*/
}

void GridCellFilamentsEditor::OnComboCloseUp(wxCommandEvent& evt)
{
    wxGridCellChoiceEditor::OnComboCloseUp(evt);
}

void GridCellFilamentsEditor::BeginEdit(int row, int col, wxGrid* grid)
{
    wxGridCellEditorEvtHandler* evtHandler = NULL;
    if (m_control)
    {
        // This event handler is needed to properly dismiss the editor when the popup is closed
        m_control->Bind(wxEVT_COMBOBOX_CLOSEUP, &GridCellFilamentsEditor::OnComboCloseUp, this);
        evtHandler = wxDynamicCast(m_control->GetEventHandler(), wxGridCellEditorEvtHandler);
    }

    // Don't immediately end if we get a kill focus event within BeginEdit
    if (evtHandler)
        evtHandler->SetInSetFocus(true);

    //m_value = grid->GetTable()->GetValue(row, col);
    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid->GetTable());
    ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
    ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
    ConfigOption& option = (*grid_row)[(ObjectGridTable::GridColType)col];
    ConfigOptionInt& option_value = dynamic_cast<ConfigOptionInt&>(option);
    if ((option_value.value - 1) < grid_col->choice_count) {
       //return grid_col->choices[(option_value.value > 0)?option_value.value - 1: 0];
        int index = (option_value.value > 0) ? option_value.value - 1 : 0;
        m_value = grid_col->choices[index];
       //return grid_col->choices[index];
    }
    //m_value = grid->GetTable()->GetValue(row, col);

    Reset(); // this updates combo box to correspond to m_value

    Combo()->SetFocus();

#ifdef __WXOSX_COCOA__
    // This is a work around for the combobox being simply dismissed when a
    // choice is made in it under OS X. The bug is almost certainly due to a
    // problem in focus events generation logic but it's not obvious to fix and
    // for now this at least allows to use wxGrid.
    Combo()->Popup();
#endif

    if (evtHandler)
    {
        // When dropping down the menu, a kill focus event
        // happens after this point, so we can't reset the flag yet.
#if !defined(__WXGTK20__)
        evtHandler->SetInSetFocus(false);
#endif
    }
}

bool GridCellFilamentsEditor::EndEdit(int WXUNUSED(row),
                                     int WXUNUSED(col),
                                     const wxGrid* WXUNUSED(grid),
                                     const wxString& WXUNUSED(oldval),
                                     wxString *newval)
{
    const wxString value = Combo()->GetValue();
    if ( value == m_value )
        return false;

    m_value = value;

    if ( newval )
        *newval = value;

    return true;
}


wxGridActivationResult GridCellFilamentsEditor::TryActivate(int row, int col, wxGrid* grid, const wxGridActivationSource& actSource)
{
    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid->GetTable());
    ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
    ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);

    if ( actSource.GetOrigin() == wxGridActivationSource::Key ) {
        const wxKeyEvent& key_event = actSource.GetKeyEvent();
        int keyCode = key_event.GetKeyCode();
        wxString choice;

        int digital_value = keyCode - '0';
        if ((digital_value >= 1) && (digital_value <= 9))
            m_cached_value = digital_value;
        else
            m_cached_value = -1;

        if (m_cached_value != -1) {
            if (m_cached_value <= grid_col->choice_count) {
                choice = grid_col->choices[m_cached_value-1];
                return wxGridActivationResult::DoChange(choice);
            }
            else {
                return wxGridActivationResult::DoNothing();
            }
        }
        else
            return wxGridActivationResult::DoNothing();
    }
    else {
        m_cached_value = -1;
        return wxGridActivationResult::DoEdit();
    }
}

void GridCellFilamentsEditor::DoActivate(int row, int col, wxGrid* grid)
{
    if (m_cached_value != -1) {
        ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid->GetTable());
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
        if (m_cached_value <= grid_col->choice_count) {
            wxString choice = grid_col->choices[m_cached_value-1];
            table->SetValue(row, col, choice);
            //Combo()->SetValue(choice);
        }
        m_cached_value = -1;
    }
}

// ----------------------------------------------------------------------------
// GridCellFilamentsRenderer
// ----------------------------------------------------------------------------
void GridCellFilamentsRenderer::Draw(wxGrid& grid,
                              wxGridCellAttr& attr,
                              wxDC& dc,
                              const wxRect& rect,
                              int row, int col,
                              bool isSelected)
{
    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid.GetTable());
    wxRect text_rect = rect;

    if (table) {
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
        ConfigOptionInt& cur_option = dynamic_cast<ConfigOptionInt&>((*grid_row)[(ObjectGridTable::GridColType)col]);

        wxBitmap* bitmap = table->get_color_bitmap((cur_option.value >= 1)?cur_option.value-1:cur_option.value);
        int bitmap_width = bitmap->GetWidth();
        int bitmap_height = bitmap->GetHeight();
        int offset_x = grid_cell_border_width;
        int offset_y = (rect.height > bitmap_height)?(rect.height - bitmap_height)/2 : grid_cell_border_height;

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
        dc.DrawRectangle(rect);
        dc.DrawBitmap(*bitmap, wxPoint(rect.x + offset_x, rect.y + offset_y));
        text_rect.x += bitmap_width + grid_cell_border_width *2;
        text_rect.width -= (bitmap_width + grid_cell_border_width *2);
    }

    wxGridCellChoiceRenderer::Draw(grid, attr, dc, text_rect, row, col, isSelected);
}

wxSize GridCellFilamentsRenderer::GetBestSize(wxGrid& grid,
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col))
{
    wxSize size{128, -1};
    return size;
}

GridCellFilamentsRenderer *GridCellFilamentsRenderer::Clone() const
{
    return new GridCellFilamentsRenderer();
}

// ----------------------------------------------------------------------------
// wxGridCellSupportEditor
// ----------------------------------------------------------------------------
// the default values for GetValue()
wxString GridCellSupportEditor::ms_stringValues[2] = { wxT(""), wxT("") };

void GridCellSupportEditor::DoActivate(int row, int col, wxGrid* grid)
{
    wxGridCellBoolEditor::DoActivate(row, col, grid);
    grid->SelectBlock(row, col, row, col, true);
}

void GridCellSupportEditor::SetValueFromGrid(int row, int col, wxGrid* grid)
{
    if (grid->GetTable()->CanGetValueAs(row, col, wxGRID_VALUE_BOOL))
    {
        m_value = grid->GetTable()->GetValueAsBool(row, col);
    }
    else
    {
        wxString cellval( grid->GetTable()->GetValue(row, col) );

        if ( cellval == ms_stringValues[false] )
            m_value = false;
        else if ( cellval == ms_stringValues[true] )
            m_value = true;
        else
        {
            // do not try to be smart here and convert it to true or false
            // because we'll still overwrite it with something different and
            // this risks to be very surprising for the user code, let them
            // know about it
            wxFAIL_MSG( wxT("invalid value for a cell with bool editor!") );

            // Still need to initialize it to something.
            m_value = false;
        }
    }
}

void GridCellSupportEditor::SetGridFromValue(int row, int col, wxGrid* grid) const
{
    wxGridTableBase * const table = grid->GetTable();
    if ( table->CanSetValueAs(row, col, wxGRID_VALUE_BOOL) )
        table->SetValueAsBool(row, col, m_value);
    else
        table->SetValue(row, col, GetStringValue());
}

// ----------------------------------------------------------------------------
// GridCellSupportRenderer
// ----------------------------------------------------------------------------
void GridCellSupportRenderer::Draw(wxGrid& grid,
                              wxGridCellAttr& attr,
                              wxDC& dc,
                              const wxRect& rect,
                              int row, int col,
                              bool isSelected)
{
    /*ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(grid.GetTable());
    wxRect text_rect = rect;

    if (table) {
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        ObjectGridTable::ObjectGridRow* grid_row = table->get_grid_row(row - 1);
        ConfigOptionBool& cur_option = dynamic_cast<ConfigOptionBool&>((*grid_row)[(ObjectGridTable::GridColType)col]);
        wxString support_text;

        if (cur_option.value)
            support_text = L("Support Enabled");
        else
            support_text = L("Support Disabled");
        int text_width, text_height;
        grid.GetTextExtent(L("Support Disabled"), &text_width, &text_height, NULL, NULL, &Label::Body_10);

        int offset_x = grid_cell_border_width;
        int offset_y = (rect.height > text_height)?(rect.height - text_height)/2 : grid_cell_border_height;

        //dc.SetPen(*wxTRANSPARENT_PEN);
        //dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
        //dc.DrawRectangle(rect);

        wxGridCellRenderer::Draw(grid, attr, dc, rect, row, col, isSelected);

        wxColour text_back_colour;
        wxColour text_fore_colour;
        if ( isSelected )
        {
            text_back_colour = grid.GetSelectionBackground();
            text_fore_colour = grid.GetSelectionForeground();
        }
        else
        {
            text_back_colour = attr.GetBackgroundColour();
            text_fore_colour = attr.GetTextColour();
        }
        dc.SetTextBackground(text_back_colour);
        dc.SetTextForeground(text_fore_colour);
        dc.DrawText(support_text, wxPoint(rect.x + offset_x, rect.y + offset_y));
        text_rect.x += text_width + grid_cell_border_width *2;
        text_rect.width -= (text_width + grid_cell_border_width *2);
        text_rect.y += offset_y;
        text_rect.height = grid_cell_checkbox_size;// + grid_cell_border_width *2;

        int flags = wxCONTROL_CELL;
        if (cur_option.value)
            flags |= wxCONTROL_CHECKED;

        wxRendererNative::Get().DrawCheckBox( &grid, dc, text_rect, flags );
    }*/

    wxGridCellBoolRenderer::Draw(grid, attr, dc, rect, row, col, isSelected);
}

wxSize GridCellSupportRenderer::GetBestSize(wxGrid& grid,
                               wxGridCellAttr& attr,
                               wxDC& dc,
                               int WXUNUSED(row),
                               int WXUNUSED(col))
{
/*
    int text_width, text_height, width;
    grid.GetTextExtent(L("Support Disabled"), &text_width, &text_height, NULL, NULL, &Label::Body_10);
    width = text_width + 3*grid_cell_border_width + grid_cell_checkbox_size;

    wxSize size{width, 20};
*/
    wxSize size{32, 20};

    return size;
}

GridCellSupportRenderer *GridCellSupportRenderer::Clone() const
{
    return new GridCellSupportRenderer();
}

// ----------------------------------------------------------------------------
// ObjectGridTable
// ----------------------------------------------------------------------------
//wxIMPLEMENT_CLASS(ObjectGrid, wxGrid);

wxBEGIN_EVENT_TABLE( ObjectGrid, wxGrid )
    EVT_KEY_DOWN( ObjectGrid::OnKeyDown )
    EVT_KEY_UP( ObjectGrid::OnKeyUp )
    EVT_CHAR ( ObjectGrid::OnChar )
    EVT_GRID_LABEL_LEFT_CLICK ( ObjectGrid::OnColHeadLeftClick )
wxEND_EVENT_TABLE()

bool ObjectGrid::OnCellLeftClick(wxGridEvent& event, int row, int col, ConfigOptionType type)
{
    if (type != coBool)
        return false;

    bool consumed = false;
    wxGridCellCoords coords(row, col);

    //found the grid window for this event
    /*wxGridWindow *gridWindow =
        DevicePosToGridWindow(event.GetPosition() + m_gridWin->GetPosition());
    if ( !gridWindow )
        gridWindow = m_gridWin;
    wxPoint position = CalcGridWindowUnscrolledPosition(event.GetPosition(), gridWindow);*/

    //compute the position considering the scrolled rows
#if HAS_COL_HEADER
    wxPoint event_position = event.GetPosition();
    event_position.y -= m_colLabelHeight;

    wxPoint position = CalcGridWindowUnscrolledPosition(event_position, nullptr);
#else
    wxPoint position = CalcGridWindowUnscrolledPosition(event.GetPosition(), nullptr);
#endif

    int col_right_pos = GetColRight(col);
    int row_up_pos = GetRowTop(row);
    int row_down_pos = GetRowBottom(row);
    int row_offset = ((row_down_pos - row_up_pos) - grid_cell_checkbox_size) /2;
    if (row_offset < 0)
        row_offset = 0;
    bool checkbox_hovered = false;
    if ((position.x > (col_right_pos - grid_cell_checkbox_size)) && (position.y >= (row_up_pos + row_offset)) && (position.y <= (row_down_pos - row_offset)))
        checkbox_hovered = true;
    //wxGridCellRenderer *render = this->GetCellRenderer(row, col);
    //wxDC temp_dc;
    //wxGridCellAttr temp_attr;
    //const wxRect checkBoxRect =
    //    wxGetContentRect(render->GetBestSize(*this, temp_attr, temp_dc, row, col),
    //                     rect, hAlign, vAlign);
    // Process the mouse down event depending on the current cursor mode. Note
    // that this assumes m_cursorMode was set in the mouse move event hendler.
    if ( m_cursorMode == WXGRID_CURSOR_SELECT_CELL)
    {
        if (!event.ShiftDown() && !event.CmdDown())
        {
            DisableCellEditControl();
            MakeCellVisible(coords);
            ClearSelection();

            if ( m_selection )
            {
                // In row or column selection mode just clicking on the cell
                // should select the row or column containing it: this is more
                // convenient for the kinds of controls that use such selection
                // mode and is compatible with 2.8 behaviour (see #12062).
                switch ( m_selection->GetSelectionMode() )
                {
                    case wxGridSelectNone:
                    case wxGridSelectCells:
                    //case wxGridSelectRowsOrColumns:
                        // nothing to do in these cases
                        //BBS: select this cell when first click
                        m_selection->SelectBlock(coords.GetRow(), coords.GetCol(),
                                             coords.GetRow(), coords.GetCol(),
                                             event);
                        consumed = true;
                        break;

                    default:
                        consumed = false;
                        break;
                }
            }
            else
                consumed = true;

            if (checkbox_hovered)
                m_waitForSlowClick = true;
            else
                m_waitForSlowClick = false;
            SetCurrentCell(coords);
        }
    }
    return consumed;
}

void ObjectGrid::OnColHeadLeftClick(wxGridEvent& event)
{
    bool consumed = false;
    int col = event.GetCol();

    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(this->GetTable());

    if (table) {
        table->sort_by_col(col);
        consumed = true;
    }
    if (!consumed)
        event.Skip();
    else
        Refresh();
}

void ObjectGrid::OnKeyDown( wxKeyEvent& event )
{
    event.Skip();
}

void ObjectGrid::paste_data( wxTextDataObject& text_data )
{
    wxString buf = text_data.GetText();
    int clip_size = buf.size();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", clip_size %1%, pasted_data %2%") %clip_size %buf;
    if (clip_size <= 0)
        return;
    int src_top_row = m_selected_block.GetTopRow();
    int src_bottom_row = m_selected_block.GetBottomRow();
    int src_left_col = m_selected_block.GetLeftCol();
    int src_right_col = m_selected_block.GetRightCol();
    int src_row_cnt = src_bottom_row - src_top_row + 1;
    int src_col_cnt = src_right_col -src_left_col + 1;

    const wxGridBlocks blocks = GetSelectedBlocks();
    wxGridBlocks::iterator iter = blocks.begin();
    wxGridBlockCoords selection;
    if (iter == blocks.end())
    {
        // No selection, copy just the current cell.
        if (m_currentCellCoords == wxGridNoCellCoords)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", no selection and no current , can not paste");
            return;
        }

        selection = wxGridBlockCoords(GetGridCursorRow(),
            GetGridCursorCol(),
            GetGridCursorRow(),
            GetGridCursorCol());
    }
    else // We do have at least one selected block.
    {
        selection = *blocks.begin();
    }

    int dst_top_row = selection.GetTopRow();
    int dst_bottom_row = selection.GetBottomRow();
    int dst_left_col = selection.GetLeftCol();
    int dst_right_col = selection.GetRightCol();
    int dst_row_cnt = dst_bottom_row - dst_top_row + 1;
    int dst_col_cnt = dst_right_col - dst_left_col + 1;
    wxArrayString string_array;

    auto split = [](wxString& source, wxArrayString& array) {
        wxString temp = source;
        wxChar split_char1 = '\t';
        wxChar split_char2 = '\n';
        bool finished = false;
        while (!finished && (temp.Length() > 0)) {
            int pos = temp.find(split_char2);
            if (pos == 0)
            {
                temp = temp.substr(1);
                continue;
            }

            wxString temp_line;
            if (pos == wxString::npos)
            {
                temp_line = temp;
                finished = true;
            }
            else
            {
                temp_line = temp.substr(0, pos);
                temp = temp.substr(pos+1);
            }

            while(true) {
                pos = temp_line.find(split_char1);
                if (pos == 0)
                {
                    temp_line = temp_line.substr(1);
                    continue;
                }
                else if (pos == wxString::npos)
                {
                    if (temp_line.Length() > 0)
                    {
                        array.push_back(temp_line.Trim());
                        break;
                    }
                }
                else
                {
                    array.push_back(temp_line.substr(0, pos));
                    temp_line = temp_line.substr(pos+1);
                }
            }
        }
    };

    ObjectGridTable* grid_table = dynamic_cast <ObjectGridTable * >(GetTable());
    if ((src_row_cnt == 1) && (src_col_cnt == 1))
    {
        if ((dst_col_cnt != 1) || (dst_left_col != src_left_col)) {
            wxLogWarning(_L("one cell can only be copied to one or multiple cells in the same column"));
        }
        else {
            split(buf, string_array);
            wxString source_string = string_array[0];
            if (string_array.GetCount() <= 0) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", can not split pasted data %1%")%buf;
                return;
            }
            for ( int i = 0; i < dst_row_cnt; i++ )
            {
                if (!this->IsReadOnly(dst_top_row+i, dst_left_col)) {
                    grid_table->SetValue(dst_top_row+i, dst_left_col, source_string);
                    grid_table->OnCellValueChanged(dst_top_row+i, dst_left_col);
                }
            }
        }
    }
    else {
        wxLogWarning(_L("multiple cells copy is not supported"));
        /*if ((src_col_cnt != 1) || (dst_left_col != src_left_col))
            wxLogWarning(_L("multiple columns copy is not supported"));
        else {
            split(buf, string_array);
            int count = string_array.GetCount();
            if ((count <= 0) || (count != src_row_cnt* src_col_cnt )){
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", can not split pasted data %1%, count %2%, src_row_cnt %3%, src_col_cnt %4%")%buf %count %src_row_cnt %src_col_cnt;
                return;
            }
            for ( int i = 0; i < src_row_cnt; i++ )
                for ( int j = 0; j < src_col_cnt; j++ )
                {
                    if (!this->IsReadOnly(dst_top_row+i, dst_left_col+j)) {
                        grid_table->SetValue(dst_top_row+i, dst_left_col+j, string_array[i*src_col_cnt+j]);
                        grid_table->OnCellValueChanged(dst_top_row+i, dst_left_col+j);
                    }
                }
        }*/
    }
    this->ForceRefresh();
}

void ObjectGrid::OnKeyUp( wxKeyEvent& event )
{
    // see include/wx/defs.h enum wxKeyCode
    int keyCode = event.GetKeyCode();
    int ctrlMask = wxMOD_CONTROL;
    int shiftMask = wxMOD_SHIFT;
    // Coordinates of the selected block to copy to clipboard.
    wxGridBlockCoords selection;
    wxTextDataObject text_data;

    if ((event.GetModifiers() & ctrlMask) != 0) {
        // CTRL is pressed
        switch (keyCode) {
            case 'c':
            case 'C':
                {
                    // Check if we have any selected blocks and if we don't
                    // have too many of them.
                    const wxGridBlocks blocks = GetSelectedBlocks();
                    wxGridBlocks::iterator iter = blocks.begin();
                    if (iter == blocks.end())
                    {
                        // No selection, copy just the current cell.
                        if (m_currentCellCoords == wxGridNoCellCoords)
                        {
                            // But we don't even have it -- nothing to do then.
                            event.Skip();
                            break;
                        }

                        selection = wxGridBlockCoords(GetGridCursorRow(),
                            GetGridCursorCol(),
                            GetGridCursorRow(),
                            GetGridCursorCol());
                    }
                    else // We do have at least one selected block.
                    {
                        selection = *blocks.begin();

                    }
                    m_selected_block = selection;
                    break;
                }

            case 'v':
            case 'V':
                //
                wxTheClipboard->GetData(text_data);
                paste_data(text_data);

                break;

            case 'f':
            case 'F':
                //TODO: search
                break;

            case 'z':
            case 'Z':
                //TODO:
                break;

            default:
                event.Skip();
        }
    }
}

void ObjectGrid::OnChar( wxKeyEvent& event )
{
    event.Skip();
}

void ObjectGrid::DrawColLabels( wxDC& dc,const wxArrayInt& cols )
{
    wxGrid::DrawColLabels( dc, cols );
}

void ObjectGrid::DrawColLabel(wxDC& dc, int col)
{
    if ( GetColWidth(col) <= 0 || m_colLabelHeight <= 0 )
        return;

    ObjectGridTable *table = dynamic_cast<ObjectGridTable *>(this->GetTable());

    if (table) {
        ObjectGridTable::ObjectGridCol* grid_col = table->get_grid_col(col);
        if (grid_col->b_icon)
            return;
        ObjectGridTable::ObjectGridCol* grid_next_col = table->get_grid_col(col+1);
        int colLeft = GetColLeft(col);
        int colWidth;

        if (grid_next_col&&grid_next_col->b_icon)
            colWidth = GetColWidth(col) + GetColWidth(col + 1);
        else
            colWidth = GetColWidth(col);

        wxRect rect(colLeft, 0, colWidth, m_colLabelHeight);
        wxGridColumnHeaderRendererDefault render;

        // It is reported that we need to erase the background to avoid display
        // artefacts, see #12055.
        {
            wxDCBrushChanger setBrush(dc, m_colLabelWin->GetBackgroundColour());
            wxDCPenChanger setPen(dc, *wxTRANSPARENT_PEN);
            dc.DrawRectangle(rect);
        }

        //render.DrawBorder(*this, dc, rect);

        int hAlign, vAlign;
        GetColLabelAlignment(&hAlign, &vAlign);
        const int orient = GetColLabelTextOrientation();

        render.DrawLabel(*this, dc, GetColLabelValue(col), rect, hAlign, vAlign, orient);
    }
}


// ----------------------------------------------------------------------------
// ObjectGridTable
// ----------------------------------------------------------------------------
std::string ObjectGridTable::category_all = "All";
std::string ObjectGridTable::plate_outside = L("Outside");

ObjectGridTable::~ObjectGridTable()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", this %1%, row_data size %2%") %this % m_grid_data.size();
    for ( int index = 0; index < m_grid_data.size(); index++ )
    {
        if (m_grid_data[index])
            delete (m_grid_data[index]);
    }
    m_grid_data.clear();

    for ( int index = 0; index < m_col_data.size(); index++ )
    {
        if (m_col_data[index])
            delete (m_col_data[index]);
    }

    m_selected_cells.clear();
}

/* ObjectGridTable related class */
wxString ObjectGridTable::GetTypeName(int row, int col)
{
    if (row == 0)
        return wxGRID_VALUE_STRING;

    ObjectGridCol* col_object = m_col_data[col];
    ConfigOptionType option_type = col_object->type;
    wxString type_name;

    switch (option_type)
    {
        case coString:
            return wxGRID_VALUE_STRING;
        case coBool:
            return wxGRID_VALUE_BOOL;
        case coInt:
            return wxGRID_VALUE_NUMBER;
        case coFloat:
        case coPercent:
            return wxGRID_VALUE_FLOAT;
        case coEnum:
            return wxGRID_VALUE_CHOICE;
        default:
            break;
    }

    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unknown column");
    return wxEmptyString;
}

int ObjectGridTable::GetNumberRows()
{
    return m_grid_data.size() + 1;
}

int ObjectGridTable::GetNumberCols()
{
    return m_col_data.size();
}

bool ObjectGridTable::IsEmptyCell( int row, int col )
{
    if (row == 0)
        return false;

    ObjectGridCol* col_object = m_col_data[col];
    ObjectGridRow* row_object = m_grid_data[row - 1];

    if (col_object->b_for_object && (row_object->row_type == row_volume))
        return true;

    return false;
}

bool ObjectGridTable::CanGetValueAs(int row, int col, const wxString& typeName)
{
    //row 0 always use string type for label
    if (row == 0) {
        if ( typeName == wxGRID_VALUE_STRING )
            return true;
        else
            return false;
    }

    //other rows for data
    ObjectGridCol* col_object = m_col_data[col];
    ObjectGridRow* row_object = m_grid_data[row - 1];
    ConfigOptionType option_type = col_object->type;

    if (col_object->b_icon)
        return false;
    if (col_object->b_for_object && (row_object->row_type == row_volume))
        return false;

    if ( typeName == wxGRID_VALUE_STRING )
    {
        if (option_type == coString)
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_BOOL )
    {
        if (option_type == coBool)
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_NUMBER )
    {
        if (option_type == coInt)
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_FLOAT  )
    {
        if ((option_type == coFloat) || (option_type == coPercent))
            return true;
        else
            return false;
    }
    else if ( typeName == wxGRID_VALUE_CHOICE )
    {
        if (option_type == coEnum)
            return true;
        else
            return false;
    }
    else
    {
        return false;
    }
}

bool ObjectGridTable::CanSetValueAs( int row, int col, const wxString& typeName )
{
    return CanGetValueAs(row, col, typeName);
}

wxString ObjectGridTable::GetValue (int row, int col)
{
    if (!m_data_valid)
        return wxString();

    //row 0 always use string type for label
    if (row == 0) {
        switch ((GridColType)col)
        {
            case col_plate_index:
                return "Plate";
                /* case col_assemble_name:
                     return "Module";*/
            case col_name:
                return "Name";
            case col_printable:
                return "Printable";
            case col_filaments:
                return "Filament";
            case col_layer_height:
                return "Layer height";
            case col_wall_loops:
                return "Perimeter";
            case col_fill_density:
                return "Infill density(%)";
            case col_enable_support:
                return "Support";
            case col_brim_type:
                return "Brim";
            case col_speed_perimeter:
                return "Perimeter speed";
            default:
                return wxString();
        }
    }

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[(GridColType)col];
    if (grid_col->b_for_object && (grid_row->row_type == row_volume))
        return wxString();

    ConfigOption& option = (*grid_row)[(GridColType)col];
    if (grid_col->type == coEnum) {
        if (col == col_brim_type) {
            ConfigOptionEnum<BrimType>& option_value = dynamic_cast<ConfigOptionEnum<BrimType>&>(option);
            if (option_value.value < grid_col->choice_count)
               return grid_col->choices[option_value.value];
        }
        else if (col == col_filaments) {
            ConfigOptionInt& option_value = dynamic_cast<ConfigOptionInt&>(option);
            if ((option_value.value - 1) < grid_col->choice_count) {
               //return grid_col->choices[(option_value.value > 0)?option_value.value - 1: 0];
                int index = (option_value.value > 0) ? option_value.value - 1 : 0;
               return convert_filament_string(index, grid_col->choices[index]);
               //return grid_col->choices[index];
            }
        }
    }
    else if (grid_col->type == coBool) {
        ConfigOptionBool& option_value = dynamic_cast<ConfigOptionBool&>(option);
        return option_value.value?"-":"-";
    }
    else if (grid_col->type == coInt) {
        ConfigOptionInt& option_value = dynamic_cast<ConfigOptionInt&>(option);
        return wxString::Format("%d", option_value.value);
    }
    else if (grid_col->type == coFloat) {
        ConfigOptionFloat& option_value = dynamic_cast<ConfigOptionFloat&>(option);
        return wxString::Format("%.2f", option_value.value);
    }
    else if (grid_col->type == coPercent) {
        ConfigOptionPercent& option_value = dynamic_cast<ConfigOptionPercent&>(option);
        return wxString::Format("%.2f", option_value.value);
    }
    else {
        try {
            ConfigOptionString& option_value = dynamic_cast<ConfigOptionString&>(option);
            if (grid_row->row_type == row_volume)
                return GUI::from_u8(std::string("  ") + option_value.value);
            else
                return GUI::from_u8(option_value.value);
        }
        catch(...) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("row %1%, col %2%, type %3% ")%row %col %grid_col->type;
            return wxString();
        }
    }

    return wxString();
}

void ObjectGridTable::update_filament_to_config(ModelConfig* config, std::string& key, ConfigOption& new_value,  ConfigOption& ori_value, bool is_object)
{
    if (!is_object)
        update_value_to_config(config, key, new_value, ori_value);
    else {
        //always create extruder in object config
        config->set_key_value(key, new_value.clone());
        config->touch();
    }
}


void ObjectGridTable::update_value_to_config(ModelConfig* config, std::string& key, ConfigOption& new_value,  ConfigOption& ori_value)
{
    if (!config->has(key))
    {
        if (ori_value != new_value)
            config->set_key_value(key, new_value.clone());
    }
    else {
        if (ori_value != new_value)
            config->set_key_value(key, new_value.clone());
        else
            config->erase(key);
    }
    config->touch();
}

void ObjectGridTable::update_volume_values_from_object(int row, int col)
{
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    bool need_refresh = false;
    DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    if (grid_row->row_type == row_object) {
        int next_row = row + 1;
        while ((next_row - 1) < m_grid_data.size())
        {
            ObjectGridRow* part_row = m_grid_data[next_row - 1];
            if (part_row->row_type == row_volume) {
                if (col == col_filaments) {
                    auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(part_row->config->option(m_col_data[col]->key));
                    if (extruder_id_ptr) {
                        part_row->filaments = *extruder_id_ptr;
                        if (part_row->filaments == grid_row->filaments) {
                            part_row->config->erase(m_col_data[col_filaments]->key);
                        }
                    }
                    else
                        part_row->filaments = grid_row->filaments;
                    part_row->ori_filaments = grid_row->filaments;
                }
                else
                    reload_part_data(part_row, grid_row, m_col_data[col]->category, global_config);
                next_row++;
                need_refresh = true;
            }
            else
                break;
        }
    }

    if (need_refresh)
        m_panel->m_object_grid->ForceRefresh();

    return;
}


void ObjectGridTable::update_value_to_object(Model* model, ObjectGridRow* grid_row, int col)
{
    ModelObject* object = model->objects[grid_row->object_id];
    ModelVolume* volume = nullptr;
    std::string* name_ptr = nullptr;
    std::string name_value;

    if (!object) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", object_id %1%, volume_id %2%, can not find modelObject anymore!")%grid_row->object_id %grid_row->volume_id;
        return;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", object_id %1%, volume_id %2%, col %3%, row_type %4%")%grid_row->object_id %grid_row->volume_id %col %grid_row->row_type;

    if (grid_row->row_type != row_object) {
        volume = object->volumes[grid_row->volume_id];
        if (col == col_name) {
            name_ptr = &(volume->name);
            name_value = grid_row->name.value;
        }
    }
    else {
        if (col == col_name) {
            name_ptr = &(object->name);
            name_value = grid_row->name.value;
        }
       /* else if (col == col_assemble_name) {
            name_ptr = &(object->module_name);
            name_value = grid_row->assemble_name.value;
        }*/
        else if (col == col_printable) {
            object->printable = grid_row->printable.value;
            object->instances[0]->printable = object->printable;

            std::vector<ObjectVolumeID> object_volume_ids;
            ObjectVolumeID object_volume_id;
            object_volume_id.object = object;
            object_volume_id.volume = nullptr;
            object_volume_ids.push_back(object_volume_id);
            wxGetApp().obj_list()->printable_state_changed(object_volume_ids);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", change object %1%'s printable to %2%")%object->module_name %object->printable;
        }
    }

    if ((name_ptr) && (*name_ptr != name_value)) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", change name from %1% to %2%!")%name_ptr->c_str() %name_value.c_str();
        *name_ptr = name_value;
        //notify object list
        wxGetApp().obj_list()->update_name_for_items();
    }
}

void ObjectGridTable::SetValue( int row, int col, const wxString& value )
{
    if (row == 0)
        return;
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];
    ObjectList* obj_list = wxGetApp().obj_list();
    if (grid_col->type == coEnum) {
        int enum_value = 0;
        for (int i = 0; i < grid_col->choice_count; i++)
        {
            if (grid_col->choices[i] == value) {
                enum_value = i;
                break;
            }
            else if ((col == col_filaments)&&(value.substr(0,2) == grid_col->choices[i].substr(0,2))){
                enum_value = i;
                break;
            }
        }
        if (col == col_brim_type) {
            ConfigOptionEnum<BrimType>& option_value = dynamic_cast<ConfigOptionEnum<BrimType>&>((*grid_row)[(GridColType)col]);
            ConfigOptionEnum<BrimType>& option_ori_value = dynamic_cast<ConfigOptionEnum<BrimType>&>((*grid_row)[(GridColType)(col + 1)]);

            option_value.value = (BrimType)enum_value;
            update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
        }
        else if (col == col_filaments) {
            ConfigOptionInt& option_value = dynamic_cast<ConfigOptionInt&>((*grid_row)[(GridColType)col]);
            ConfigOptionInt& option_ori_value = dynamic_cast<ConfigOptionInt&>((*grid_row)[(GridColType)(col + 1)]);

            option_value.value = enum_value + 1;
            update_filament_to_config(grid_row->config, grid_col->key, option_value, option_ori_value, (grid_row->row_type == row_object));
            update_volume_values_from_object(row, col);
            wxGetApp().obj_list()->update_filament_values_for_items(m_panel->m_filaments_count);
            //m_panel->m_plater->update();
        }
    }
    else if (grid_col->type == coBool) {
        ConfigOptionBool &option_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)col]);
        ConfigOptionBool &option_ori_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)(col+1)]);

        option_value.value = (wxAtoi(value) == 1)?true:false;

        if (grid_col->b_from_config) {
            update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
        }
        else {
            update_value_to_object(m_panel->m_model, grid_row, col);
        }
    }
    else if (grid_col->type == coFloat) {
        ConfigOptionFloat &option_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)col]);
        ConfigOptionFloat &option_ori_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)(col+1)]);

        double  double_value;
        value.ToDouble(&double_value);
        option_value.value = (float)double_value;

        update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
    }
    else if (grid_col->type == coInt) {
        ConfigOptionInt &option_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)col]);
        ConfigOptionInt &option_ori_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)(col+1)]);
        long  int_value;
        value.ToLong(&int_value);

        option_value.value = (int) int_value;
        update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
    }
    else if (grid_col->type == coPercent) {
        double  double_value;
        value.ToDouble(&double_value);
        if ((double_value > 100.f) || (double_value < 0.f))
            return;
        ConfigOptionFloat &option_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)col]);
        ConfigOptionFloat &option_ori_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)(col+1)]);

        option_value.value = (float)double_value;

        update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
    }
    else {
        if (grid_col->b_from_config) {
            ConfigOptionString& option_value = dynamic_cast<ConfigOptionString&>((*grid_row)[(GridColType)col]);
            ConfigOptionString& option_ori_value = dynamic_cast<ConfigOptionString&>((*grid_row)[(GridColType)(col + 1)]);

            option_value.value = into_u8(value);
            update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
        }
        else {
            ConfigOptionString& option_value = dynamic_cast<ConfigOptionString&>((*grid_row)[(GridColType)col]);

            if (grid_row->row_type == row_volume) {
                //std::string new_value = value.ToStdString();
                std::string new_value = into_u8(value);
                size_t pos = new_value.find_first_not_of(' ');
                if (pos > 0)
                    new_value.erase(0, pos);
                option_value.value = new_value;
            }
            else
                option_value.value = into_u8(value);
            update_value_to_object(m_panel->m_model, grid_row, col);
        }
    }
}

long ObjectGridTable::GetValueAsLong( int row, int col )
{
    if (!m_data_valid)
        return 0;

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ConfigOptionInt &option_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)col]);
    return (long)option_value.getInt();
}

bool ObjectGridTable::GetValueAsBool( int row, int col )
{
    if (!m_data_valid)
        return 0;

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ConfigOptionBool &option_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)col]);
    return option_value.getBool();
}

double ObjectGridTable::GetValueAsDouble( int row, int col )
{
    if (!m_data_valid)
        return 0;

    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ConfigOptionFloat &option_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)col]);
    return (double )option_value.getFloat();
}

void ObjectGridTable::SetValueAsLong( int row, int col, long value )
{
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];
    ConfigOptionInt &option_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)col]);
    ConfigOptionInt &option_ori_value = dynamic_cast<ConfigOptionInt &>((*grid_row)[(GridColType)(col+1)]);

    option_value.value = (int) value;
    update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);

    return;
}

void ObjectGridTable::SetValueAsBool( int row, int col, bool value )
{
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];
    ConfigOptionBool &option_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)col]);
    ConfigOptionBool &option_ori_value = dynamic_cast<ConfigOptionBool &>((*grid_row)[(GridColType)(col+1)]);

    option_value.value = (int) value;

    if (grid_col->b_from_config) {
        update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);
    }
    else {
        update_value_to_object(m_panel->m_model, grid_row, col);
    }
    m_panel->m_object_grid->ForceRefresh();

    return;
}

void ObjectGridTable::SetValueAsDouble(int row, int col, double value)
{
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    ObjectGridCol* grid_col = m_col_data[col];

    if (grid_col->type == coPercent) {
        if ((value > 100.f) || (value < 0.f))
            return;
    }
    ConfigOptionFloat &option_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)col]);
    ConfigOptionFloat &option_ori_value = dynamic_cast<ConfigOptionFloat &>((*grid_row)[(GridColType)(col+1)]);

    option_value.value = (float)value;

    update_value_to_config(grid_row->config, grid_col->key, option_value, option_ori_value);

    return;
}

void ObjectGridTable::SetColLabelValue( int col, const wxString& value )
{
    if ( col > (int)(m_colLabels.GetCount()) - 1 )
    {
        int n = m_colLabels.GetCount();
        int i;

        for ( i = n; i <= col; i++ )
        {
            m_colLabels.Add( wxGridTableBase::GetColLabelValue(i) );
        }
    }

    m_colLabels[col] = value;
}

wxString ObjectGridTable::GetColLabelValue( int col )
{
    if ( col > (int)(m_colLabels.GetCount()) - 1 )
    {
        // using default label
        //
        return wxGridTableBase::GetColLabelValue( col );
    }
    else
    {
        return m_colLabels[col];
    }
}

void ObjectGridTable::release_object_configs()
{
    if (m_grid_data.size() > 0)
    {
        for (int i = 0; i < m_grid_data.size(); i ++)
        {
            delete m_grid_data[i];
        }
        m_grid_data.clear();
    }

    if (m_col_data.size() > 0)
    {
        for (int i = 0; i < m_col_data.size(); i ++)
        {
            delete m_col_data[i];
        }
        m_col_data.clear();
    }

    m_data_valid = false;

    return;
}

//convert the filament str to short and readable
wxString ObjectGridTable::convert_filament_string(int index, wxString& filament_str)
{
    wxString result_str;
    if (filament_str.find("PLA") !=  wxNOT_FOUND ) {
        //PLA
        result_str = wxString(std::to_string(index+1) + ": PLA");
    }
    else if (filament_str.find("ABS") != wxNOT_FOUND ) {
        //ABS
        result_str = wxString(std::to_string(index+1) + ": ABS");
    }
    else if (filament_str.find("PETG") != wxNOT_FOUND ) {
        //PETG
        result_str= wxString(std::to_string(index+1) + ": PETG");
    }
    else if (filament_str.find("TPU") != wxNOT_FOUND ) {
        //TPU
        result_str = wxString(std::to_string(index+1) + ": TPU");
    }
    else
        result_str = filament_str;

    return result_str;
}

static wxString brim_choices[] =
{
    L("Auto"),
    L("Manual"),
    L("No-brim"),
    //L("Inner brim only"),
    //L("Outer and inner brim")
};

void ObjectGridTable::init_cols(ObjectGrid *object_grid)
{
    const float font_size = 1.5f * wxGetApp().em_unit();


    // printable for object
    ObjectGridCol* col       = new ObjectGridCol(coBool, "printable", ObjectGridTable::category_all, true, false, true, false, wxALIGN_CENTRE);
    col->size = object_grid->GetTextExtent(L("Printable")).x;
    m_col_data.push_back(col);

    // reset icon for printable
    col = new ObjectGridCol(coBool, "printable_reset", ObjectGridTable::category_all, true, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //first column for plate_index
    col = new ObjectGridCol(coString, "plate_index", L(" "), true, false, false, false, wxALIGN_CENTRE); //bool only_object, bool icon, bool edit, bool config
    m_col_data.push_back(col);

    //second column for module name
    /*col = new ObjectGridCol(coString, "assemble_name", ObjectGridTable::category_all, true, false, true, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);*/

    //3th column: for object/volume name
    col = new ObjectGridCol(coString, "name", ObjectGridTable::category_all, false, false, true, false, wxALIGN_CENTRE);
    col->size = 200;
    m_col_data.push_back(col);

    //object/volume extruder_id
    col = new ObjectGridCol(coEnum, "extruder", ObjectGridTable::category_all, false, false, true, true, wxALIGN_CENTRE);
    col->size = 128;
    //the spec now guarantees vectors store their elements contiguously
    col->choices = &m_panel->m_filaments_name[0];
    col->choice_count = m_panel->m_filaments_count;
    m_col_data.push_back(col);

    //reset icon for extruder_id
    col = new ObjectGridCol(coEnum, "extruder_reset", ObjectGridTable::category_all, false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object layer height
    col = new ObjectGridCol(coFloat, "layer_height", L("Quality"), true, false, true, true, wxALIGN_CENTRE);
    col->size = object_grid->GetTextExtent(L("Layer height")).x;
    m_col_data.push_back(col);

    //reset icon for extruder_id
    col = new ObjectGridCol(coFloat, "layer_height_reset", L("Quality"), true, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object/volume perimeters
    col = new ObjectGridCol(coInt, "wall_loops", L("Strength"), false, false, true, true, wxALIGN_CENTRE);
    col->size = object_grid->GetTextExtent(L("Wall loops")).x;
    m_col_data.push_back(col);

    //reset icon for perimeters
    col = new ObjectGridCol(coInt, "wall_loops_reset", L("Strength"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object/volume fill density
    col = new ObjectGridCol(coPercent, "sparse_infill_density", L("Strength"), false, false, true, true, wxALIGN_CENTRE);
    col->size = object_grid->GetTextExtent(L("Infill density(%)")).x;
    m_col_data.push_back(col);

    //reset icon for fill density
    col = new ObjectGridCol(coPercent, "fill_density_reset", L("Strength"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //support material
    col = new ObjectGridCol(coBool, "enable_support", L("Support"), true, false, true, true, wxALIGN_CENTRE);
    col->size = object_grid->GetTextExtent(L("Support")).x;
    m_col_data.push_back(col);

    //reset icon for support material
    col = new ObjectGridCol(coBool, "support_reset", L("Support"), true, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //Bed Adhesion
    col = new ObjectGridCol(coEnum, "brim_type", L("Support"), true, false, true, true, wxALIGN_CENTRE);
    col->size = object_grid->GetTextExtent(L("Auto Brim")).x + 8; //add 8 for border
    col->choices = brim_choices;
    col->choice_count = WXSIZEOF(brim_choices);
    m_col_data.push_back(col);

    //reset icon for Bed Adhesion
    col = new ObjectGridCol(coEnum, "brim_type_reset", L("Support"), true, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    //object/volume speed
    col = new ObjectGridCol(coFloat, "inner_wall_speed", L("Speed"), false, false, true, true, wxALIGN_CENTRE);
    col->size = object_grid->GetTextExtent(L("Inner wall speed")).x;
    m_col_data.push_back(col);

    //reset icon for speed
    col = new ObjectGridCol(coFloat, "inner_wall_speed_reset", L("Speed"), false, true, false, false, wxALIGN_CENTRE);
    m_col_data.push_back(col);

    return;
}

void ObjectGridTable::construct_object_configs(ObjectGrid *object_grid)
{
    //release first
    release_object_configs();

    //init cols
    init_cols(object_grid);

    if (!m_panel->m_model)
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "found invalid m_model, should not happen" << std::endl;
        return;
    }
    int object_count = m_panel->m_model->objects.size();
    PartPlateList& partplate_list = m_panel->m_plater->get_partplate_list();
    DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const DynamicPrintConfig* plater_config = m_panel->m_plater->config();
    const DynamicPrintConfig&  filament_config = *plater_config;

    for (int i = 0; i < object_count; i++)
    {
        ModelObject* object = m_panel->m_model->objects[i];
        ObjectGridRow* object_grid = new ObjectGridRow(i, 0, row_object);
        int plate_index;

        object_grid->config = &(object->config);
        object_grid->name.value = object->name;
        object_grid->ori_name = object_grid->name;
        plate_index = partplate_list.find_instance_belongs(i, 0);
        if (plate_index == -1)
            object_grid->plate_index.value = ObjectGridTable::plate_outside;
        else
            object_grid->plate_index.value = /*std::string("Plate ") + */std::to_string(plate_index+1);
       /* object_grid->assemble_name.value = object->module_name;
        object_grid->ori_assemble_name = object_grid->assemble_name;*/
        object_grid->printable.value = object->instances[0]->printable;
        object_grid->ori_printable.value = object_grid->printable.value;
        //auto extruder_id_ptr = get_object_config_value<ConfigOptionInt>(filament_config, object_grid->config, m_col_data[col_filaments]->key);
        auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(object_grid->config->option(m_col_data[col_filaments]->key));
        if (extruder_id_ptr) {
            object_grid->filaments = *extruder_id_ptr;
            if (object_grid->filaments.value == 0) {
                object_grid->filaments.value = 1;
                object_grid->config->set_key_value(m_col_data[col_filaments]->key, object_grid->filaments.clone());
            }
        }
        else {
            object_grid->filaments.value = 1;
            object_grid->config->set_key_value(m_col_data[col_filaments]->key, object_grid->filaments.clone());
        }
        object_grid->ori_filaments.value = 1;

        object_grid->layer_height = *(get_object_config_value<ConfigOptionFloat>(global_config, object_grid->config, m_col_data[col_layer_height]->key));
        object_grid->ori_layer_height = *(global_config.option<ConfigOptionFloat>(m_col_data[col_layer_height]->key));
        object_grid->wall_loops = *(get_object_config_value<ConfigOptionInt>(global_config, object_grid->config, m_col_data[col_wall_loops]->key));
        object_grid->ori_wall_loops = *(global_config.option<ConfigOptionInt>(m_col_data[col_wall_loops]->key));
        object_grid->sparse_infill_density = *(get_object_config_value<ConfigOptionPercent>(global_config, object_grid->config, m_col_data[col_fill_density]->key));
        object_grid->ori_fill_density = *(global_config.option<ConfigOptionPercent>(m_col_data[col_fill_density]->key));
        object_grid->enable_support = *(get_object_config_value<ConfigOptionBool>(global_config, object_grid->config, m_col_data[col_enable_support]->key));
        object_grid->ori_enable_support = *(global_config.option<ConfigOptionBool>(m_col_data[col_enable_support]->key));
        object_grid->brim_type = *(get_object_config_value<ConfigOptionEnum<BrimType>>(global_config, object_grid->config, m_col_data[col_brim_type]->key));
        object_grid->ori_brim_type = *(global_config.option<ConfigOptionEnum<BrimType>>(m_col_data[col_brim_type]->key));
        object_grid->speed_perimeter = *(get_object_config_value<ConfigOptionFloat>(global_config, object_grid->config, m_col_data[col_speed_perimeter]->key));
        object_grid->ori_speed_perimeter = *(global_config.option<ConfigOptionFloat>(m_col_data[col_speed_perimeter]->key));
        m_grid_data.push_back(object_grid);

        int volume_count = object->volumes.size();
        if (volume_count <= 1)
            continue;

        for (int j = 0; j < volume_count; j++)
        {
            ModelVolume* volume = object->volumes[j];
            ObjectGridRow* volume_grid = new ObjectGridRow(i, j, row_volume);
            volume_grid->config = &(volume->config);
            volume_grid->name.value = volume->name;
            size_t pos = volume_grid->name.value.find_first_not_of(' ');
            if (pos > 0)
                volume_grid->name.value.erase(0, pos);
            volume_grid->ori_name = volume_grid->name;
            plate_index = partplate_list.find_instance_belongs(i, 0);
            if (plate_index == -1)
                volume_grid->plate_index.value = ObjectGridTable::plate_outside;
            else
                volume_grid->plate_index.value = /*std::string("Plate ") +*/ std::to_string(plate_index+1);
           /* volume_grid->assemble_name.value = object->module_name;
            volume_grid->ori_assemble_name = volume_grid->assemble_name;*/
            volume_grid->printable.value = object->instances[0]->printable;
            volume_grid->ori_printable.value = volume_grid->printable.value;
            //auto extruder_id_ptr = get_volume_config_value<ConfigOptionInt>(filament_config, object_grid->config, volume_grid->config, m_col_data[col_filaments]->key);
            auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(volume_grid->config->option(m_col_data[col_filaments]->key));
            if (extruder_id_ptr) {
                volume_grid->filaments = *extruder_id_ptr;
                if ((volume_grid->filaments.value == 0) || (volume_grid->filaments == object_grid->filaments)) {
                    volume_grid->config->erase(m_col_data[col_filaments]->key);
                    volume_grid->filaments = object_grid->filaments;
                }
            }
            else
                volume_grid->filaments = object_grid->filaments;
            volume_grid->ori_filaments = object_grid->filaments;
            volume_grid->layer_height = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_grid->config, volume_grid->config, m_col_data[col_layer_height]->key));
            volume_grid->ori_layer_height = object_grid->layer_height;
            volume_grid->wall_loops = *(get_volume_config_value<ConfigOptionInt>(global_config, object_grid->config, volume_grid->config, m_col_data[col_wall_loops]->key));
            volume_grid->ori_wall_loops = object_grid->wall_loops;
            volume_grid->sparse_infill_density = *(get_volume_config_value<ConfigOptionPercent>(global_config, object_grid->config, volume_grid->config, m_col_data[col_fill_density]->key));
            volume_grid->ori_fill_density = object_grid->sparse_infill_density;
            volume_grid->enable_support = *(get_volume_config_value<ConfigOptionBool>(global_config, object_grid->config, volume_grid->config, m_col_data[col_enable_support]->key));
            volume_grid->ori_enable_support = object_grid->enable_support;
            volume_grid->brim_type = *(get_volume_config_value<ConfigOptionEnum<BrimType>>(global_config, object_grid->config, volume_grid->config, m_col_data[col_brim_type]->key));
            volume_grid->ori_brim_type = object_grid->brim_type;
            volume_grid->speed_perimeter = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_grid->config, volume_grid->config, m_col_data[col_speed_perimeter]->key));
            volume_grid->ori_speed_perimeter = object_grid->speed_perimeter;
            m_grid_data.push_back(volume_grid);
        }
    }

    m_data_valid = true;

    return;
}

void ObjectGridTable::SetSelection(int object_id, int volume_id)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", set selection to object %1% part %2%") %object_id %volume_id;
    //invalid object, skip
    if ((object_id == -1)&&(volume_id == -1))
        return;

    for (int index = 0; index <  m_grid_data.size(); index++)
    {
        ObjectGridRow* row = m_grid_data[index];
        if (row->object_id == object_id) {
            if ((volume_id == -1) || ((volume_id == row->volume_id)&&(row->row_type == row_volume))) {
                m_panel->m_object_grid->SelectRow(index+1);
                m_panel->m_object_grid->MakeCellVisible(index + 1, 0);
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", found row %1%") %index;
                break;
            }
        }
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", can not find row");
}

void ObjectGridTable::reload_object_data(ObjectGridRow* grid_row, const std::string& category, DynamicPrintConfig&  global_config)
{
    if (category == ObjectGridTable::category_all) {
        grid_row->layer_height = *(get_object_config_value<ConfigOptionFloat>(global_config, grid_row->config, m_col_data[col_layer_height]->key));
        grid_row->ori_layer_height = *(global_config.option<ConfigOptionFloat>(m_col_data[col_layer_height]->key));
        grid_row->wall_loops = *(get_object_config_value<ConfigOptionInt>(global_config, grid_row->config, m_col_data[col_wall_loops]->key));
        grid_row->ori_wall_loops = *(global_config.option<ConfigOptionInt>(m_col_data[col_wall_loops]->key));
        grid_row->sparse_infill_density = *(get_object_config_value<ConfigOptionPercent>(global_config, grid_row->config, m_col_data[col_fill_density]->key));
        grid_row->ori_fill_density = *(global_config.option<ConfigOptionPercent>(m_col_data[col_fill_density]->key));
        grid_row->enable_support = *(get_object_config_value<ConfigOptionBool>(global_config, grid_row->config, m_col_data[col_enable_support]->key));
        grid_row->ori_enable_support = *(global_config.option<ConfigOptionBool>(m_col_data[col_enable_support]->key));
        grid_row->brim_type = *(get_object_config_value<ConfigOptionEnum<BrimType>>(global_config, grid_row->config, m_col_data[col_brim_type]->key));
        grid_row->ori_brim_type = *(global_config.option<ConfigOptionEnum<BrimType>>(m_col_data[col_brim_type]->key));
        grid_row->speed_perimeter = *(get_object_config_value<ConfigOptionFloat>(global_config, grid_row->config, m_col_data[col_speed_perimeter]->key));
        grid_row->ori_speed_perimeter = *(global_config.option<ConfigOptionFloat>(m_col_data[col_speed_perimeter]->key));
    }
    else if (category == L("Quality")) {
        grid_row->layer_height = *(get_object_config_value<ConfigOptionFloat>(global_config, grid_row->config, m_col_data[col_layer_height]->key));
        grid_row->ori_layer_height = *(global_config.option<ConfigOptionFloat>(m_col_data[col_layer_height]->key));
    }
    else if (category == L("Strength")) {
        grid_row->wall_loops = *(get_object_config_value<ConfigOptionInt>(global_config, grid_row->config, m_col_data[col_wall_loops]->key));
        grid_row->ori_wall_loops = *(global_config.option<ConfigOptionInt>(m_col_data[col_wall_loops]->key));

        grid_row->sparse_infill_density = *(get_object_config_value<ConfigOptionPercent>(global_config, grid_row->config, m_col_data[col_fill_density]->key));
        grid_row->ori_fill_density      = *(global_config.option<ConfigOptionPercent>(m_col_data[col_fill_density]->key));
    }
    else if (category == L("Support")) {
        grid_row->enable_support = *(get_object_config_value<ConfigOptionBool>(global_config, grid_row->config, m_col_data[col_enable_support]->key));
        grid_row->ori_enable_support = *(global_config.option<ConfigOptionBool>(m_col_data[col_enable_support]->key));

        grid_row->brim_type = *(get_object_config_value<ConfigOptionEnum<BrimType>>(global_config, grid_row->config, m_col_data[col_brim_type]->key));
        grid_row->ori_brim_type = *(global_config.option<ConfigOptionEnum<BrimType>>(m_col_data[col_brim_type]->key));
    }
    else if (category == L("Speed")) {
        grid_row->speed_perimeter = *(get_object_config_value<ConfigOptionFloat>(global_config, grid_row->config, m_col_data[col_speed_perimeter]->key));
        grid_row->ori_speed_perimeter = *(global_config.option<ConfigOptionFloat>(m_col_data[col_speed_perimeter]->key));
    }
}

void ObjectGridTable::reload_part_data(ObjectGridRow* volume_row, ObjectGridRow* object_row, const std::string& category, DynamicPrintConfig&  global_config)
{
    if (category == ObjectGridTable::category_all) {
        volume_row->layer_height = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_row->config, volume_row->config, m_col_data[col_layer_height]->key));
        volume_row->ori_layer_height = object_row->layer_height;
        volume_row->wall_loops = *(get_volume_config_value<ConfigOptionInt>(global_config, object_row->config, volume_row->config, m_col_data[col_wall_loops]->key));
        volume_row->ori_wall_loops = object_row->wall_loops;
        volume_row->sparse_infill_density = *(get_volume_config_value<ConfigOptionPercent>(global_config, object_row->config, volume_row->config, m_col_data[col_fill_density]->key));
        volume_row->ori_fill_density = object_row->sparse_infill_density;
        volume_row->enable_support = *(get_volume_config_value<ConfigOptionBool>(global_config, object_row->config, volume_row->config, m_col_data[col_enable_support]->key));
        volume_row->ori_enable_support = object_row->enable_support;
        volume_row->brim_type = *(get_volume_config_value<ConfigOptionEnum<BrimType>>(global_config, object_row->config, volume_row->config, m_col_data[col_brim_type]->key));
        volume_row->ori_brim_type = object_row->brim_type;
        volume_row->speed_perimeter = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_row->config, volume_row->config, m_col_data[col_speed_perimeter]->key));
        volume_row->ori_speed_perimeter = object_row->speed_perimeter;
    }
    else if (category == L("Quality")) {
        volume_row->layer_height = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_row->config, volume_row->config, m_col_data[col_layer_height]->key));
        if (volume_row->layer_height == object_row->layer_height) {
            volume_row->config->erase(m_col_data[col_layer_height]->key);
        }
        volume_row->ori_layer_height = object_row->layer_height;
    }
    else if (category == L("Strength")) {
        volume_row->wall_loops = *(get_volume_config_value<ConfigOptionInt>(global_config, object_row->config, volume_row->config, m_col_data[col_wall_loops]->key));
        if (volume_row->wall_loops == object_row->wall_loops) {
            volume_row->config->erase(m_col_data[col_wall_loops]->key);
        }
        volume_row->ori_wall_loops        = object_row->wall_loops;

        volume_row->sparse_infill_density = *(get_volume_config_value<ConfigOptionPercent>(global_config, object_row->config, volume_row->config, m_col_data[col_fill_density]->key));
        if (volume_row->sparse_infill_density == object_row->sparse_infill_density) {
            volume_row->config->erase(m_col_data[col_fill_density]->key);
        }
        volume_row->ori_fill_density = object_row->sparse_infill_density;
    }
    else if (category == L("Support")) {
        volume_row->enable_support = *(get_volume_config_value<ConfigOptionBool>(global_config, object_row->config, volume_row->config, m_col_data[col_enable_support]->key));
        if (volume_row->enable_support == object_row->enable_support) {
            volume_row->config->erase(m_col_data[col_enable_support]->key);
        }
        volume_row->ori_enable_support = object_row->enable_support;

        volume_row->brim_type = *(get_volume_config_value<ConfigOptionEnum<BrimType>>(global_config, object_row->config, volume_row->config, m_col_data[col_brim_type]->key));
        if (volume_row->brim_type == object_row->brim_type) {
            volume_row->config->erase(m_col_data[col_brim_type]->key);
        }
        volume_row->ori_brim_type = object_row->brim_type;
    }
    else if (category == L("Speed")) {
        volume_row->speed_perimeter = *(get_volume_config_value<ConfigOptionFloat>(global_config, object_row->config, volume_row->config, m_col_data[col_speed_perimeter]->key));
        if (volume_row->speed_perimeter == object_row->speed_perimeter) {
            volume_row->config->erase(m_col_data[col_speed_perimeter]->key);
        }
        volume_row->ori_speed_perimeter = object_row->speed_perimeter;
    }
}

//called by the GUI_ObjectTableSettings, to update the values on the cell, and also update data to plater
void ObjectGridTable::reload_cell_data(int row, const std::string& category)
{
    if (row == 0)
        return;
    ObjectGridRow* grid_row = m_grid_data[row - 1];
    DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;

    if (grid_row->row_type == row_object) {
        reload_object_data(grid_row, category, global_config);

        int next_row = row + 1;
        while ((next_row - 1) < m_grid_data.size())
        {
            ObjectGridRow* part_row = m_grid_data[next_row - 1];
            if (part_row->row_type == row_volume) {
                reload_part_data(part_row, grid_row, category, global_config);
                next_row++;
            }
            else
                break;
        }
    }
    else {
        int next_row = row - 1;
        ObjectGridRow* object_row = m_grid_data[next_row - 1];
        while (object_row->row_type == row_volume)
        {
            next_row --;
            object_row = m_grid_data[next_row - 1];
        }
        reload_part_data(grid_row, object_row, category, global_config);
    }
    m_panel->m_object_grid->ForceRefresh();

    ObjectVolumeID object_volume_id;
    object_volume_id.object = m_panel->m_model->objects[grid_row->object_id];
    object_volume_id.volume = (grid_row->row_type == row_object)?nullptr:object_volume_id.object->volumes[grid_row->volume_id];
    wxGetApp().obj_list()->object_config_options_changed(object_volume_id);
    if (object_volume_id.volume)
        dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab(true))->update_model_config();
    else
        dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab())->update_model_config();
    //m_panel->m_plater->update();
}

void ObjectGridTable::update_row_properties()
{
    ObjectGrid* grid_table = m_panel->m_object_grid;
    //col 0 no need to update, always uneditable
    for (int col = 1; col < col_speed_perimeter_reset; col++)
    {
        ObjectGridTable::ObjectGridCol* grid_col = get_grid_col(col);
        //grid_table->SetColSize(col, grid_col->size);

        //row 0 no need to update, always for headers
        for (int row = 1; row < get_row_count(); row++)
        {
            ObjectGridTable::ObjectGridRow* grid_row = get_grid_row(row-1);

            if ((!grid_col->b_icon) && (grid_col->b_for_object)) {
                if (grid_row->row_type == ObjectGridTable::row_volume) {
                    grid_table->SetReadOnly(row, col);
                    //FIXME: recycle the old editor and renders
                    grid_table->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                    grid_table->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
                }
                else {
                    grid_table->SetReadOnly(row, col, false);

                    switch (grid_col->type)
                    {
                        case coString:
                            grid_table->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                            grid_table->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
                            break;
                        case coBool:
                            grid_table->SetCellEditor(row, col, new GridCellSupportEditor());
                            //grid_table->SetCellEditor(row, col, new wxGridCellBoolEditor());
                            //grid_table->SetCellRenderer(row, col, new GridCellSupportRenderer());
                            grid_table->SetCellRenderer(row, col, new wxGridCellBoolRenderer());
                            break;
                        case coInt:
                            grid_table->SetCellEditor(row, col, new wxGridCellNumberEditor());
                            grid_table->SetCellRenderer(row, col, new  wxGridCellNumberRenderer());
                            break;
                        case coEnum:
                            if (col == ObjectGridTable::col_filaments) {
                                GridCellFilamentsEditor *filament_editor = new GridCellFilamentsEditor(grid_col->choice_count, grid_col->choices, false, &m_panel->m_color_bitmaps);
                                grid_table->SetCellEditor(row, col, filament_editor);
                                grid_table->SetCellRenderer(row, col, new GridCellFilamentsRenderer());
                            }
                            else
                                grid_table->SetCellEditor(row, col, new wxGridCellChoiceEditor(grid_col->choice_count, grid_col->choices));
                            break;
                        case coFloat:
                            grid_table->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                            grid_table->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                            break;
                        case coPercent:
                        {
                            /*wxGridCellFloatEditor *float_editor = new wxGridCellFloatEditor(6,2);
                            wxFloatingPointValidator<float> *float_validator = new wxFloatingPointValidator<float>(3, nullptr, wxNUM_VAL_ZERO_AS_BLANK);
                            float_validator->SetRange(0.f, 100.f);
                            float_editor->SetValidator(*float_validator);

                            if (rows < 3)
                                m_object_grid->SetCellEditor(row, col, float_editor);
                            else*/
                                grid_table->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                            grid_table->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
            else if (col == ObjectGridTable::col_name) {
                grid_table->SetCellFont(row, col, Label::Body_13);
                /*if (grid_row->row_type == ObjectGridTable::row_object)
                    grid_table->SetCellFont(row, col, Label::Head_14);
                else
                    grid_table->SetCellFont(row, col, Label::Body_14);*/
            }
        }
    }

    return;
}

void ObjectGridTable::sort_by_default()
{
    compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
        int plate_1, plate_2;
        std::string row1_plate = row1->plate_index.value;
        std::string row2_plate = row2->plate_index.value;
        bool result = false;

        if (row1_plate == ObjectGridTable::plate_outside)
            plate_1 = 100;
        else
            plate_1 = std::atoi(row1_plate.c_str());
        if (row2_plate == ObjectGridTable::plate_outside)
            plate_2 = 100;
        else
            plate_2 = std::atoi(row2_plate.c_str());

        if (plate_1 < plate_2)
            result = true;
        else if (row1_plate == row2_plate) {
            std::string row1_name = row1->name.value;
            std::string row2_name = row2->name.value;
            result = (row1_name < row2_name);
        }
        return result;
    };
    sort_row_data(sort_func);
    m_sort_col = 0;
}

void ObjectGridTable::sort_row_data(compare_row_func sort_func)
{
    int size = m_grid_data.size();
    if (!size)
        return;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" enter, this %1%, row_data size %2%") %this % m_grid_data.size();
    std::list<ObjectGridRow*> new_grid_rows;
    for ( auto it = m_grid_data.begin(); it != m_grid_data.end(); it++ )
    {
        if ((*it)->row_type == row_object)
           new_grid_rows.push_back(*it);
    }
    new_grid_rows.sort(sort_func);
    //std::sort(new_grid_rows.begin(), new_grid_rows.end(), sort_func);
    auto it = new_grid_rows.begin();
    while( it != new_grid_rows.end() )
    {
        if ((*it)->row_type != row_object) {
            ++it;
            continue;
        }
        auto origin_it = find(m_grid_data.begin(), m_grid_data.end(), *it);
        //move it to next for insert
        ++it;
        if (origin_it == m_grid_data.end()) //should not happen, finished
            break;
        ++origin_it;
        while (origin_it != m_grid_data.end() && ((*origin_it)->row_type != row_object))
        {
            new_grid_rows.insert(it, *origin_it);
            ++origin_it;
        }
    }
    m_grid_data.clear();
    m_grid_data.resize(size);
    std::copy(new_grid_rows.begin(), new_grid_rows.end(), m_grid_data.begin());
    new_grid_rows.clear();

    update_row_properties();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" finished, this %1%, row_data size %2%") %this % m_grid_data.size();
}

void ObjectGridTable::sort_by_col(int col)
{
    //handle the sort logic
    if (col == col_name) {
        if (m_sort_col == col) {
            auto sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
                return (row2->name.value.compare(row1->name.value) < 0);
            };
            //sort_by_name();
            sort_row_data(sort_func);
            m_sort_col = -1;
        }
        else {
            auto sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
                return (row1->name.value.compare(row2->name.value) < 0);
            };
            //sort_by_name();
            sort_row_data(sort_func);
            m_sort_col = col;
        }
    }
    //else if (col == col_assemble_name) {
    //    if (m_sort_col == col) {
    //        compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
    //            //wxString string1 = GUI::from_u8(row1->assemble_name.value);
    //            //wxString string2 = GUI::from_u8(row2->assemble_name.value);
    //            //return (string1.compare(string2) <= 0);
    //            return (row2->assemble_name.value.compare(row1->assemble_name.value) < 0);
    //        };
    //        sort_row_data(sort_func);
    //        m_sort_col = -1;
    //    }
    //    else {
    //        compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
    //            //wxString string1 = GUI::from_u8(row1->assemble_name.value);
    //            //wxString string2 = GUI::from_u8(row2->assemble_name.value);
    //            //return (string1.compare(string2) <= 0);
    //            return (row1->assemble_name.value.compare(row2->assemble_name.value) < 0);
    //        };
    //        sort_row_data(sort_func);
    //        m_sort_col = col;
    //    }
    //}
    else if (col == col_plate_index) {
        if (m_sort_col == col) {
            compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
                int plate_1, plate_2;
                std::string row1_plate = row1->plate_index.value;
                std::string row2_plate = row2->plate_index.value;
                bool result = false;

                if (row1_plate == ObjectGridTable::plate_outside)
                    plate_1 = 100;
                else
                    plate_1 = std::atoi(row1_plate.c_str());
                if (row2_plate == ObjectGridTable::plate_outside)
                    plate_2 = 100;
                else
                    plate_2 = std::atoi(row2_plate.c_str());

                if (plate_2 < plate_1)
                    result = true;
                else if (plate_1 == plate_2) {
                    std::string row1_name = row1->name.value;
                    std::string row2_name = row2->name.value;
                    result = (row2_name < row1_name);
                }
                return result;
            };
            sort_row_data(sort_func);
            m_sort_col = -1;
        }
        else {
            compare_row_func sort_func = [](ObjectGridRow* row1, ObjectGridRow* row2) {
                int plate_1, plate_2;
                std::string row1_plate = row1->plate_index.value;
                std::string row2_plate = row2->plate_index.value;
                bool result = false;

                if (row1_plate == ObjectGridTable::plate_outside)
                    plate_1 = 100;
                else
                    plate_1 = std::atoi(row1_plate.c_str());
                if (row2_plate == ObjectGridTable::plate_outside)
                    plate_2 = 100;
                else
                    plate_2 = std::atoi(row2_plate.c_str());

                if (plate_1 < plate_2)
                    result = true;
                else if (plate_1 == plate_2) {
                    std::string row1_name = row1->name.value;
                    std::string row2_name = row2->name.value;
                    result = (row1_name < row2_name);
                }
                return result;
            };
            sort_row_data(sort_func);
            m_sort_col = col;
        }
    }

    return;
}


bool ObjectGridTable::OnCellLeftClick(int row, int col, ConfigOptionType &type)
{
    bool consumed = false;

    if (row == 0) {
        sort_by_col(col);
    } else if (col >= col_name) {
        ObjectGridRow *grid_row   = m_grid_data[row - 1];
        ObjectGridCol* grid_col = m_col_data[col];
        ObjectGridCol* grid_col_2 = m_col_data[col - 1];

        if (grid_col->b_icon) {
            ConfigOption& orig_option = (*grid_row)[(GridColType)col];
            ConfigOption& cur_option = (*grid_row)[(GridColType)(col-1)];
            //reset the value to original one
            if (cur_option != orig_option) {
                cur_option.set(&orig_option);
                if (grid_col_2->b_from_config) {
                    if (col == col_filaments_reset) {
                        update_filament_to_config(grid_row->config, grid_col_2->key, cur_option, orig_option, (grid_row->row_type == row_object));
                        update_volume_values_from_object(row, col-1);
                    }
                    else
                        update_value_to_config(grid_row->config, grid_col_2->key, cur_option, orig_option);
                    ObjectVolumeID object_volume_id;
                    object_volume_id.object = m_panel->m_model->objects[grid_row->object_id];
                    object_volume_id.volume = (grid_row->row_type == row_object) ? nullptr : object_volume_id.object->volumes[grid_row->volume_id];
                    if (col == col_filaments_reset)
                        wxGetApp().obj_list()->update_filament_values_for_items(m_panel->m_filaments_count);
                    else
                        wxGetApp().obj_list()->object_config_options_changed(object_volume_id);

                    //update the right side setting list
                    bool is_object = (grid_row->row_type == row_object);
                    ModelObject* object = m_panel->m_model->objects[grid_row->object_id];
                    m_panel->m_side_window->Freeze();
                    m_panel->m_object_settings->ValueChanged(row, is_object, object, grid_row->config, grid_col_2->category, grid_col_2->key);
                    m_panel->m_side_window->Thaw();
                    //m_panel->m_plater->update();
                }
                else {
                    update_value_to_object(m_panel->m_model, grid_row, col - 1);
                }
                m_panel->m_object_grid->ForceRefresh();
            }
            consumed = true;
        }
        else
            type = grid_col->type;
    }
    return consumed;
}

void ObjectGridTable::OnSelectCell(int row, int col)
{
    m_selected_cells.clear();
    m_panel->m_side_window->Freeze();
    if (row == 0) {
        m_panel->m_object_settings->UpdateAndShow(row, false, false, false, nullptr, nullptr, std::string());
    }
    else {
        ObjectGridRow* grid_row = m_grid_data[row - 1];
        ObjectGridCol* grid_col = m_col_data[col];
        bool is_object = (grid_row->row_type == row_object);
        ModelObject* object = m_panel->m_model->objects[grid_row->object_id];

        //m_panel->m_object_settings->get_og()->set_name(GUI::from_u8(grid_row->name.value));
        //m_panel->m_page_text->SetLabel(GUI::from_u8(grid_row->name.value));
        m_panel->m_object_settings->UpdateAndShow(row, true, is_object, false, object, grid_row->config, grid_col->category);

        std::vector<ObjectVolumeID> object_volume_ids;
        ObjectVolumeID object_volume_id;
        object_volume_id.object = object;
        object_volume_id.volume = (grid_row->row_type == row_object)?nullptr:object->volumes[grid_row->volume_id];
        object_volume_ids.push_back(object_volume_id);
        //disable multiple selection notify, only notify one selected item to object list
        /*wxGridCellCoordsArray cell_array = m_panel->m_object_grid->GetSelectedCells();
        int count = cell_array.size();
        if (count == 0) {
            ObjectVolumeID object_volume_id;
            object_volume_id.object = object;
            object_volume_id.volume = (grid_row->row_type == row_object)?nullptr:object->volumes[grid_row->volume_id];
            object_volume_ids.push_back(object_volume_id);

            wxGridCellCoords cell_coord(row, col);
            m_selected_cells.push_back(cell_coord);
        }
        else {
            while (count > 0) {
                int cel_row = cell_array[count-1].GetRow();
                int cel_col = cell_array[count-1].GetCol();
                ObjectGridRow* grid_data = m_grid_data[cel_row - 1];
                object = m_panel->m_model->objects[grid_data->object_id];
                ObjectVolumeID object_volume_id;
                object_volume_id.object = object;
                object_volume_id.volume = (grid_data->row_type == row_object)?nullptr:object->volumes[grid_data->volume_id];
                object_volume_ids.push_back(object_volume_id);
                count --;

                wxGridCellCoords cell_coord(cel_row, cel_col);
                m_selected_cells.push_back(cell_coord);
            }
        }*/

        wxGetApp().obj_list()->select_items(object_volume_ids);
    }
    m_panel->m_side_window->Layout();
    //m_panel->m_side_window->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_ALWAYS);
    m_panel->m_side_window->Thaw();
    m_panel->Layout();
    m_current_row = row;
    m_current_col = col;
}

void ObjectGridTable::OnRangeSelected(int row, int col, int row_count, int col_count)
{
}

void ObjectGridTable::OnCellValueChanged(int row, int col)
{
    if (row == 0) {
        //skip it
    }
    else if (col <= (int)col_filaments_reset) {
        //skip it
    }
    else {
        ObjectGridRow* grid_row = m_grid_data[row - 1];
        ObjectGridCol* grid_col = m_col_data[col];
        bool is_object = (grid_row->row_type == row_object);
        ModelObject* object = m_panel->m_model->objects[grid_row->object_id];

        m_panel->m_side_window->Freeze();

        m_panel->m_object_settings->ValueChanged(row, is_object, object, grid_row->config, grid_col->category, grid_col->key);

        m_panel->m_side_window->Thaw();
        //update volume cell
        /*if (is_object) {
            int next_row = row + 1;
            DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            while ((next_row - 1) < m_grid_data.size())
            {
                ObjectGridRow* part_row = m_grid_data[next_row - 1];
                if (part_row->row_type == row_volume) {
                    reload_part_data(part_row, grid_row, grid_col->category, global_config);
                    next_row++;
                }
                else
                    break;
            }
        }*/
        //obj_list->update_extruder_values_for_items(m_panel->m_filaments_count);
        if (grid_col->b_from_config) {
            if (col != col_filaments) {//for col_filaments, no cellvaluechanged triggered
                ObjectVolumeID object_volume_id;
                object_volume_id.object = object;
                object_volume_id.volume = is_object ? nullptr : object->volumes[grid_row->volume_id];
                wxGetApp().obj_list()->object_config_options_changed(object_volume_id);
                //m_panel->m_plater->update();
            }
        }
    }
}

wxBitmap& ObjectGridTable::get_undo_bitmap(bool selected)
{
    return m_panel->m_undo_bitmap;
}

wxBitmap* ObjectGridTable::get_color_bitmap(int color_index)
{
    if (color_index < m_panel->m_color_bitmaps.size())
        return m_panel->m_color_bitmaps[color_index];
    else
        return m_panel->m_color_bitmaps[0];
}

void ObjectGridTable::resetValuesInCurrentCell(wxEvent& WXUNUSED(event))
{
    //return for the first row
    if (m_current_row <= 0)
        return;

    ObjectGridRow* grid_row = m_grid_data[m_current_row - 1];
    ObjectGridCol* grid_col = m_col_data[m_current_col];
    bool is_object = (grid_row->row_type == row_object);
    ModelObject* object = m_panel->m_model->objects[grid_row->object_id];

    m_panel->resetAllValuesInSideWindow(m_current_row, is_object, object, grid_row->config, grid_col->category);
}

void ObjectGridTable::enable_reset_all_button(bool enable)
{
    //m_panel->m_global_reset->Enable(enable);
}

wxIMPLEMENT_CLASS(ObjectTablePanel, wxPanel);
/* ObjectTabelPanel related class */
wxBEGIN_EVENT_TABLE( ObjectTablePanel, wxPanel )
    //EVT_GRID_LABEL_LEFT_CLICK( ObjectTablePanel::OnLabelLeftClick )
    EVT_GRID_CELL_LEFT_CLICK( ObjectTablePanel::OnCellLeftClick )
    EVT_GRID_ROW_SIZE( ObjectTablePanel::OnRowSize )
    EVT_GRID_COL_SIZE( ObjectTablePanel::OnColSize )
    //EVT_GRID_COL_AUTO_SIZE( ObjectTablePanel::OnColAutoSize )
    EVT_GRID_SELECT_CELL( ObjectTablePanel::OnSelectCell )
    //EVT_GRID_RANGE_SELECTING( ObjectTablePanel::OnRangeSelecting )
    EVT_GRID_RANGE_SELECTED( ObjectTablePanel::OnRangeSelected )
    //EVT_GRID_CELL_CHANGING( GridFrame::OnCellValueChanging )
    EVT_GRID_CELL_CHANGED( ObjectTablePanel::OnCellValueChanged )
    //EVT_GRID_CELL_BEGIN_DRAG( GridFrame::OnCellBeginDrag )
wxEND_EVENT_TABLE()

ObjectTablePanel::ObjectTablePanel( wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name, Plater* platerObj, Model *modelObj )
                    : wxPanel( parent, id, pos, size, style, name ), m_model(modelObj), m_plater(platerObj), m_float_validator(2, nullptr, wxNUM_VAL_ZERO_AS_BLANK)
{
    //m_bg_colour = wxColour(0xfa, 0xfa, 0xfa);
    m_float_validator.SetRange(0, 100);
    m_bg_colour = wxColour(0xff, 0xff, 0xff);
    //m_hover_colour = wxColour(61, 70, 72);
    SetBackgroundColour(m_bg_colour);

    SetSize(wxSize(-1, FromDIP(450)));
    SetMinSize(wxSize(-1, FromDIP(450)));
    SetMaxSize(wxSize(-1, FromDIP(450)));

    //m_search_line = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

    init_bitmap();

    init_filaments_and_colors();

	m_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    //m_top_sizer->Add(m_search_line, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);


    m_object_grid = new ObjectGrid(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
    m_object_grid_table = new ObjectGridTable(this);
    this->load_data();
    //m_object_grid_table->SetAttrProvider(new MyGridCellAttrProvider);
    //m_object_grid->AssignTable(m_object_grid_table);


    m_side_window = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(460),FromDIP(480)), wxVSCROLL);
    m_side_window->SetScrollRate( 0, 5 );
    m_page_sizer = new wxBoxSizer(wxVERTICAL);
    m_page_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_side_window->SetBackgroundColour(wxColour(0xff, 0xff, 0xff));
    m_side_window->SetSizer(m_page_sizer);
    m_side_window->SetScrollbars(1, 20, 1, 2);
    //m_side_window->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);

    //m_side_window->EnableScrolling(false, true);
    //m_side_window->SetSize(wxSize(128, 512));

    /* m_page_text = new wxStaticText(m_side_window, wxID_ANY, wxString(L("Per Object Setting")), wxDefaultPosition, wxSize(-1, 32), wxALIGN_CENTRE_HORIZONTAL|wxST_ELLIPSIZE_END);
     m_page_text->SetMaxSize(wxSize(256, 32));
     m_page_text->SetFont(Label::Head_18);
     m_page_top_sizer->Add(m_page_text, 0, wxEXPAND, 5);*/




    //TODO, adjust later
    /*
     _bmp_reset = ScalableBitmap(m_side_window, "undo");
    m_bmp_reset_disable = ScalableBitmap(m_side_window, "dot");
    m_global_reset = new ScalableButton(m_side_window, wxID_ANY, m_bmp_reset);
    m_global_reset->SetToolTip(_(L("Reset All")));

    m_global_reset->SetBitmapDisabled(m_bmp_reset_disable.bmp());

    m_global_reset->Bind(wxEVT_BUTTON, [this](wxEvent &event) {
        m_object_grid_table->resetValuesInCurrentCell(event);
    });
    m_global_reset->Enable(false);
    m_page_top_sizer->AddSpacer(16);
    m_page_top_sizer->Add(m_global_reset, 0, wxEXPAND, 5);*/



    //create object settings
    m_side_window->SetFont(::Label::Body_12);
    m_object_settings = new ObjectTableSettings(m_side_window, m_object_grid_table);
    m_object_settings->Hide();
    m_page_sizer->Add(m_page_top_sizer, 0, wxEXPAND | wxALIGN_CENTER_HORIZONTAL, 0);
    m_page_sizer->Add(m_object_settings->get_sizer(), 1, wxEXPAND | wxALL, 2 );

    auto m_line_left = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1,-1), wxTAB_TRAVERSAL);
    m_line_left->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));


    m_top_sizer->Add(m_object_grid, 1, wxEXPAND,0);
    m_top_sizer->Add(m_line_left, 0, wxEXPAND, 0);
    m_top_sizer->Add(m_side_window,0,0,0);

    //wxBoxSizer * page_sizer = new wxBoxSizer(wxHORIZONTAL);

    SetFont(::Label::Body_13);
    SetSizer(m_top_sizer);
    Layout();
}

int ObjectTablePanel::init_bitmap()
{
    m_undo_bitmap = create_scaled_bitmap("lock_normal", nullptr, 24);
    m_color_bitmaps = get_extruder_color_icons();

    return 0;
}

int ObjectTablePanel::init_filaments_and_colors()
{
    //DynamicPrintConfig&  global_config   = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    const DynamicPrintConfig* global_config = m_plater->config();
    const std::vector<std::string> filament_presets = wxGetApp().preset_bundle->filament_presets;
    m_filaments_count = filament_presets.size();
    if (m_filaments_count <= 0) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", can not get filaments, count: %1%, set to default") %m_filaments_count;
        set_default_filaments_and_colors();
        return -1;
    }

    const ConfigOptionStrings* filament_opt = dynamic_cast<const ConfigOptionStrings*>(global_config->option("filament_colour"));
    if (filament_opt == nullptr) {
        set_default_filaments_and_colors();
        return -1;
    }
    m_filaments_colors.resize(m_filaments_count);
    m_filaments_name.resize(m_filaments_count);
    unsigned int color_count = filament_opt->values.size();
    if (color_count != m_filaments_count) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", invalid color count:%1%, extruder count: %2%") %color_count %m_filaments_count;
    }

    unsigned int i = 0;
    unsigned char rgb[3];
    while (i < m_filaments_count) {
        const std::string& txt_color = global_config->opt_string("filament_colour", i);
        if (i < color_count) {
            if (Slic3r::GUI::BitmapCache::parse_color(txt_color, rgb))
            {
                m_filaments_colors[i] = wxColour(rgb[0], rgb[1], rgb[2]);
            }
            else
            {
                m_filaments_colors[i] = *wxGREEN;
            }
        }
        else {
            m_filaments_colors[i] = *wxGREEN;
        }

        //parse the filaments
        m_filaments_name[i] = wxString(std::to_string(i+1) + ": " + filament_presets[i]);

        i++;
    }

    return 0;
}

void ObjectTablePanel::load_data()
{
    int rows, cols;

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", enter");
    m_object_grid_table->construct_object_configs(m_object_grid);
    m_object_grid->AssignTable(m_object_grid_table);

    rows = m_object_grid_table->get_row_count();
    cols = m_object_grid_table->get_col_count();

    //construct tables
    //m_object_grid->CreateGrid(rows, cols, wxGridSelectCells);
#if HAS_COL_HEADER
    m_object_grid->SetColLabelValue(ObjectGridTable::col_printable, L("Printable"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_printable_reset, "");
    m_object_grid->SetColLabelValue(ObjectGridTable::col_plate_index, L("Plate"));
    /*m_object_grid->SetColLabelValue(ObjectGridTable::col_assemble_name, L("Module"));*/
    m_object_grid->SetColLabelValue(ObjectGridTable::col_name, L("Name"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_filaments, L("Filament"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_filaments_reset, "");
    m_object_grid->SetColLabelValue(ObjectGridTable::col_layer_height, L("Layer height"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_layer_height_reset, "");
    m_object_grid->SetColLabelValue(ObjectGridTable::col_wall_loops, L("Wall loops"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_wall_loops_reset, "");
    m_object_grid->SetColLabelValue(ObjectGridTable::col_fill_density, L("Infill density(%)"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_fill_density_reset, "");
    m_object_grid->SetColLabelValue(ObjectGridTable::col_enable_support, L("Support"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_enable_support_reset, "");
    m_object_grid->SetColLabelValue(ObjectGridTable::col_brim_type, L("Brim"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_brim_type_reset, "");
    m_object_grid->SetColLabelValue(ObjectGridTable::col_speed_perimeter, L("Inner wall speed"));
    m_object_grid->SetColLabelValue(ObjectGridTable::col_speed_perimeter_reset, "");
    m_object_grid->SetLabelFont(Label::Head_13);
    m_object_grid->SetLabelTextColour(wxColour(0x30,0x3a,0x3c));
    m_object_grid->SetLabelBackgroundColour(wxColour(0xff,0xff,0xff));
#else
    m_object_grid->HideColLabels();
#endif
    m_object_grid->HideRowLabels();
    m_object_grid->EnableGridLines (true);

    /*set the first row as label*/
    //format
    wxGridCellAttr *attr;
    attr = new wxGridCellAttr;
    //attr->SetBackgroundColour(wxColour(191, 191, 255));
    attr->SetBackgroundColour(*wxWHITE);
    attr->SetTextColour(*wxBLACK);
    attr->SetAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
    attr->SetReadOnly(true);
    m_object_grid->SetRowAttr (0, attr);
#if HAS_COL_HEADER
    m_object_grid->SetRowSize(0, 0);
#endif
    //merges
    m_object_grid->SetCellSize(0, ObjectGridTable::col_printable, 1, 2);
    //m_object_grid->SetCellSize(0, ObjectGridTable::col_assemble_name, 1, 1);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_name, 1, 1);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_filaments, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_layer_height, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_wall_loops, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_fill_density, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_enable_support, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_brim_type, 1, 2);
    m_object_grid->SetCellSize(0, ObjectGridTable::col_speed_perimeter, 1, 2);

    //m_object_grid->SetSelectionForeground(wxColour(255, 0, 0));
    //m_object_grid->SetSelectionBackground(wxColour(0, 255, 0));
    //wxGridCellAutoWrapStringEditor* string_editor = new wxGridCellAutoWrapStringEditor();
    //wxGridCellBoolEditor* bool_editor = new wxGridCellBoolEditor();
    //wxGridCellFloatEditor* float_editor = new wxGridCellFloatEditor();
    //wxGridCellNumberEditor* number_editor = new wxGridCellNumberEditor();
    //wxGridCellChoiceEditor* choice_editor = new wxGridCellChoiceEditor();
    //wxGridCellEnumEditor* enum_editor = new wxGridCellEnumEditor();

    for (int col = 0; col < cols; col++)
    {
        ObjectGridTable::ObjectGridCol* grid_col = m_object_grid_table->get_grid_col(col);
        m_object_grid->SetColSize(col, grid_col->size);

        for (int row = 1; row < rows; row++)
        {
            ObjectGridTable::ObjectGridRow* grid_row = m_object_grid_table->get_grid_row(row-1);

            m_object_grid->SetCellAlignment(row, col, grid_col->horizontal_align, wxALIGN_CENTRE );
            m_object_grid->SetCellOverflow(row, col, false);
            //m_object_grid->SetCellBackgroundColour (row, col, *wxLIGHT_GREY);
            m_object_grid->SetCellBackgroundColour (row, col, *wxWHITE);
            //set the render and editor
            if (grid_col->b_icon) {
                m_object_grid->SetCellRenderer(row, col, new GridCellIconRenderer());
                m_object_grid->SetReadOnly(row, col);
            }
            else if (grid_col->b_for_object && (grid_row->row_type == ObjectGridTable::row_volume)) {
                m_object_grid->SetReadOnly(row, col);
                m_object_grid->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                m_object_grid->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
            }
            else {
                if (!grid_col->b_editable)
                    m_object_grid->SetReadOnly(row, col);
                //set editor
                switch (grid_col->type)
                {
                    case coString:
                        m_object_grid->SetCellEditor(row, col, new wxGridCellAutoWrapStringEditor());
                        m_object_grid->SetCellRenderer(row, col, new wxGridCellAutoWrapStringRenderer());
                        break;
                    case coBool:
                        m_object_grid->SetCellEditor(row, col, new GridCellSupportEditor());
                        //m_object_grid->SetCellEditor(row, col, new wxGridCellBoolEditor());
                        //m_object_grid->SetCellRenderer(row, col, new GridCellSupportRenderer());
                        m_object_grid->SetCellRenderer(row, col, new wxGridCellBoolRenderer());
                        break;
                    case coInt:
                        m_object_grid->SetCellEditor(row, col, new wxGridCellNumberEditor());
                        m_object_grid->SetCellRenderer(row, col, new  wxGridCellNumberRenderer());
                        break;
                    case coEnum:
                        if (col == ObjectGridTable::col_filaments) {
                            GridCellFilamentsEditor *filament_editor = new GridCellFilamentsEditor(grid_col->choice_count, grid_col->choices, false, &m_color_bitmaps);
                            m_object_grid->SetCellEditor(row, col, filament_editor);
                            //m_object_grid->SetCellEditor(row, col, new wxGridCellChoiceEditor(grid_col->choice_count, grid_col->choices));
                            m_object_grid->SetCellRenderer(row, col, new GridCellFilamentsRenderer());
                        }
                        else
                            m_object_grid->SetCellEditor(row, col, new wxGridCellChoiceEditor(grid_col->choice_count, grid_col->choices));
                        break;
                    case coFloat:
                        m_object_grid->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                        m_object_grid->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                        break;
                    case coPercent:
                    {
                        /*wxGridCellFloatEditor *float_editor = new wxGridCellFloatEditor(6,2);
                        wxFloatingPointValidator<float> *float_validator = new wxFloatingPointValidator<float>(3, nullptr, wxNUM_VAL_ZERO_AS_BLANK);
                        float_validator->SetRange(0.f, 100.f);
                        float_editor->SetValidator(*float_validator);

                        if (rows < 3)
                            m_object_grid->SetCellEditor(row, col, float_editor);
                        else*/
                            m_object_grid->SetCellEditor(row, col, new wxGridCellFloatEditor(6,2));
                        m_object_grid->SetCellRenderer(row, col, new wxGridCellFloatRenderer(6,2));
                        break;
                    }
                    default:
                        break;
                }
                if (col == ObjectGridTable::col_name) {
                    m_object_grid->SetCellFont(row, col, Label::Body_13);
                    /* if (grid_row->row_type == ObjectGridTable::row_object)
                         m_object_grid->SetCellFont(row, col, Label::Head_14);
                     else
                         m_object_grid->SetCellFont(row, col, Label::Body_14);*/
                }
            }
        }
    }
    m_object_grid->Fit();
    for (int i = 0; i < ObjectGridTable::col_max; i++)
    {
        ObjectGridTable::ObjectGridCol *grid_col = m_object_grid_table->get_grid_col(i);
        if (grid_col->size > 0) {
            int fit_size1 = m_object_grid->GetColSize(i);
            m_object_grid->SetColSize(i, grid_col->size);
            //if (grid_col->size < fit_size1) m_object_grid->SetColSize(i, grid_col->size);
        }

        //else {
            //adjust the left col
            //int delta = grid_col->size - fit_size1;
            //grid_col = m_object_grid_table->get_grid_col(i - 1);
            //int fit_size2 = m_object_grid->GetColSize(i-1);
            //grid_col->size = fit_size2 - delta;
            //m_object_grid->SetColSize(i, grid_col->size);
        //}
    }

    //set col size after fit
   /* ObjectGridTable::ObjectGridCol* grid_col = m_object_grid_table->get_grid_col(ObjectGridTable::col_brim_type);
    grid_col->size = m_object_grid->GetTextExtent(grid_col->choices[0]).x + 30;
    m_object_grid->SetColSize(ObjectGridTable::col_brim_type, grid_col->size);

    grid_col = m_object_grid_table->get_grid_col(ObjectGridTable::col_filaments);
    grid_col->size = 128;
    m_object_grid->SetColSize(ObjectGridTable::col_filaments, grid_col->size);*/
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", finished, got %1% rows, %2% cols") %m_object_grid_table->GetNumberRows() %m_object_grid_table->GetNumberCols() ;
}

void ObjectTablePanel::SetSelection(int object_id, int volume_id)
{
    m_object_grid_table->SetSelection(object_id, volume_id);
}

ObjectTablePanel::~ObjectTablePanel()
{
    //do it in the grid
    /*if (m_object_grid_table) {
        m_object_grid_table->release_object_configs();
        delete m_object_grid_table;
        m_object_grid_table = nullptr;
    }*/
    if (m_top_sizer)
        m_top_sizer->Clear(true);

    m_filaments_name.clear();
    m_filaments_colors.clear();
}

void ObjectTablePanel::OnCellLeftClick( wxGridEvent& ev )
{
    int row = ev.GetRow();
    int col = ev.GetCol();

    ConfigOptionType type = coNone;
    bool consumed = m_object_grid_table->OnCellLeftClick(row, col, type);
    if (consumed) {
        //m_object_grid->ClearSelection();
        //m_object_grid->SelectBlock(row, col-1, row, col-1, true);
        return;
    }

    consumed = m_object_grid->OnCellLeftClick(ev, row, col, type);
    if (!consumed)
        ev.Skip();
}

void ObjectTablePanel::OnRowSize( wxGridSizeEvent& ev)
{
    wxSize panel_size = get_init_size();

    g_dialog_max_width = (panel_size.GetWidth() > g_max_size_from_parent.GetWidth())?g_max_size_from_parent.GetWidth():panel_size.GetWidth();
    g_dialog_max_height =(panel_size.GetHeight() > g_max_size_from_parent.GetHeight())?g_max_size_from_parent.GetHeight():panel_size.GetHeight();
    this->SetMaxSize(wxSize(g_dialog_max_width, g_dialog_max_height));

    wxSize current_size = GetParent()->GetSize();
    //if (current_size.GetHeight() < g_dialog_max_height)
        GetParent()->SetMaxSize(wxSize(g_dialog_max_width, g_dialog_max_height));
    GetParent()->SetSize(wxSize(g_dialog_max_width, g_dialog_max_height));
}

void ObjectTablePanel::OnColSize( wxGridSizeEvent& ev)
{
    wxSize panel_size = get_init_size();

    g_dialog_max_width = (panel_size.GetWidth() > g_max_size_from_parent.GetWidth())?g_max_size_from_parent.GetWidth():panel_size.GetWidth();
    g_dialog_max_height =(panel_size.GetHeight() > g_max_size_from_parent.GetHeight())?g_max_size_from_parent.GetHeight():panel_size.GetHeight();
    this->SetMaxSize(wxSize(g_dialog_max_width, g_dialog_max_height));

    wxSize current_size = GetParent()->GetSize();
    //if (current_size.GetWidth() < g_dialog_max_width)
        GetParent()->SetMaxSize(wxSize(g_dialog_max_width, g_dialog_max_height));
    GetParent()->SetSize(wxSize(g_dialog_max_width, g_dialog_max_height));
}

void ObjectTablePanel::OnSelectCell( wxGridEvent& ev )
{
    int row = ev.GetRow();
    int col = ev.GetCol();

    if ((row == m_cur_row) && (col == m_cur_col))
    {
        //the same cel selected
        return;
    }

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("cell (%1%, %2%) selected") %row %col;
    m_object_grid_table->OnSelectCell(row, col);

    m_cur_row = row;
    m_cur_col = col;

    ev.Skip();
}

void ObjectTablePanel::OnCellValueChanged( wxGridEvent& ev )
{
    int row = ev.GetRow();
    int col = ev.GetCol();

    m_object_grid_table->OnCellValueChanged(row, col);

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("cell (%1%, %2%) changed from %3% to %4%") %row %col %ev.GetString() % m_object_grid->GetCellValue(row, col);

    ev.Skip();
}


void ObjectTablePanel::OnRangeSelected( wxGridRangeSelectEvent& ev )
{
    int left_col = ev.GetLeftCol();
    int right_col = ev.GetRightCol();
    int top_row = ev.GetTopRow();
    int bottom_row = ev.GetBottomRow();

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("cell from (%1%, %2%) to (%3%, %4%) selected") %top_row %left_col %bottom_row %right_col;

    ev.Skip();
}

wxSize ObjectTablePanel::get_init_size()
{
    wxSize size;
    int width = 0, height = 0;

    //compute height
    int row_count = m_object_grid_table->GetNumberRows();
    if (row_count >= g_overflow_row_count) {
        height = row_count * g_min_row_size;
    }
    else {
        for (int index = 0; index < m_object_grid_table->GetNumberRows(); index++)
        {
            height += m_object_grid->GetRowSize(index);
        }
    }
    height += g_extra_height;

    if (height < (g_min_row_count * g_min_row_size))
        height = g_min_row_count * g_min_row_size;

    //compute width
    for (int index = 0; index < m_object_grid_table->GetNumberCols(); index++)
    {
        width += m_object_grid->GetColSize(index);
    }
    width += g_vscroll_width + g_min_setting_width;

    size.Set(width, height);

    return size;
}

void ObjectTablePanel::resetAllValuesInSideWindow(int row, bool is_object, ModelObject* object, ModelConfig* config, const std::string& category)
{
    //
    m_object_settings->resetAllValues(row, is_object, object, config, category);
}

// ----------------------------------------------------------------------------
// ObjectTableDialog
// ----------------------------------------------------------------------------
ObjectTableDialog::ObjectTableDialog(wxWindow* parent, Plater* platerObj, Model *modelObj, wxSize maxSize)
    : GUI::DPIDialog(parent, wxID_ANY, _L("Object/Part Setting"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
    ,
    m_model(modelObj), m_plater(platerObj)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    //this->SetBackgroundColour(m_bg_colour);

    //m_top_sizer = new wxBoxSizer( wxVERTICAL );

    //m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    SetBackgroundColour(*wxWHITE);
    SetMinSize(wxSize(-1, FromDIP(480)));

    //m_static_title = new wxStaticText( m_panel, wxID_ANY, wxT("Totally Objects, Parts"), wxDefaultPosition, wxDefaultSize, 0 );
    //m_static_title->SetFont(Label::Head_12);
    //m_static_title->Wrap( -1 );
    //m_top_sizer->Add(m_static_title, 0, wxALL, 5);

    //wxBoxSizer* bSizer1;
	//bSizer1 = new wxBoxSizer( wxVERTICAL );

	//this->SetSizer( bSizer1 );
	//this->Layout();

    // And also actually enable them.
    //m_panel->SetScrollRate(10, 10);
    auto m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // icon
    std::string icon_path = (boost::format("%1%/images/BambuStudio.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    //top line
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(0xA6, 0xa9, 0xAA));
    m_main_sizer->Add(m_line_top, 0, wxEXPAND, 0);

    m_obj_panel = new ObjectTablePanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE, wxEmptyString, m_plater, m_model);
    //m_top_sizer->Add(m_obj_panel, 1, wxALL | wxEXPAND, 5);

    wxSize panel_size = m_obj_panel->get_init_size();
    g_max_size_from_parent = maxSize;
    if ((maxSize.GetWidth() == -1) || (maxSize.GetHeight() == -1)) {
        wxDisplay display(wxDisplay::GetFromWindow(this));
        //auto drect = display.GetGeometry();
        wxRect 	client_area = display.GetClientArea ();
        g_max_size_from_parent.SetWidth(client_area.GetWidth());
        g_max_size_from_parent.SetHeight(client_area.GetHeight());
    }

    if ((g_dialog_width == 0) || (g_dialog_height == 0))
    {
        g_dialog_height = (panel_size.GetHeight() > g_max_size_from_parent.GetHeight())?g_max_size_from_parent.GetHeight():panel_size.GetHeight();
        g_dialog_width = (panel_size.GetWidth() > g_max_size_from_parent.GetWidth())?g_max_size_from_parent.GetWidth():panel_size.GetWidth();
    }
    this->SetSize(wxSize(g_dialog_width, g_dialog_height));
    g_dialog_max_width = (panel_size.GetWidth() > g_max_size_from_parent.GetWidth())?g_max_size_from_parent.GetWidth():panel_size.GetWidth();
    g_dialog_max_height = g_max_size_from_parent.GetHeight();
    //g_dialog_max_height = (panel_size.GetHeight() > g_max_size_from_parent.GetHeight()) ? g_max_size_from_parent.GetHeight() : panel_size.GetHeight();
    this->SetMaxSize(wxSize(g_dialog_max_width, g_dialog_max_height));
    //m_top_sizer->SetSizeHints(this);
    //this->SetSizer(m_top_sizer);
    //SetClientSize(m_panel->GetSize());
    Bind(wxEVT_CLOSE_WINDOW, &ObjectTableDialog::OnClose, this);
    Bind(wxEVT_SIZE, &ObjectTableDialog::OnSize, this);

    //this->Layout();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", created, this %1%, m_obj_panel %2%") %this % m_obj_panel;

    m_main_sizer->Add(m_obj_panel, 1, wxEXPAND|wxTOP,2);
    SetSizer(m_main_sizer);
    Layout();
    Fit();
}

ObjectTableDialog::~ObjectTableDialog()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", this %1%, m_obj_panel %2%") %this % m_obj_panel;
    if (m_obj_panel) {
        delete m_obj_panel;
        m_obj_panel = nullptr;
    }
}

void ObjectTableDialog::Popup(int obj_idx, int vol_idx, wxPoint position /*= wxDefaultPosition*/)
{
    m_obj_panel->sort_by_default();
    m_obj_panel->SetSelection(obj_idx, vol_idx);

    this->SetPosition(position);
    this->ShowModal();
}

void ObjectTableDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    const wxSize& size = wxSize(40 * em, 30 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void ObjectTableDialog::on_sys_color_changed()
{

    Refresh();
}

void ObjectTableDialog::OnClose(wxCloseEvent &evt)
{
    this->GetSize(&g_dialog_width, &g_dialog_height);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", g_dialog_width %1%, g_dialog_height %2%") %g_dialog_width % g_dialog_height;
    evt.Skip();
}

void ObjectTableDialog::OnSize(wxSizeEvent& event)
{
    SetSize(wxSize(-1, FromDIP(480)));
    SetMinSize(wxSize(-1, FromDIP(480)));
    SetMaxSize(wxSize(-1, FromDIP(480)));
    return;
    wxSize new_size = event.GetSize();
    if ((new_size.GetWidth() > g_dialog_max_width) || (new_size.GetHeight() > g_dialog_max_height)) {
        int width = (new_size.GetWidth() > g_dialog_max_width)?new_size.GetWidth():g_dialog_max_width;
        int height = (new_size.GetHeight() > g_dialog_max_height)?new_size.GetHeight():g_dialog_max_height;
        this->SetMaxSize(wxSize(width, height));
    }
    event.Skip();
}

} // namespace GUI
} // namespace Slic3r
