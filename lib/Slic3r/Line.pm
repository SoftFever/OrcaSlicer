package Slic3r::Line;
use strict;
use warnings;

sub new {
    my $class = shift;
    my $self;
    if (@_ == 2) {
        $self = [ map Slic3r::Point->new($_), @_ ];
    } elsif (ref $_[0] eq 'ARRAY') {
        $self = [ map Slic3r::Point->new($_), @{$_[0]} ];
    } elsif ($_[0]->isa(__PACKAGE__)) {
        return $_[0];
    } else {
        die "Invalid argument for $class->new";
    }
    bless $self, $class;
    return $self;
}

sub a { $_[0][0] }
sub b { $_[0][1] }

sub id {
    my $self = shift;
    return $self->a->id . "-" . $self->b->id;
}

sub ordered_id {
    my $self = shift;
    return join('-', sort map $_->id, @$self);
}

sub coordinates {
    my $self = shift;
    return ($self->a->coordinates, $self->b->coordinates);
}

sub coincides_with {
    my $self = shift;
    my ($line) = @_;
    
    return ($self->a->coincides_with($line->a) && $self->b->coincides_with($line->b))
        || ($self->a->coincides_with($line->b) && $self->b->coincides_with($line->a));
}

sub has_endpoint {
    my $self = shift;
    my ($point) = @_;
    return $point->coincides_with($self->a) || $point->coincides_with($self->b);
}

sub has_segment {
    my $self = shift;
    my ($line) = @_;
    
    # a segment belongs to another segment if its points belong to it
    return Slic3r::Geometry::point_in_segment($line->[0], $self)
        && Slic3r::Geometry::point_in_segment($line->[1], $self);
}

sub parallel_to {
    my $self = shift;
    my ($line) = @_;
    return Slic3r::Geometry::lines_parallel($self, $line);
}

1;
