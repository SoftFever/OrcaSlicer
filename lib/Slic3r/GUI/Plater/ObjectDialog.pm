package Slic3r::GUI::Plater::ObjectDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :systemsettings :notebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Object", wxDefaultPosition, [500,350]);
    $self->{object} = $params{object};
    
    $self->{tabpanel} = Wx::Notebook->new($self, -1, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);
    $self->{tabpanel}->AddPage($self->{info} = Slic3r::GUI::Plater::ObjectDialog::InfoTab->new($self->{tabpanel}), "Info");
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    EVT_BUTTON($self, wxID_OK, sub { $self->EndModal(wxID_OK); });
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($self->{tabpanel}, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    
    return $self;
}

package Slic3r::GUI::Plater::ObjectDialog::InfoTab;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    
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
	
	my $object = $self->GetParent->GetParent->{object};
	return [
		['Name'			=> $object->name],
		['Size'			=> sprintf "%.2f x %.2f x %.2f", @{$object->size}],
		['Facets'		=> $object->facets],
		['Vertices'		=> $object->vertices],
		['Materials' 	=> $object->materials],
		['Two-Manifold' => $object->is_manifold ? 'Yes' : 'No'],
	];
}

1;
