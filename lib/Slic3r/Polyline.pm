package Slic3r::Polyline;
use Moo;

use Math::Clipper qw();
use Sub::Quote;

# arrayref of ordered points
has 'points' => (
    is          => 'rw',
    required    => 1,
    default     => sub { [] },
    isa         => quote_sub q{ use Carp; confess "invalid points" if grep ref $_ ne 'Slic3r::Point', @{$_[0]} },
);

sub id {
    my $self = shift;
    return join ' - ', map $_->id, @{$self->points};
}

sub cast {
    my $self = shift;
    my ($points) = @_;
    
    @$points = map { ref $_ eq 'ARRAY' ? Slic3r::Point->cast($_) : $_ } @$points;
    return __PACKAGE__->new(points => $points);
}

sub lines {
    my $self = shift;
    my @lines = ();
    my $previous_point;
    foreach my $point (@{ $self->points }) {
        if ($previous_point) {
            push @lines, Slic3r::Line->new(points => [ $previous_point, $point ]);
        }
        $previous_point = $point;
    }
    return @lines;
}

sub p {
    my $self = shift;
    return [ map $_->p, @{$self->points} ];
}

sub merge_continuous_lines {
    my $self = shift;
    
    my $last_line;
    foreach my $line ($self->lines) {
        if (defined $last_line && $line->parallel_to($last_line)) {
            # $line and $last_line are parallel and continuous,
            # so we can remove their common point from our polyline
            
            # find common point
            my ($common_point) = grep $_ eq $line->a || $_ eq $line->b, @{$last_line->points};
            
            # remove point from polyline
            @{$self->points} = grep $_ ne $common_point, @{$self->points};
        }
        $last_line = $line;
    }
}

sub reverse_points {
    my $self = shift;
    @{$self->points} = reverse @{$self->points};
}

sub is_counter_clockwise {
    my $self = shift;
    return Math::Clipper::is_counter_clockwise($self->p);
}

sub make_counter_clockwise {
    my $self = shift;
    $self->reverse_points if !$self->is_counter_clockwise;
}

sub make_clockwise {
    my $self = shift;
    $self->reverse_points if $self->is_counter_clockwise;
}

1;
