package Slic3r::AMF::Parser;
use strict;
use warnings;

use XML::SAX::ExpatXS;
use base 'XML::SAX::Base';

my %xyz_index = (x => 0, y => 1, z => 2); #=

sub new {
    my $self = shift->SUPER::new(@_);
    $self->{_tree} = [];
    $self;
}

sub start_element {
    my $self = shift;
    my $data = shift;
    
    if ($data->{LocalName} eq 'vertex') {
        $self->{_vertex} = ["", "", ""];
    } elsif ($self->{_vertex} && $data->{LocalName} =~ /^[xyz]$/ && $self->{_tree}[-1] eq 'coordinates') {
        $self->{_coordinate} = $data->{LocalName};
    } elsif ($data->{LocalName} eq 'triangle') {
        $self->{_triangle} = [[], "", "", ""];  # empty normal
    } elsif ($self->{_triangle} && $data->{LocalName} =~ /^v([123])$/ && $self->{_tree}[-1] eq 'triangle') {
        $self->{_vertex_idx} = $1;
    }
    
    push @{$self->{_tree}}, $data->{LocalName};
}

sub characters {
    my $self = shift;
    my $data = shift;
    
    if ($self->{_vertex} && $self->{_coordinate}) {
        $self->{_vertex}[ $xyz_index{$self->{_coordinate}} ] .= $data->{Data};
    } elsif ($self->{_triangle} && defined $self->{_vertex_idx}) {
        $self->{_triangle}[ $self->{_vertex_idx} ] .= $data->{Data};
    }
}

sub end_element {
    my $self = shift;
    my $data = shift;
    
    pop @{$self->{_tree}};
    
    if ($data->{LocalName} eq 'vertex') {
        push @{$self->{_vertices}}, $self->{_vertex};
        $self->{_vertex} = undef;
    } elsif ($self->{_coordinate} && $data->{LocalName} =~ /^[xyz]$/) {
        $self->{_coordinate} = undef;
    } elsif ($data->{LocalName} eq 'triangle') {
        push @{$self->{_facets}}, $self->{_triangle};
        $self->{_triangle} = undef;
    } elsif ($self->{_vertex_idx} && $data->{LocalName} =~ /^v[123]$/) {
        $self->{_vertex_idx} = undef;
    }
    
}

1;
