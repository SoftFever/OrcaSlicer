# This dialog opens up when double clicked on an object line in the list at the right side of the platter.
# One may load additional STLs and additional modifier STLs,
# one may change the properties of the print per each modifier mesh or a Z-span.

package Slic3r::GUI::Plater::ObjectSettingsDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :systemsettings :notebook wxTAB_TRAVERSAL wxTheApp);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Dialog';

# Called with
# %params{object} of a Perl type Slic3r::GUI::Plater::Object
# %params{model_object} of a C++ type Slic3r::ModelObject
sub new {
    my ($class, $parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Settings for " . $params{object}->name, wxDefaultPosition, [700,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->{$_} = $params{$_} for keys %params;

    $self->{tabpanel} = Wx::Notebook->new($self, -1, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);
    $self->{tabpanel}->AddPage($self->{parts} = Slic3r::GUI::Plater::ObjectPartsPanel->new($self->{tabpanel}, model_object => $params{model_object}), "Parts");
    $self->{tabpanel}->AddPage($self->{layers} = Slic3r::GUI::Plater::ObjectDialog::LayersTab->new($self->{tabpanel}), "Layers");
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    EVT_BUTTON($self, wxID_OK, sub {
        # validate user input
        return if !$self->{parts}->CanClose;
        return if !$self->{layers}->CanClose;
        
        # notify tabs
        $self->{layers}->Closing;
        
        # save window size
        wxTheApp->save_window_pos($self, "object_settings");
        
        $self->EndModal(wxID_OK);
        $self->{parts}->Destroy;
        $self->Destroy;
    });
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($self->{tabpanel}, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    $self->Layout;
    
    wxTheApp->restore_window_pos($self, "object_settings");
    
    return $self;
}

sub PartsChanged {
    my ($self) = @_;
    return $self->{parts}->PartsChanged;
}

sub PartSettingsChanged {
    my ($self) = @_;
    return $self->{parts}->PartSettingsChanged || $self->{layers}->LayersChanged;
}

package Slic3r::GUI::Plater::ObjectDialog::BaseTab;
use base 'Wx::Panel';

sub model_object {
    my ($self) = @_;
    # $self->GetParent->GetParent is of type Slic3r::GUI::Plater::ObjectSettingsDialog
    return $self->GetParent->GetParent->{model_object};
}

package Slic3r::GUI::Plater::ObjectDialog::LayersTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Grid;
use Wx::Event qw(EVT_GRID_CELL_CHANGED);
use base 'Slic3r::GUI::Plater::ObjectDialog::BaseTab';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    {
        my $label = Wx::StaticText->new($self, -1, "You can use this section to override the default layer height for parts of this object.",
            wxDefaultPosition, [-1, 40]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $sizer->Add($label, 0, wxEXPAND | wxALL, 10);
    }
    
    my $grid = $self->{grid} = Wx::Grid->new($self, -1, wxDefaultPosition, wxDefaultSize);
    $sizer->Add($grid, 1, wxEXPAND | wxALL, 10);
    $grid->CreateGrid(0, 3);
    $grid->DisableDragRowSize;
    $grid->HideRowLabels;
    $grid->SetColLabelValue(0, "Min Z (mm)");
    $grid->SetColLabelValue(1, "Max Z (mm)");
    $grid->SetColLabelValue(2, "Layer height (mm)");
    $grid->SetColSize($_, 135) for 0..2;
    $grid->SetDefaultCellAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
    
    # load data
    foreach my $range (@{ $self->model_object->layer_height_ranges }) {
        $grid->AppendRows(1);
        my $i = $grid->GetNumberRows-1;
        $grid->SetCellValue($i, $_, $range->[$_]) for 0..2;
    }
    $grid->AppendRows(1); # append one empty row
    
    EVT_GRID_CELL_CHANGED($grid, sub {
        my ($grid, $event) = @_;
        
        # remove any non-numeric character
        my $value = $grid->GetCellValue($event->GetRow, $event->GetCol);
        $value =~ s/,/./g;
        $value =~ s/[^0-9.]//g;
        $grid->SetCellValue($event->GetRow, $event->GetCol, ($event->GetCol == 2) ? $self->_clamp_layer_height($value) : $value);
        
        # if there's no empty row, let's append one
        for my $i (0 .. $grid->GetNumberRows) {
            if ($i == $grid->GetNumberRows) {
                # if we're here then we found no empty row
                $grid->AppendRows(1);
                last;
            }
            if (!grep $grid->GetCellValue($i, $_), 0..2) {
                # exit loop if this row is empty
                last;
            }
        }
        
        $self->{layers_changed} = 1;
    });
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

sub _clamp_layer_height
{
    my ($self, $value) = @_;
    # $self->GetParent->GetParent is of type Slic3r::GUI::Plater::ObjectSettingsDialog
    my $config            = $self->GetParent->GetParent->{config};
    if ($value =~ /^[0-9,.E]+$/) {
        # Looks like a number. Validate the layer height.
        my $nozzle_dmrs       = $config->get('nozzle_diameter');
        my $min_layer_heights = $config->get('min_layer_height');
        my $max_layer_heights = $config->get('max_layer_height');
        my $min_layer_height  = 1000.;
        my $max_layer_height  = 0.;
        my $max_nozzle_dmr    = 0.;
        for (my $i = 0; $i < int(@{$nozzle_dmrs}); $i += 1) {
            $min_layer_height = $min_layer_heights->[$i] if ($min_layer_heights->[$i] < $min_layer_height);
            $max_layer_height = $max_layer_heights->[$i] if ($max_layer_heights->[$i] > $max_layer_height);
            $max_nozzle_dmr   = $nozzle_dmrs      ->[$i] if ($nozzle_dmrs      ->[$i] > $max_nozzle_dmr  );
        }
        $min_layer_height = 0.005 if ($min_layer_height < 0.005);
        $max_layer_height = $max_nozzle_dmr * 0.75 if ($max_layer_height == 0.);
        $max_layer_height = $max_nozzle_dmr if ($max_layer_height > $max_nozzle_dmr);
        return ($value < $min_layer_height) ? $min_layer_height :
               ($value > $max_layer_height) ? $max_layer_height : $value;
    } else {
        # If an invalid numeric value has been entered, use the default layer height.
        return $config->get('layer_height');
    }
}

sub CanClose {
    my $self = shift;
    
    # validate ranges before allowing user to dismiss the dialog
    
    foreach my $range ($self->_get_ranges) {
        my ($min, $max, $height) = @$range;
        if ($max <= $min) {
            Slic3r::GUI::show_error($self, "Invalid Z range $min-$max.");
            return 0;
        }
        if ($min < 0 || $max < 0) {
            Slic3r::GUI::show_error($self, "Invalid Z range $min-$max.");
            return 0;
        }
        if ($height < 0) {
            Slic3r::GUI::show_error($self, "Invalid layer height $height.");
            return 0;
        }
        # TODO: check for overlapping ranges
    }
    
    return 1;
}

sub Closing {
    my $self = shift;
    
    # save ranges into the plater object
    $self->model_object->set_layer_height_ranges([ $self->_get_ranges ]);
}

sub _get_ranges {
    my $self = shift;
    
    my @ranges = ();
    for my $i (0 .. $self->{grid}->GetNumberRows-1) {
        my ($min, $max, $height) = map $self->{grid}->GetCellValue($i, $_), 0..2;
        next if $min eq '' || $max eq '' || $height eq '';
        push @ranges, [ $min, $max, $height ];
    }
    return sort { $a->[0] <=> $b->[0] } @ranges;
}

sub LayersChanged {
    my ($self) = @_;
    return $self->{layers_changed};
}

1;
