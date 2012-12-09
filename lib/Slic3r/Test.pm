package Slic3r::Test;
use strict;
use warnings;

use IO::Scalar;
use Slic3r::Geometry qw(epsilon);

my %cuboids = (
    '20mm_cube' => [20,20,20],
    '2x20x10'   => [2, 20,10],
);

sub init_print {
    my ($model_name, %params) = @_;
    
    my $model = Slic3r::Model->new;
    {
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
        $model->add_object(vertices => $vertices)->add_volume(facets => $facets);
    }
    
    my $config = Slic3r::Config->new_from_defaults;
    $config->apply($params{config}) if $params{config};
    
    my $print = Slic3r::Print->new(config => $config);
    $print->add_model($model);
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

sub compare {
    my ($a, $b) = @_;
    return abs($a - $b) < epsilon;
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
                ($info{dist_E} > 0)
                    ? ($info{extruding} = 1)
                    : ($info{retracting} = 1);
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
