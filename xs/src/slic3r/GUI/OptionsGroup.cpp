#include "OptionsGroup.hpp"
#include "OptionsGroup/Field.hpp"
#include "Config.hpp"

// Translate the ifdef 
#ifdef __WXOSX__
    #define wxOSX true
#else
    #define wxOSX false
#endif

#define BORDER(a, b) ((wxOSX ? a : b))

namespace Slic3r { namespace GUI {


void OptionsGroup::BUILD() {
    if (staticbox) {
        wxStaticBox* box = new wxStaticBox(_parent, -1, title);
        _sizer = new wxStaticBoxSizer(box, wxVERTICAL);
    } else {
        _sizer = new wxBoxSizer(wxVERTICAL);
    }
    size_t num_columns = 1;
    if (label_width != 0) ++num_columns;
    if (extra_column != 0) ++num_columns;

    _grid_sizer = new wxFlexGridSizer(0, num_columns, 0, 0);
    _grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    _grid_sizer->AddGrowableCol(label_width > 0);
    _sizer->Add(_grid_sizer, 0, wxEXPAND | wxALL, BORDER(0,5));
}

void OptionsGroup::append_line(const Line& line) {
    if (line.has_sizer() || (line.has_widget() && line.full_width)) {
        wxASSERT(line.sizer() != nullptr);
        _sizer->Add( (line.has_sizer() ? line.sizer() : line.widget().sizer()), 0, wxEXPAND | wxALL, BORDER(0, 15));
        return;
    }
    wxSizer* grid_sizer = _grid_sizer;
    // If we have an extra column, build it.
    // If there's a label, build it.
    if (label_width != 0) {
        wxStaticText* label = new wxStaticText(_parent, -1, (line.label) + ":", wxDefaultPosition);
        label->Wrap(label_width);
        if (wxIsEmpty(line.tooltip())) { label->SetToolTip(line.tooltip()); }
        grid_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
    }
    // If we have a widget, add it to the sizer
    if (line.has_widget()) {
        grid_sizer->Add(line.widget().sizer(), 0, wxEXPAND | wxALL, BORDER(0,15));
        return;
    }
    // If we have a single option with no sidetext just add it directly to the grid sizer
    if (line.options().size() == 1) {
        const ConfigOptionDef& opt = line.options()[0];
        if (line.extra_widgets().size() && !wxIsEmpty(opt.sidetext) && line.extra_widgets().size() == 0) {
            Field* field = _build_field(opt);
            if (field != nullptr) {
                if (field->has_sizer()) {
                    grid_sizer->Add(field->sizer(), 0, (opt.full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
                } else if (field->has_window()) {
                    grid_sizer->Add(field->window(), 0, (opt.full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
                }
            }
        }
    }
    // Otherwise, there's more than one option or a single option with sidetext -- make
    // a horizontal sizer to arrange things.
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    grid_sizer->Add(sizer, 0, 0, 0);
    for (auto& option : line.options()) {
        // add label if any
        if (!wxIsEmpty(option.label)) {
            wxStaticText* field_label = new wxStaticText(_parent, -1, __(option.label) + ":", wxDefaultPosition, wxDefaultSize);
            sizer->Add(field_label, 0, wxALIGN_CENTER_VERTICAL,0);
        }

        // add field
        Field* field = _build_field(option);
        if (field != nullptr) {
            if (field->has_sizer()) {
                sizer->Add(field->sizer(), 0, (option.full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
            } else if (field->has_window()) {
                sizer->Add(field->window(), 0, (option.full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
            }
        }

        if (!wxIsEmpty(option.sidetext)) {
        }
		// !!! side_widget !!! find out the purpose
//        if (option.side_widget.valid()) {
//           sizer->Add(option.side_widget.sizer(), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1);
//       }
        if (&option != &line.options().back()) {
            sizer->AddSpacer(4);
        }

        // add side text if any
        // add side widget if any
    }
    // Append extra sizers
    for (auto& widget : line.extra_widgets()) {
        _sizer->Add(widget.sizer(), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
    }
}

Field* OptionsGroup::_build_field(const ConfigOptionDef& opt) {
    Field* built_field = nullptr;
    switch (opt.type) {
        case coString:
            {
            printf("Making new textctrl\n");
            TextCtrl* temp = new TextCtrl(_parent, opt);
            printf("recasting textctrl\n");
            built_field = dynamic_cast<Field*>(temp);
            }
            break;
        default:
            break;
    }
    return built_field;
}
} }
