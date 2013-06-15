package Slic3r::Test;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(_eq);

use IO::Scalar;
use List::Util qw(first);
use Slic3r::Geometry qw(epsilon X Y Z);

my %cuboids = (
    '20mm_cube' => [20,20,20],
    '2x20x10'   => [2, 20,10],
);

sub model {
    my ($model_name, %params) = @_;
    
    my ($vertices, $facets);
    if ($cuboids{$model_name}) {
        my ($x, $y, $z) = @{ $cuboids{$model_name} };
        $vertices = [
            [$x,$y,0], [$x,0,0], [0,0,0], [0,$y,0], [$x,$y,$z], [0,$y,$z], [0,0,$z], [$x,0,$z],
        ];
        $facets = [
            [0,1,2], [0,2,3], [4,5,6], [4,6,7], [0,4,7], [0,7,1], [1,7,6], [1,6,2], [2,6,5], [2,5,3], [4,0,3], [4,3,5],
        ],
    }
    
    my $model = Slic3r::Model->new;
    my $object = $model->add_object(vertices => $vertices);
    $object->add_volume(facets => $facets);
    $object->add_instance(
        offset      => [0,0],
        rotation    => $params{rotation},
    );
    return $model;
}

sub init_print {
    my ($model_name, %params) = @_;
    
    my $config = Slic3r::Config->new_from_defaults;
    $config->apply($params{config}) if $params{config};
    $config->set('gcode_comments', 1) if $ENV{SLIC3R_TESTS_GCODE};
    
    my $print = Slic3r::Print->new(config => $config);
    
    $model_name = [$model_name] if ref($model_name) ne 'ARRAY';
    $print->add_model(model($_, %params)) for @$model_name;
    $print->validate;
    
    return $print;
}

sub gcode {
    my ($print) = @_;
    
    my $fh = IO::Scalar->new(\my $gcode);
    $print->export_gcode(output_fh => $fh, quiet => 1);
    $fh->close;
    
    return $gcode;
}

sub _eq {
    my ($a, $b) = @_;
    return abs($a - $b) < epsilon;
}

sub add_facet {
    my ($facet, $vertices, $facets) = @_;
    
    push @$facets, [];
    for my $i (0..2) {
        my $v = first { $vertices->[$_][X] == $facet->[$i][X] && $vertices->[$_][Y] == $facet->[$i][Y] && $vertices->[$_][Z] == $facet->[$i][Z] } 0..$#$vertices;
        if (!defined $v) {
            push @$vertices, [ @{$facet->[$i]}[X,Y,Z] ];
            $v = $#$vertices;
        }
        $facets->[-1][$i] = $v;
    }
}

1;
