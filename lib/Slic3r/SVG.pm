package Slic3r::SVG;
use strict;
use warnings;

use SVG;

use constant X => 0;
use constant Y => 1;

sub factor {
    return $Slic3r::resolution * 10;
}

sub svg {
    my ($print) = @_;
    
    return SVG->new(width => $print->max_length * factor(), height => $print->max_length * factor());
}

sub output_polygons {
    my ($print, $filename, $polygons) = @_;
    
    my $svg = svg($print);
    my $g = $svg->group(
        style => {
            'stroke-width' => 2,
        },
    );
    foreach my $polygon (@$polygons) {
        my $path = $svg->get_path(
            'x' => [ map($_->[X] * factor(), @$polygon) ],
            'y' => [ map($_->[Y] * factor(), @$polygon) ],
            -type => 'polygon',
        );
        $g->polygon(
            %$path,
        );
    }
    
    write_svg($svg, $filename);
}

sub output_lines {
    my ($print, $filename, $lines) = @_;
    
    my $svg = svg($print);
    my $g = $svg->group(
        style => {
            'stroke-width' => 2,
        },
    );
    
    my $color = 'red';
    my $draw_line = sub {
        my ($line) = @_;
        $g->line(
            x1 => $line->[0][X] * factor(),
            y1 => $line->[0][Y] * factor(),
            x2 => $line->[1][X] * factor(),
            y2 => $line->[1][Y] * factor(),
            style => {
                'stroke' => $color,
            },
        );
    };
    
    my $last = pop @$lines;
    foreach my $line (@$lines) {
        $draw_line->($line);
    }
    $color = 'black';
    $draw_line->($last);
    
    write_svg($svg, $filename);
}

sub write_svg {
    my ($svg, $filename) = @_;
    
    open my $fh, '>', $filename;
    print $fh $svg->xmlify;
    close $fh;
    printf "SVG written to %s\n", $filename;
}

1;
