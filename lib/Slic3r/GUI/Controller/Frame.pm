package Slic3r::GUI::Controller::Frame;
use strict;
use warnings;
use utf8;

use Wx qw(:frame :id :misc :sizer);
use Wx::Event qw(EVT_CLOSE);
use base 'Wx::Frame';

sub new {
    my ($class) = @_;
    my $self = $class->SUPER::new(undef, -1, "Controller", wxDefaultPosition, [500,350], wxDEFAULT_FRAME_STYLE);
    
    $self->{sizer} = my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add(Slic3r::GUI::Controller::PrinterPanel->new($self), 1, wxEXPAND);
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    $self->Layout;
    
    EVT_CLOSE($self, sub {
        my (undef, $event) = @_;
        
        # ...
        
        $event->Skip;
    });
    
    return $self;
}

1;
