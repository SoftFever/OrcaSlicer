package Slic3r::Polyline;
use strict;
use warnings;

use Math::Clipper qw();
use Slic3r::Geometry qw(A B polyline_remove_parallel_continuous_edges polyline_remove_acute_vertices
    move_points same_point);
use XXX;

# the constructor accepts an array(ref) of points
sub new {
    my $class = shift;
    my $self;
    if (@_ == 1) {
        $self = [ @{$_[0]} ];
    } else {
        $self = [ @_ ];
    }
    
    bless $self, $class;
    bless $_, 'Slic3r::Point' for @$self;
    $self;
}

sub id {
    my $self = shift;
    return join ' - ', sort map $_->id, @$self;
}

sub lines {
    my $self = shift;
    my @lines = ();
    my $previous_point;
    foreach my $point (@$self) {
        if ($previous_point) {
            push @lines, Slic3r::Line->new($previous_point, $point);
        }
        $previous_point = $point;
    }
    return @lines;
}

sub merge_continuous_lines {
    my $self = shift;
    
    polyline_remove_parallel_continuous_edges($self);
    bless $_, 'Slic3r::Point' for @$self;
}

sub remove_acute_vertices {
    my $self = shift;
    polyline_remove_acute_vertices($self);
    bless $_, 'Slic3r::Point' for @$self;
}

sub simplify {
    my $self = shift;
    my $tolerance = shift || 10;
    
    @$self = Slic3r::Geometry::Douglas_Peucker($self, $tolerance);
    bless $_, 'Slic3r::Point' for @$self;
}

sub reverse {
    my $self = shift;
    @$self = CORE::reverse @$self;
}

sub is_counter_clockwise {
    my $self = shift;
    return Math::Clipper::is_counter_clockwise($self);
}

sub make_counter_clockwise {
    my $self = shift;
    $self->reverse if !$self->is_counter_clockwise;
}

sub make_clockwise {
    my $self = shift;
    $self->reverse if $self->is_counter_clockwise;
}

sub nearest_point_to {
    my $self = shift;
    my ($point) = @_;
    
    $point = Slic3r::Geometry::nearest_point($point, $self);
    return Slic3r::Point->new($point);
}

sub has_segment {
    my $self = shift;
    my ($line) = @_;
    
    for ($self->lines) {
        return 1 if $_->has_segment($line);
    }
    return 0;
}

sub clip_with_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    return $self->clip_with_expolygon(Slic3r::ExPolygon->new($polygon));
}

sub clip_with_expolygon {
    my $self = shift;
    my ($expolygon) = @_;
    
    my @polylines = ();
    my $current_polyline = [];
    foreach my $line ($self->lines) {
        my ($first_line, @other_lines) = @{ $expolygon->clip_line($line) };
        next unless $first_line;
        
        if (!@$current_polyline) {
            push @$current_polyline, @$first_line;
        } elsif ($first_line->[A]->coincides_with($current_polyline->[-1])) {
            push @$current_polyline, $first_line->[B];
        } else {
            push @polylines, $current_polyline;
            $current_polyline = [ @$first_line ];
        }
        
        foreach my $other_line (@other_lines) {
            if (@$current_polyline) {
                push @polylines, $current_polyline;
                $current_polyline = [];
            }
            push @polylines, [ @$other_line ];
        }
    }
    if (@$current_polyline) {
        push @polylines, $current_polyline;
    }
    
    if (@polylines > 1 && same_point($polylines[-1][-1], $polylines[0][0])) {
        if (scalar(@{$polylines[-1]}) == 2) {
            unshift @{$polylines[0]}, $polylines[-1][0];
            pop @polylines;
        } else {
            push @{$polylines[-1]}, $polylines[0][-1];
            shift @polylines;
        }
    }
    
    return map Slic3r::Polyline->new($_), @polylines;
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::bounding_box($self);
}

sub translate {
    my $self = shift;
    my ($x, $y) = @_;
    @$self = move_points([$x, $y], @$self);
}

1;
