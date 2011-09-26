package Slic3r::ExtrusionPath;
use Moo;

extends 'Slic3r::Polyline';

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
        
        my $new_point = Slic3r::Geometry::point_along_segment($last_point->p, $self->points->[-1]->p, $distance);
        push @{$self->points}, Slic3r::Point->cast($new_point);
        $distance = 0;
    }
}

sub endpoints {
    my $self = shift;
    my ($as_arrayref) = @_;
    my @points = ($self->points->[0], $self->points->[-1]);
    return $as_arrayref ? map($_->p, @points) : @points;
}

sub reverse {
    my $self = shift;
    @{$self->points} = reverse @{$self->points};
}

1;
