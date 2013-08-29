package Slic3r::ExtrusionPath::Collection;
use strict;
use warnings;

sub cleanup {
    my $self = shift;
    
    # split paths at angles that are too acute to be printed as they will cause blobs
    my @paths = map $_->split_at_acute_angles, @$self;
    $self->clear;
    $self->append(@paths);
}

1;
