package Slic3r::ExtrusionPath::Collection;
use Moo;

use XXX;

has 'paths' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::ExtrusionPath]',
    default => sub { [] },
);

sub add {
    my $self = shift;
    my ($path) = @_;
    
    push @{$self->paths}, $path;
}

sub endpoints {
    my $self = shift;
    my ($as_arrayref) = @_;
    return map $_->endpoints($as_arrayref), @{$self->paths};
}

sub shortest_path {
    my $self = shift;
    my ($start_near) = @_;
    
    # get point as arrayref
    $start_near = $start_near->p if $start_near && ref $start_near ne 'ARRAY';
    
    my @paths = ();
    my $start_at;
    CYCLE: while (@{$self->paths}) {
        # find nearest point
        $start_at = Slic3r::Point->cast(Slic3r::Geometry::nearest_point($start_near, [ $self->endpoints(1) ]));
        
        # loop through paths to find the one that starts or ends at the point found
        PATH: for (my $i = 0; $i <= $#{$self->paths}; $i++) {
            if ($start_at->id eq $self->paths->[$i]->points->[0]->id) {
                push @paths, splice @{$self->paths}, $i, 1;
            } elsif ($start_at->id eq $self->paths->[$i]->points->[-1]->id) {
                $self->paths->[$i]->reverse;
                push @paths, splice @{$self->paths}, $i, 1;
            } else {
                next PATH;
            }
            $start_near = $paths[-1]->points->[-1]->p;
            next CYCLE;
        }
    }
    return @paths;
}

1;
