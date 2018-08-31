# Status bar at the bottom of the main screen.
# Now it just implements cancel cb on perl side, every other functionality is
# in C++

package Slic3r::GUI::ProgressStatusBar; 
use strict;
use warnings;

our $cancel_cb;

sub SetCancelCallback {
    my $self = shift;
    my ($cb) = @_;
    $cancel_cb = $cb;
    $cb ? $self->ShowCancelButton : $self->HideCancelButton;
}

1;
