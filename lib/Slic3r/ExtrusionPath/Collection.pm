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
    return [ map $_->endpoints, @{$self->paths} ];
}

sub shortest_path {
    my $self = shift;
    my ($start_near) = @_;
    
    my @paths = ();
    my $start_at;
    CYCLE: while (@{$self->paths}) {
        # find nearest point
        $start_at = $start_near
            ? Slic3r::Point->new(Slic3r::Geometry::nearest_point($start_near, $self->endpoints))
            : $self->endpoints->[0];
        
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
            $start_near = $paths[-1]->points->[-1];
            next CYCLE;
        }
    }
    return @paths;
}

sub cleanup {
    my $self = shift;
    
    # split paths at angles that are too acute to be printed as they will cause blobs
    @{$self->paths} = map $_->split_at_acute_angles, @{$self->paths};
}

sub detect_arcs {
    my $self = shift;
    @{$self->paths} = map $_->detect_arcs(@_), @{$self->paths};
}

1;
