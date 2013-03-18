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
    my ($model_name) = @_;
    
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
    $model->add_object(vertices => $vertices)->add_volume(facets => $facets);
    return $model;
}

sub init_print {
    my ($model_name, %params) = @_;
    
    my $config = Slic3r::Config->new_from_defaults;
    $config->apply($params{config}) if $params{config};
    $config->set('gcode_comments', 1) if $ENV{SLIC3R_TESTS_GCODE};
    
    my $print = Slic3r::Print->new(config => $config);
    $print->add_model(model($model_name));
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

package Slic3r::Test::GCodeReader;
use Moo;

has 'gcode' => (is => 'ro', required => 1);
has 'X' => (is => 'rw', default => sub {0});
has 'Y' => (is => 'rw', default => sub {0});
has 'Z' => (is => 'rw', default => sub {0});
has 'E' => (is => 'rw', default => sub {0});
has 'F' => (is => 'rw', default => sub {0});

our $Verbose = 0;
my @AXES = qw(X Y Z E);

sub parse {
    my $self = shift;
    my ($cb) = @_;
    
    foreach my $line (split /\n/, $self->gcode) {
        print "$line\n" if $Verbose || $ENV{SLIC3R_TESTS_GCODE};
        $line =~ s/\s*;(.*)//; # strip comment
        next if $line eq '';
        my $comment = $1;
        
        # parse command
        my ($command, @args) = split /\s+/, $line;
        my %args = map { /([A-Z])(.*)/; ($1 => $2) } @args;
        my %info = ();
        
        # check retraction
        if ($command =~ /^G[01]$/) {
            if (!exists $args{E}) {
                $info{travel} = 1;
            }
            foreach my $axis (@AXES) {
                if (!exists $args{$axis}) {
                    $info{"dist_$axis"} = 0;
                    next;
                }
                $info{"dist_$axis"} = $args{$axis} - $self->$axis;
            }
            $info{dist_XY} = Slic3r::Line->new([0,0], [@info{qw(dist_X dist_Y)}])->length;
            if (exists $args{E}) {
                if ($info{dist_E} > 0) {
                    $info{extruding} = 1;
                } elsif ($info{dist_E} < 0) {
                    $info{retracting} = 1
                }
            }
        }
        
        # run callback
        $cb->($self, $command, \%args, \%info);
        
        # update coordinates
        if ($command =~ /^(?:G[01]|G92)$/) {
            for (@AXES, 'F') {
                $self->$_($args{$_}) if exists $args{$_};
            }
        }
        
        # TODO: update temperatures
    }
}

1;
