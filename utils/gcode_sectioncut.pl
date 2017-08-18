#!/usr/bin/perl
# This script generates section cuts from a given G-Code file

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Getopt::Long qw(:config no_auto_abbrev);
use IO::All;
use List::Util qw(max);
use Slic3r;
use Slic3r::Geometry qw(X Y);
use Slic3r::Geometry::Clipper qw(JT_SQUARE);
use Slic3r::Test;
use SVG;

my %opt = (
    layer_height    => 0.2,
    extrusion_width => 0.5,
    scale           => 30,
);
{
    my %options = (
        'help'                  => sub { usage() },
        'layer-height|h=f'      => \$opt{layer_height},
        'extrusion-width|w=f'   => \$opt{extrusion_width},
        'scale|s=i'             => \$opt{scale},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    my $input_file = $ARGV[0];
    my $output_file = $input_file;
    $output_file =~ s/\.(?:gcode|gco|ngc|g)$/.svg/;
    
    # read paths
    my %paths = ();    # z => [ path, path ... ]
    Slic3r::GCode::Reader->new->parse(io($input_file)->all, sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && $info->{extruding}) {
            $paths{ $self->Z } ||= [];
            push @{ $paths{ $self->Z } }, Slic3r::Line->new(
                [ $self->X, $self->Y ],
                [ $info->{new_X}, $info->{new_Y} ],
            );
        }
    });
    
    # calculate print extents
    my $bounding_box = Slic3r::Geometry::BoundingBox->new_from_points([ map @$_, map @$_, values %paths ]);
    
    # calculate section line
    my $section_y = $bounding_box->center->[Y];
    my $section_line = [
        [ $bounding_box->x_min, $section_y ],
        [ $bounding_box->x_max, $section_y ],
    ];
    
    # initialize output
    my $max_z = max(keys %paths);
    my $svg = SVG->new(
        width  => $opt{scale} * $bounding_box->size->[X],
        height => $opt{scale} * $max_z,
    );
    
    # put everything into a group
    my $g = $svg->group(style => {
        'stroke-width'  => 1,
        'stroke'        => '#444444',
        'fill'          => 'grey',
    });
    
    # draw paths
    foreach my $z (sort keys %paths) {
        foreach my $line (@{ $paths{$z} }) {
            my @intersections = @{intersection_pl(
                [ $section_line ],
                [ _grow($line, $opt{extrusion_width}/2) ],
            )};
            
            $g->rectangle(
                'x'         => $opt{scale} * ($_->[0][X] - $bounding_box->x_min),
                'y'         => $opt{scale} * ($max_z - $z),
                'width'     => $opt{scale} * abs($_->[1][X] - $_->[0][X]),
                'height'    => $opt{scale} * $opt{layer_height},
                'rx'        => $opt{scale} * $opt{layer_height} * 0.35,
                'ry'        => $opt{scale} * $opt{layer_height} * 0.35,
            ) for @intersections;
        }
    }
    
    # write output
    Slic3r::open(\my $fh, '>', $output_file);
    print $fh $svg->xmlify;
    close $fh;
    printf "Section cut SVG written to %s\n", $output_file;
}

# replace built-in Line->grow method which relies on int_offset()
sub _grow {
    my ($line, $distance) = @_;
    
    my $polygon = [ @$line, CORE::reverse @$line[1..($#$line-1)] ];
    return @{Math::Clipper::offset([$polygon], $distance, 100000, JT_SQUARE, 2)};
}

sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: gcode_sectioncut.pl [ OPTIONS ] file.gcode

    --help              Output this usage screen and exit
    --layer-height, -h  Use the specified layer height
    --extrusion-width, -w  Use the specified extrusion width
    --scale             Factor for converting G-code units to SVG units
    
EOF
    exit ($exit_code || 0);
}

__END__
