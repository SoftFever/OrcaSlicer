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
    
    my @my_paths = @{$self->paths};
    my @paths = ();
    my $start_at;
    CYCLE: while (@my_paths) {
        # find nearest point
        my $endpoints = [ map $_->endpoints, @my_paths ];
        $start_at = $start_near
            ? Slic3r::Point->new(Slic3r::Geometry::nearest_point($start_near, $endpoints))
            : $self->endpoints->[0];
        
        # loop through paths to find the one that starts or ends at the point found
        PATH: for (my $i = 0; $i <= $#my_paths; $i++) {
            if ($start_at->id eq $my_paths[$i]->points->[0]->id) {
                push @paths, splice @my_paths, $i, 1;
            } elsif ($start_at->id eq $my_paths[$i]->points->[-1]->id) {
                $my_paths[$i]->reverse;
                push @paths, splice @my_paths, $i, 1;
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
