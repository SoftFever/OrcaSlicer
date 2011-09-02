package Slic3r::Polyline;
use Moose;

has 'lines' => (
    traits  => ['Array'],
    is      => 'rw',
    isa     => 'ArrayRef[Slic3r::Line]',
    default => sub { [] },
    handles => {
        add_line => 'push',
    },
);

after 'add_line' => sub {
    my $self = shift;
    
    # add a weak reference to this polyline in line objects
    # (avoid circular refs)
    $self->lines->[-1]->polyline($self);
};

sub BUILD {
    my $self = shift;
    $_->polyline($self) for @{ $self->lines };
}

sub id {
    my $self = shift;
    return join '-', map($_->id, $self->ordered_points(1));
}

sub new_from_points {
    my $class = shift;
    my (@points) = @_;
    
    # we accept Point objects or arrayrefs with point coordinates
    @points = map {
        ref $_ eq 'ARRAY' 
            ? Slic3r::Point->new('x' => $_->[0], 'y' => $_->[1])
            : $_
    } @points;
    
    my $polyline = __PACKAGE__->new;
    my $previous_point;
    $previous_point = $points[-1] if $class eq 'Slic3r::Polyline::Closed';
    foreach my $point (@points) {
        if ($previous_point) {
            my $line = Slic3r::Line->new(a => $previous_point, b => $point);
            $polyline->add_line($line);
        }
        $previous_point = $point;
    }
    
    return $polyline;
}

sub points {
    my $self = shift;
    my %points = ();
    $points{$_} = $_ for map $_->points, @{ $self->lines };
    return values %points;
}

sub ordered_points {
    my $self = shift;
    my ($as_objects) = @_;
    my $points = [];
    
    #printf "\n\n==> Number of lines: %d\n", scalar @{ $self->lines };
    my @lines = @{ $self->lines };
    while (@lines && @$points < @{ $self->lines }) {
        #printf "\nNumber of points: %d\n", scalar @{ $points };
        my @temp = @lines;
        @lines = ();
        foreach my $line (@temp) {
            #printf "Line: %s\n", $line->id;
            my $point;
            if (!@$points) {
                # make sure we start from a point not connected to another segment if any
                push @$points, sort { @{$a->lines} <=> @{$b->lines} } $line->points;
                next;
            } elsif ($line->has_endpoint($points->[-1])) {
                $point = +(grep $points->[-1] ne $_, $line->points)[0];
            }
            if (!$point) {
                #printf "  no point found, retrying\n";
                push @lines, $line;
                next;
            }
            #printf "  adding point %s\n", $point->id;
            push @$points, $point;
        }
    }
    
    pop @$points
        if $self->isa('Slic3r::Polyline::Closed') && $points->[0]->coincides_with($points->[-1]);
    
    return @$points if $as_objects;
    
    $points = [ map [ $_->x, $_->y ], @$points ]; #]
    return $points;
}

1;
