package Slic3r::GUI::Plater::ObjectPreviewDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :systemsettings :notebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, $params{object}->name, wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->{model_object} = $params{model_object};
    
    my $canvas = Slic3r::GUI::PreviewCanvas->new($self);
    $canvas->load_object($self->{model_object});
    $canvas->set_bounding_box($self->{model_object}->bounding_box);
    $canvas->set_auto_bed_shape;
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($canvas, 1, wxEXPAND, 0);
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    # needed to actually free memory
    EVT_CLOSE($self, sub {
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    return $self;
}

1;
