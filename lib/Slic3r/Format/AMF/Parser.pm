package Slic3r::Format::AMF::Parser;
use strict;
use warnings;

use base 'XML::SAX::Base';

my %xyz_index = (x => 0, y => 1, z => 2); #=

sub new {
    my $self = shift->SUPER::new(@_);
    $self->{_tree} = [];
    $self->{_objects_map} = {};  # this hash maps AMF object IDs to object indexes in $model->objects
    $self->{_instances} = {};   # apply these lazily to make sure all objects have been parsed
    $self;
}

sub start_element {
    my $self = shift;
    my $data = shift;
    
    if ($data->{LocalName} eq 'object') {
        $self->{_object} = $self->{_model}->add_object;
        $self->{_object_vertices} = [];
        $self->{_objects_map}{ $self->_get_attribute($data, 'id') } = $#{ $self->{_model}->objects };
    } elsif ($data->{LocalName} eq 'vertex') {
        $self->{_vertex} = ["", "", ""];
    } elsif ($self->{_vertex} && $data->{LocalName} =~ /^[xyz]$/ && $self->{_tree}[-1] eq 'coordinates') {
        $self->{_coordinate} = $data->{LocalName};
    } elsif ($data->{LocalName} eq 'volume') {
        $self->{_volume} = $self->{_object}->add_volume(
            material_id => $self->_get_attribute($data, 'materialid') // undef,
            mesh        => Slic3r::TriangleMesh->new,
        );
        $self->{_volume_facets} = [];
    } elsif ($data->{LocalName} eq 'triangle') {
        $self->{_triangle} = ["", "", ""];
    } elsif ($self->{_triangle} && $data->{LocalName} =~ /^v([123])$/ && $self->{_tree}[-1] eq 'triangle') {
        $self->{_vertex_idx} = $1-1;
    } elsif ($data->{LocalName} eq 'material') {
        my $material_id = $self->_get_attribute($data, 'id') // '_';
        $self->{_material} = $self->{_model}->set_material($material_id);
    } elsif ($data->{LocalName} eq 'metadata') {
        $self->{_metadata_type} = $self->_get_attribute($data, 'type');
        $self->{_metadata_value} = '';
    } elsif ($data->{LocalName} eq 'constellation') {
        $self->{_constellation} = 1; # we merge all constellations as we don't support more than one
    } elsif ($data->{LocalName} eq 'instance' && $self->{_constellation}) {
        my $object_id = $self->_get_attribute($data, 'objectid');
        $self->{_instances}{$object_id} ||= [];
        push @{ $self->{_instances}{$object_id} }, $self->{_instance} = {};
    } elsif ($data->{LocalName} =~ /^(?:deltax|deltay|rz)$/ && $self->{_instance}) {
        $self->{_instance_property} = $data->{LocalName};
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
    } elsif ($self->{_metadata_type}) {
        $self->{_metadata_value} .= $data->{Data};
    } elsif ($self->{_instance_property}) {
        $self->{_instance}{ $self->{_instance_property} } .= $data->{Data};
    }
}

sub end_element {
    my $self = shift;
    my $data = shift;
    
    pop @{$self->{_tree}};
    
    if ($data->{LocalName} eq 'object') {
        $self->{_object} = undef;
        $self->{_object_vertices} = undef;
    } elsif ($data->{LocalName} eq 'vertex') {
        push @{$self->{_object_vertices}}, $self->{_vertex};
        $self->{_vertex} = undef;
    } elsif ($self->{_coordinate} && $data->{LocalName} =~ /^[xyz]$/) {
        $self->{_coordinate} = undef;
    } elsif ($data->{LocalName} eq 'volume') {
        $self->{_volume}->mesh->ReadFromPerl($self->{_object_vertices}, $self->{_volume_facets});
        $self->{_volume}->mesh->repair;
        $self->{_volume} = undef;
        $self->{_volume_facets} = undef;
    } elsif ($data->{LocalName} eq 'triangle') {
        push @{$self->{_volume_facets}}, $self->{_triangle};
        $self->{_triangle} = undef;
    } elsif (defined $self->{_vertex_idx} && $data->{LocalName} =~ /^v[123]$/) {
        $self->{_vertex_idx} = undef;
    } elsif ($data->{LocalName} eq 'material') {
        $self->{_material} = undef;
    } elsif ($data->{LocalName} eq 'metadata') {
        my $value = $self->{_metadata_value};
        if ($self->{_metadata_type} =~ /^slic3r\.(.+)/) {
            my $opt_key = $1;
            if (exists $Slic3r::Config::Options->{$opt_key}) {
                my $config;
                if ($self->{_material}) {
                    $config = $self->{_material}->config;
                } elsif ($self->{_volume}) {
                    $config = $self->{_volume}->config;
                } elsif ($self->{_object}) {
                    $config = $self->{_object}->config;
                }
                
                $config->set_deserialize($opt_key, $value) if defined $config;
            } elsif ($opt_key eq 'modifier' && $self->{_volume}) {
                $self->{_volume}->set_modifier($value);
            }
        } elsif ($self->{_metadata_type} eq 'name' && $self->{_volume}) {
            $self->{_volume}->set_name($value);
        } elsif ($self->{_metadata_type} eq 'name' && $self->{_object}) {
            $self->{_object}->set_name($value);
        } elsif ($self->{_material}) {
            $self->{_material}->set_attribute($self->{_metadata_type}, $value);
        }
        $self->{_metadata_type} = undef;
        $self->{_metadata_value} = undef;
    } elsif ($data->{LocalName} eq 'constellation') {
        $self->{_constellation} = undef;
    } elsif ($data->{LocalName} eq 'instance') {
        $self->{_instance} = undef;
    } elsif ($data->{LocalName} =~ /^(?:deltax|deltay|rz)$/ && $self->{_instance}) {
        $self->{_instance_property} = undef;
    }
}

sub end_document {
    my $self = shift;
    
    foreach my $object_id (keys %{ $self->{_instances} }) {
        my $new_object_id = $self->{_objects_map}{$object_id};
        if (!defined $new_object_id) {
            warn "Undefined object $object_id referenced in constellation\n";
            next;
        }
        
        foreach my $instance (@{ $self->{_instances}{$object_id} }) {
            $self->{_model}->objects->[$new_object_id]->add_instance(
                rotation => $instance->{rz} || 0,
                offset   => Slic3r::Pointf->new($instance->{deltax} || 0, $instance->{deltay} || 0),
            );
        }
    }
}

sub _get_attribute {
    my $self = shift;
    my ($data, $name) = @_;
    
    return +(map $_->{Value}, grep $_->{Name} eq $name, values %{$data->{Attributes}})[0];
}

1;
