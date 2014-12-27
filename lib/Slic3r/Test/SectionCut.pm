package Slic3r::Test::SectionCut;
use Moo;

use List::Util qw(first min max);
use Slic3r::Geometry qw(unscale);
use Slic3r::Geometry::Clipper qw(intersection_pl);
use SVG;
use Slic3r::SVG;

has 'print'         => (is => 'ro', required => 1);
has 'scale'         => (is => 'ro', default => sub { 30 });
has 'y_percent'     => (is => 'ro', default => sub { 0.5 });  # Y coord of section line expressed as factor
has 'line'          => (is => 'rw');
has '_height'       => (is => 'rw');
has '_svg'          => (is => 'rw');
has '_svg_style'    => (is => 'rw', default => sub { {} });

sub BUILD {
    my $self = shift;
    
    # calculate the Y coordinate of the section line
    my $bb = $self->print->bounding_box;
    my $y = ($bb->y_min + $bb->y_max) * $self->y_percent;
    
    # store our section line
    $self->line(Slic3r::Line->new([ $bb->x_min, $y ], [ $bb->x_max, $y ]));
}

sub export_svg {
    my $self = shift;
    my ($filename) = @_;
    
    # get bounding box of print and its height
    # (Print should return a BoundingBox3 object instead)
    my $bb = $self->print->bounding_box;
    my $print_size = $bb->size;
    $self->_height(max(map $_->print_z, map @{$_->layers}, @{$self->print->objects}));
    
    # initialize the SVG canvas
    $self->_svg(my $svg = SVG->new(
        width  => $self->scale * unscale($print_size->x),
        height => $self->scale * $self->_height,
    ));
    
    # set default styles
    $self->_svg_style->{'stroke-width'}   = 1;
    $self->_svg_style->{'fill-opacity'}   = 0.5;
    $self->_svg_style->{'stroke-opacity'} = 0.2;
    
    # plot perimeters
    $self->_svg_style->{'stroke'}   = '#EE0000';
    $self->_svg_style->{'fill'}     = '#FF0000';
    $self->_plot_group(sub { map @{$_->perimeters}, @{$_[0]->regions} });
    
    # plot infill
    $self->_svg_style->{'stroke'}   = '#444444';
    $self->_svg_style->{'fill'}     = '#454545';
    $self->_plot_group(sub { map @{$_->fills}, @{$_[0]->regions} });
    
    # plot support material
    $self->_svg_style->{'stroke'}   = '#12EF00';
    $self->_svg_style->{'fill'}     = '#22FF00';
    $self->_plot_group(sub { $_[0]->isa('Slic3r::Layer::Support') ? ($_[0]->support_fills, $_[0]->support_interface_fills) : () });
    
    Slic3r::open(\my $fh, '>', $filename);
    print $fh $svg->xmlify;
    close $fh;
    printf "Section cut SVG written to %s\n", $filename;
}

sub _plot_group {
    my $self = shift;
    my ($filter) = @_;
    
    my $bb = $self->print->bounding_box;
    my $g = $self->_svg->group(style => { %{$self->_svg_style} });
    
    foreach my $object (@{$self->print->objects}) {
        foreach my $copy (@{$object->_shifted_copies}) {
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                # get all ExtrusionPath objects
                my @paths = map $_->clone,
                    map { ($_->isa('Slic3r::ExtrusionLoop') || $_->isa('Slic3r::ExtrusionPath::Collection')) ? @$_ : $_ }
                    grep defined $_,
                    $filter->($layer);
                
                # move paths to location of copy
                $_->polyline->translate(@$copy) for @paths;
                
                if (0) {
                    # export plan with section line and exit
                    require "Slic3r/SVG.pm";
                    Slic3r::SVG::output(
                        "line.svg",
                        no_arrows       => 1,
                        lines           => [ $self->line ],
                        red_polylines   => [ map $_->polyline, @paths ],
                    );
                    exit;
                }
                
                foreach my $path (@paths) {
                    foreach my $line (@{$path->lines}) {
                        my @intersections = @{intersection_pl(
                            [ $self->line->as_polyline ],
                            $line->grow(Slic3r::Geometry::scale $path->width/2),
                        )};
                        
                        die "Intersection has more than two points!\n"
                            if defined first { @$_ > 2 } @intersections;
                        
                        # turn intersections to lines
                        my @lines = map Slic3r::Line->new(@$_), @intersections;
                        
                        # align intersections to canvas
                        $_->translate(-$bb->x_min, 0) for @lines;
                        
                        # we want lines oriented from left to right in order to draw
                        # rectangles correctly
                        foreach my $line (@lines) {
                            $line->reverse if $line->a->x > $line->b->x;
                        }
                        
                        if ($path->is_bridge) {
                            foreach my $line (@lines) {
                                my $radius = $path->width / 2;
                                my $width = unscale abs($line->b->x - $line->a->x);
                                if ((10 * $radius) < $width) {
                                    # we're cutting the path in the longitudinal direction, so we've got a rectangle
                                    $g->rectangle(
                                        'x'         => $self->scale * unscale($line->a->x),
                                        'y'         => $self->scale * $self->_y($layer->print_z),
                                        'width'     => $self->scale * $width,
                                        'height'    => $self->scale * $radius * 2,
                                        'rx'        => $self->scale * $radius * 0.35,
                                        'ry'        => $self->scale * $radius * 0.35,
                                    );
                                } else {
                                    $g->circle(
                                        'cx'        => $self->scale * (unscale($line->a->x) + $radius),
                                        'cy'        => $self->scale * $self->_y($layer->print_z - $radius),
                                        'r'         => $self->scale * $radius,
                                    );
                                }
                            }
                        } else {
                            foreach my $line (@lines) {
                                my $height = $path->height;
                                $height = $layer->height if $height == -1;
                                $g->rectangle(
                                    'x'         => $self->scale * unscale($line->a->x),
                                    'y'         => $self->scale * $self->_y($layer->print_z),
                                    'width'     => $self->scale * unscale($line->b->x - $line->a->x),
                                    'height'    => $self->scale * $height,
                                    'rx'        => $self->scale * $height * 0.5,
                                    'ry'        => $self->scale * $height * 0.5,
                                );
                            }
                        }
                    }
                }
            }
        }
    }
}

sub _y {
    my $self = shift;
    my ($y) = @_;
    
    return $self->_height - $y;
}

1;
