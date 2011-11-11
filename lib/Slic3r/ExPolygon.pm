package Slic3r::ExPolygon;
use strict;
use warnings;

# an ExPolygon is a polygon with holes

use Math::Clipper qw(CT_UNION PFT_NONZERO JT_MITER);
use Slic3r::Geometry::Clipper qw(union_ex);

# the constructor accepts an array of polygons 
# or a Math::Clipper ExPolygon (hashref)
sub new {
    my $class = shift;
    my $self;
    if (@_ == 1 && ref $_[0] eq 'HASH') {
        $self = [
            Slic3r::Polygon->new($_[0]{outer}),
            map Slic3r::Polygon->new($_), @{$_[0]{holes}},
        ];
    } else {
        $self = [ map Slic3r::Polygon->new($_), @_ ];
    }
    bless $self, $class;
    $self;
}

# this class method accepts an array of polygons and returns
# an array of expolygons with the right holes applied to the 
# right contours
sub make {
    my $class = shift;
    return @{ union_ex(\@_) };
}

sub contour {
    my $self = shift;
    return $self->[0];
}

sub holes {
    my $self = shift;
    return @$self[1..$#$self];
}

sub clipper_expolygon {
    my $self = shift;
    return {
        outer => $self->contour,
        holes => [ $self->holes ],
    };
}

sub offset {
    my $self = shift;
    my ($distance, $scale, $joinType, $miterLimit) = @_;
    $scale      ||= $Slic3r::resolution * 1000000;
    $joinType   = JT_MITER if !defined $joinType;
    $miterLimit ||= 2;
    
    my $offsets = Math::Clipper::offset($self, $distance, $scale, $joinType, $miterLimit);
    
    # apply holes to the right contours
    return (ref $self)->make(@$offsets);
}

1;
