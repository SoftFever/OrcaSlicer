package Slic3r::ExPolygon;
use strict;
use warnings;

# an ExPolygon is a polygon with holes

use List::Util qw(first);
use Slic3r::Geometry qw(X Y A B point_in_polygon epsilon scaled_epsilon);
use Slic3r::Geometry::Clipper qw(union_ex diff_pl);

sub wkt {
    my $self = shift;
    return sprintf "POLYGON(%s)", 
        join ',', map "($_)", map { join ',', map "$_->[0] $_->[1]", @$_ } @$self;
}

sub dump_perl {
    my $self = shift;
    return sprintf "[%s]", 
        join ',', map "[$_]", map { join ',', map "[$_->[0],$_->[1]]", @$_ } @$self;
}

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
use Slic3r::Geometry qw(X1 Y1);

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_2D([ map @$_, map @$_, @$self ]) ];
}

1;
