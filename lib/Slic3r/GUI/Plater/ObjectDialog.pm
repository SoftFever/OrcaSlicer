package Slic3r::GUI::Plater::ObjectDialog;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Object Info", wxDefaultPosition, wxDefaultSize);
    $self->{object} = $params{object};

    my $properties_box = Wx::StaticBox->new($self, -1, "Info", wxDefaultPosition, [400,200]);
    my $grid_sizer = Wx::FlexGridSizer->new(3, 2, 10, 5);
    $properties_box->SetSizer($grid_sizer);
    $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    $grid_sizer->AddGrowableCol(1);
    
    my $label_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $label_font->SetPointSize(10);
    
    my $properties = $self->get_properties;
    foreach my $property (@$properties) {
    	my $label = Wx::StaticText->new($properties_box, -1, $property->[0] . ":");
    	my $value = Wx::StaticText->new($properties_box, -1, $property->[1]);
    	$label->SetFont($label_font);
	    $grid_sizer->Add($label, 1, wxALIGN_BOTTOM);
	    $grid_sizer->Add($value, 0);
    }
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    EVT_BUTTON($self, wxID_OK, sub { $self->EndModal(wxID_OK); });
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($properties_box, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

sub get_properties {
	my $self = shift;
	
	return [
		['Name'			=> $self->{object}->name],
		['Size'			=> sprintf "%.2f x %.2f x %.2f", @{$self->{object}->size}],
		['Facets'		=> $self->{object}->facets],
		['Vertices'		=> $self->{object}->vertices],
		['Materials' 	=> $self->{object}->materials],
		['Two-Manifold' => $self->{object}->is_manifold ? 'Yes' : 'No'],
	];
}

1;
