package Slic3r::SVG;
use strict;
use warnings;

use SVG;

use constant X => 0;
use constant Y => 1;

our $filltype = 'evenodd';

sub factor {
    return &Slic3r::SCALING_FACTOR * 10;
}

sub svg {
    my $svg = SVG->new(width => 200 * 10, height => 200 * 10);
    my $marker_end = $svg->marker(
        id => "endArrow",
        viewBox => "0 0 10 10",
        refX => "1",
        refY => "5",
        markerUnits => "strokeWidth",
        orient => "auto",
        markerWidth => "10",
        markerHeight => "8",
    );
    $marker_end->polyline(
        points => "0,0 10,5 0,10 1,5",
        fill => "darkblue",
    );
    
    return $svg;
}

sub output {
    my ($filename, @things) = @_;
    
    my $svg = svg();
    my $arrows = 1;
    
    while (my $type = shift @things) {
        my $value = shift @things;
        
        if ($type eq 'no_arrows') {
            $arrows = 0;
        } elsif ($type =~ /^(?:(.+?)_)?expolygons$/) {
            my $colour = $1;
            $value = [ map $_->pp, @$value ];
            
            my $g = $svg->group(
                style => {
                    'stroke-width' => 0,
                    'stroke' => $colour || 'black',
                    'fill' => ($type !~ /polygons/ ? 'none' : ($colour || 'grey')),
                    'fill-type' => $filltype,
                },
            );
            foreach my $expolygon (@$value) {
                my $points = join ' ', map "M $_ z", map join(" ", reverse map $_->[0]*factor() . " " . $_->[1]*factor(), @$_), @$expolygon;
                $g->path(
                    d => $points,
                );
            }
        } elsif ($type =~ /^(?:(.+?)_)?(polygon|polyline)s$/) {
            my ($colour, $method) = ($1, $2);
            $value = [ map $_->pp, @$value ];
            
            my $g = $svg->group(
                style => {
                    'stroke-width' => ($method eq 'polyline') ? 1 : 0,
                    'stroke' => $colour || 'black',
                    'fill' => ($type !~ /polygons/ ? 'none' : ($colour || 'grey')),
                },
            );
            foreach my $polygon (@$value) {
                my $path = $svg->get_path(
                    'x' => [ map($_->[X] * factor(), @$polygon) ],
                    'y' => [ map($_->[Y] * factor(), @$polygon) ],
                    -type => 'polygon',
                );
                $g->$method(
                    %$path,
                    'marker-end' => !$arrows ? "" : "url(#endArrow)",
                );
            }
        } elsif ($type =~ /^(?:(.+?)_)?points$/) {
            my $colour = $1 // 'black';
            my $r = $colour eq 'black' ? 1 : 3;
            $value = [ map $_->pp, @$value ];
            
            my $g = $svg->group(
                style => {
                    'stroke-width' => 2,
                    'stroke' => $colour,
                    'fill' => $colour,
                },
            );
            foreach my $point (@$value) {
                $g->circle(
                    cx      => $point->[X] * factor(),
                    cy      => $point->[Y] * factor(),
                    r       => $r,
                );
            }
        } elsif ($type =~ /^(?:(.+?)_)?lines$/) {
            my $colour = $1;
            $value = [ map $_->pp, @$value ];
            
            my $g = $svg->group(
                style => {
                    'stroke-width' => 2,
                },
            );
            foreach my $line (@$value) {
                $g->line(
                    x1 => $line->[0][X] * factor(),
                    y1 => $line->[0][Y] * factor(),
                    x2 => $line->[1][X] * factor(),
                    y2 => $line->[1][Y] * factor(),
                    style => {
                        'stroke' => $colour || 'black',
                    },
                    'marker-end' => !$arrows ? "" : "url(#endArrow)",
                );
            }
        }
    }
    
    write_svg($svg, $filename);
}

sub write_svg {
    my ($svg, $filename) = @_;
    
    Slic3r::open(\my $fh, '>', $filename);
    print $fh $svg->xmlify;
    close $fh;
    printf "SVG written to %s\n", $filename;
}

1;
