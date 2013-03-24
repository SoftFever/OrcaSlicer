package Slic3r::GUI::Plater::ObjectDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :systemsettings :notebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Object", wxDefaultPosition, [500,350]);
    $self->{object} = $params{object};
    
    $self->{tabpanel} = Wx::Notebook->new($self, -1, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);
    $self->{tabpanel}->AddPage($self->{info} = Slic3r::GUI::Plater::ObjectDialog::InfoTab->new($self->{tabpanel}, object => $self->{object}), "Info");
    $self->{tabpanel}->AddPage($self->{layers} = Slic3r::GUI::Plater::ObjectDialog::LayersTab->new($self->{tabpanel}, object => $self->{object}), "Layers");
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    EVT_BUTTON($self, wxID_OK, sub {
        # validate user input
        return if !$self->{layers}->CanClose;
        
        # notify tabs
        $self->{layers}->Closing;
        
        $self->EndModal(wxID_OK);
    });
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($self->{tabpanel}, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    
    return $self;
}

package Slic3r::GUI::Plater::ObjectDialog::InfoTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};
    
    my $grid_sizer = Wx::FlexGridSizer->new(3, 2, 10, 5);
    $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    $grid_sizer->AddGrowableCol(1);
    
    my $label_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $label_font->SetPointSize(10);
    
    my $properties = $self->get_properties;
    foreach my $property (@$properties) {
    	my $label = Wx::StaticText->new($self, -1, $property->[0] . ":");
    	my $value = Wx::StaticText->new($self, -1, $property->[1]);
    	$label->SetFont($label_font);
	    $grid_sizer->Add($label, 1, wxALIGN_BOTTOM);
	    $grid_sizer->Add($value, 0);
    }
    
    $self->SetSizer($grid_sizer);
    $grid_sizer->SetSizeHints($self);
    
    return $self;
}

sub get_properties {
	my $self = shift;
	
	my $object = $self->{object};
	return [
		['Name'			=> $object->name],
		['Size'			=> sprintf "%.2f x %.2f x %.2f", @{$object->size}],
		['Facets'		=> $object->facets],
		['Vertices'		=> $object->vertices],
		['Materials' 	=> $object->materials],
		['Two-Manifold' => $object->is_manifold ? 'Yes' : 'No'],
	];
}

package Slic3r::GUI::Plater::ObjectDialog::LayersTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Grid;
use Wx::Event qw(EVT_GRID_CELL_CHANGED);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    {
        my $label = Wx::StaticText->new($self, -1, "You can use this section to override the default layer height for parts of this object. Set layer height to zero to skip portions of the input file.",
            wxDefaultPosition, [-1, 25]);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $sizer->Add($label, 0, wxEXPAND | wxALL, 10);
    }
    
    my $grid = $self->{grid} = Wx::Grid->new($self, -1, wxDefaultPosition, wxDefaultSize);
    $sizer->Add($grid, 1, wxEXPAND | wxALL, 10);
    $grid->CreateGrid(0, 3);
    $grid->DisableDragRowSize;
    $grid->HideRowLabels if &Wx::wxVERSION_STRING !~ / 2\.8\./;
    $grid->SetColLabelValue(0, "Min Z (mm)");
    $grid->SetColLabelValue(1, "Max Z (mm)");
    $grid->SetColLabelValue(2, "Layer height (mm)");
    $grid->SetColSize($_, 135) for 0..2;
    $grid->SetDefaultCellAlignment(wxALIGN_CENTRE, wxALIGN_CENTRE);
    
    # load data
    foreach my $range (@{ $self->{object}->layer_height_ranges }) {
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
        $grid->SetCellValue($event->GetRow, $event->GetCol, $value);
        
        # if there's no empty row, let's append one
        for my $i (0 .. $grid->GetNumberRows-1) {
            if (!grep $grid->GetCellValue($i, $_), 0..2) {
                return;
            }
        }
        $grid->AppendRows(1);
    });
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
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
    $self->{object}->layer_height_ranges([ $self->_get_ranges ]);
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

1;
