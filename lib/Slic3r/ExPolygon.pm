package Slic3r::ExPolygon;
use strict;
use warnings;

# an ExPolygon is a polygon with holes

use List::Util qw(first);
use Slic3r::Geometry::Clipper qw(union_ex diff_pl);

sub offset {
    my $self = shift;
    return Slic3r::Geometry::Clipper::offset(\@$self, @_);
}

sub offset_ex {
    my $self = shift;
    return Slic3r::Geometry::Clipper::offset_ex(\@$self, @_);
}

sub noncollapsing_offset_ex {
    my $self = shift;
    my ($distance, @params) = @_;
    
    return $self->offset_ex($distance + 1, @params);
}

sub bounding_box {
    my $self = shift;
    return $self->contour->bounding_box;
}

package Slic3r::ExPolygon::Collection;

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_2D([ map @$_, map @$_, @$self ]) ];
}

1;
