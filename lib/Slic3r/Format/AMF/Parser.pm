package Slic3r::Format::AMF::Parser;
use strict;
use warnings;

use XML::SAX::PurePerl;

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
    } elsif ($data->{LocalName} eq 'volume') {
        $self->{_volume_materialid} = $self->_get_attribute($data, 'materialid') || '_';
        $self->{_volume} = [];
    } elsif ($data->{LocalName} eq 'triangle') {
        $self->{_triangle} = ["", "", ""];
    } elsif ($self->{_triangle} && $data->{LocalName} =~ /^v([123])$/ && $self->{_tree}[-1] eq 'triangle') {
        $self->{_vertex_idx} = $1-1;
    } elsif ($data->{LocalName} eq 'material') {
        $self->{_material_id} = $self->_get_attribute($data, 'id') || '_';
        $self->{_material} = {};
    } elsif ($data->{LocalName} eq 'metadata' && $self->{_tree}[-1] eq 'material') {
        $self->{_material_metadata_type} = $self->_get_attribute($data, 'type');
        $self->{_material}{ $self->{_material_metadata_type} } = "";
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
    } elsif ($self->{_material_metadata_type}) {
        $self->{_material}{ $self->{_material_metadata_type} } .= $data->{Data};
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
    } elsif ($data->{LocalName} eq 'volume') {
        $self->{_meshes_by_material}{ $self->{_volume_materialid} } ||= [];
        push @{ $self->{_meshes_by_material}{ $self->{_volume_materialid} } }, @{$self->{_volume}};
        $self->{_volume} = undef;
    } elsif ($data->{LocalName} eq 'triangle') {
        push @{$self->{_volume}}, $self->{_triangle};
        $self->{_triangle} = undef;
    } elsif ($self->{_vertex_idx} && $data->{LocalName} =~ /^v[123]$/) {
        $self->{_vertex_idx} = undef;
    } elsif ($data->{LocalName} eq 'material') {
        $self->{_materials}{ $self->{_material_id} } = $self->{_material};
        $self->{_material_id} = undef;
        $self->{_material} = undef;
    } elsif ($data->{LocalName} eq 'metadata' && $self->{_material}) {
        $self->{_material_metadata_type} = undef;
    }
}

sub _get_attribute {
    my $self = shift;
    my ($data, $name) = @_;
    
    return +(map $_->{Value}, grep $_->{Name} eq $name, values %{$data->{Attributes}})[0];
}

1;
