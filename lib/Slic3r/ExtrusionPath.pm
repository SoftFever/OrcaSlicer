package Slic3r::ExtrusionPath;
use Moo;

extends 'Slic3r::Polyline';

# this integer represents the vertical thickness of the extrusion
# expressed in layers
has 'depth_layers' => (is => 'ro', default => sub {1});

use constant PI => 4 * atan2(1, 1);

sub clip_end {
    my $self = shift;
    my ($distance) = @_;
    
    while ($distance > 0) {
        my $last_point = pop @{$self->points};
        
        my $last_segment_length = $last_point->distance_to($self->points->[-1]);
        if ($last_segment_length <= $distance) {
            $distance -= $last_segment_length;
            next;
        }
        
        my $new_point = Slic3r::Geometry::point_along_segment($last_point, $self->points->[-1], $distance);
        push @{$self->points}, Slic3r::Point->new($new_point);
        $distance = 0;
    }
}

sub endpoints {
    my $self = shift;
    return ($self->points->[0], $self->points->[-1]);
}

sub reverse {
    my $self = shift;
    @{$self->points} = reverse @{$self->points};
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
            push @paths, (ref $self)->cast([@p]);
            @p = ($p3);
            push @p, grep $_, shift @points or last;
        } else {
            push @p, $p3;
        }
    }
    push @paths, (ref $self)->cast([@p]) if @p > 1;
    return @paths;
}

1;
