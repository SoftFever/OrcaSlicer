package Slic3r::Point;
use strict;
use warnings;

sub new {
    my $class = shift;
    my $self;
    if (@_ == 2) {
        $self = [@_];
    } elsif ((ref $_[0]) =~ 'ARRAY' || (ref $_[0]) =~ /Slic3r::Point/) {
        $self = [@{$_[0]}];
    } elsif ($_[0]->isa(__PACKAGE__)) {
        return $_[0];
    } else {
        die "Invalid arguments for ${class}->new";
    }
    bless $self, $class;
    return $self;
}

sub clone {
    my $self = shift;
    return (ref $self)->new(@$self);
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

sub rotate {
    my $self = shift;
    my ($angle, $center) = @_;
    @$self = @{ +(Slic3r::Geometry::rotate_points($angle, $center, $self))[0] };
    $self;
}

sub translate {
    my $self = shift;
    my ($x, $y) = @_;
    @$self = @{ +(Slic3r::Geometry::move_points([$x, $y], $self))[0] };
    $self;
}

sub x { $_[0]->[0] }
sub y { $_[0]->[1] }

1;
