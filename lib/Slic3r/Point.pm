package Slic3r::Point;
use strict;
use warnings;

sub new {
    my $class = shift;
    my $self;
    if (@_ == 2) {
        $self = [@_];
    } elsif (ref $_[0] eq 'ARRAY') {
        $self = [@{$_[0]}];
    } elsif ($_[0]->isa(__PACKAGE__)) {
        return $_[0];
    } else {
        use XXX;
        ZZZ "test";
        die "Invalid arguments for ${class}->new";
    }
    bless $self, $class;
    return $self;
}

sub id {
    my $self = shift;
    return join ',', @$self;
}

sub coordinates {
    my $self = shift;
    return @$self;
}

sub coincides_with {
    my $self = shift;
    my ($point) = @_;
    return Slic3r::Geometry::points_coincide($self, $point);
}

sub distance_to {
    my $self = shift;
    my ($point) = @_;
    return Slic3r::Geometry::distance_between_points($self, $point);
}

sub x { $_[0]->[0] }
sub y { $_[0]->[1] }

1;
