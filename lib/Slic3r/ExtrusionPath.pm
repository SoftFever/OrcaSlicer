package Slic3r::ExtrusionPath;
use strict;
use warnings;

use parent qw(Exporter);

our @EXPORT_OK = qw(EXTR_ROLE_PERIMETER EXTR_ROLE_EXTERNAL_PERIMETER 
    EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER EXTR_ROLE_OVERHANG_PERIMETER
    EXTR_ROLE_FILL EXTR_ROLE_SOLIDFILL EXTR_ROLE_TOPSOLIDFILL EXTR_ROLE_BRIDGE 
    EXTR_ROLE_INTERNALBRIDGE EXTR_ROLE_SKIRT EXTR_ROLE_SUPPORTMATERIAL EXTR_ROLE_GAPFILL);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);

use Slic3r::Geometry qw(PI X Y epsilon deg2rad rotate_points);

sub clip_with_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    return $self->clip_with_expolygon(Slic3r::ExPolygon->new($polygon));
}

sub clip_with_expolygon {
    my $self = shift;
    my ($expolygon) = @_;
    
    return map $self->clone(polyline => $_),
        $self->polyline->clip_with_expolygon($expolygon);
}

sub intersect_expolygons {
    my $self = shift;
    my ($expolygons) = @_;
    
    return map $self->clone(polyline => Slic3r::Polyline->new(@$_)),
        @{Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection([ map $_->pp, @$expolygons ], [$self->pp])};
}

sub subtract_expolygons {
    my $self = shift;
    my ($expolygons) = @_;
    
    return map $self->clone(polyline => Slic3r::Polyline->new(@$_)),
        @{Boost::Geometry::Utils::multi_linestring_multi_polygon_difference([$self->pp], [ map $_->pp, @$expolygons ])};
}

sub simplify {
    my $self = shift;
    $self->polyline($self->polyline->simplify(@_));
}

sub clip_end {
    my $self = shift;
    my $polyline = $self->polyline;
    $polyline->clip_end(@_);
    $self->polyline($polyline);
}

sub length {
    my $self = shift;
    return $self->polyline->length;
}

sub points {
    my $self = shift;
    return $self->polyline;
}

sub last_point {
    my $self = shift;
    return $self->polyline->[-1];
}

sub is_perimeter {
    my $self = shift;
    return $self->role == EXTR_ROLE_PERIMETER
        || $self->role == EXTR_ROLE_EXTERNAL_PERIMETER
        || $self->role == EXTR_ROLE_OVERHANG_PERIMETER
        || $self->role == EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER;
}

sub is_fill {
    my $self = shift;
    return $self->role == EXTR_ROLE_FILL
        || $self->role == EXTR_ROLE_SOLIDFILL
        || $self->role == EXTR_ROLE_TOPSOLIDFILL;
}

sub is_bridge {
    my $self = shift;
    return $self->role == EXTR_ROLE_BRIDGE
        || $self->role == EXTR_ROLE_INTERNALBRIDGE
        || $self->role == EXTR_ROLE_OVERHANG_PERIMETER;
}

sub split_at_acute_angles {
    my $self = shift;
    
    # calculate angle limit
    my $angle_limit = abs(Slic3r::Geometry::deg2rad(40));
    my @points = @{$self->p};
    
    my @paths = ();
    
    # take first two points
    my @p = splice @points, 0, 2;
    
    # loop until we have one spare point
    while (my $p3 = shift @points) {
        my $angle = abs(Slic3r::Geometry::angle3points($p[-1], $p[-2], $p3));
        $angle = 2*PI - $angle if $angle > PI;
        
        if ($angle < $angle_limit) {
            # if the angle between $p[-2], $p[-1], $p3 is too acute
            # then consider $p3 only as a starting point of a new
            # path and stop the current one as it is
            push @paths, $self->clone(polyline => Slic3r::Polyline->new(@p));
            @p = ($p3);
            push @p, grep $_, shift @points or last;
        } else {
            push @p, $p3;
        }
    }
    push @paths, $self->clone(polyline => Slic3r::Polyline->new(@p))
        if @p > 1;
    
    return @paths;
}

1;
