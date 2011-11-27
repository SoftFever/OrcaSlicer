package Slic3r::TriangleMesh::IntersectionLine;
use Moo;

has 'a'             => (is => 'ro', required => 1);
has 'b'             => (is => 'ro', required => 1);
has 'a_id'          => (is => 'ro', required => 1);
has 'b_id'          => (is => 'ro', required => 1);
has 'facet_index'   => (is => 'ro', required => 1);
has 'prev_facet_index' => (is => 'ro', required => 0);
has 'next_facet_index' => (is => 'ro', required => 0);
has 'facet_edge'    => (is => 'ro', default => sub {0});

sub points {
    my $self = shift;
    return [$self->a, $self->b];
}

sub line {
    my $self = shift;
    return Slic3r::Line->new($self->a, $self->b);
}

1;
