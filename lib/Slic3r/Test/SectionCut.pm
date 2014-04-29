package Slic3r::Test::SectionCut;
use Moo;

use List::Util qw(first max);
use Slic3r::Geometry qw(X Y A B X1 Y1 X2 Y2 unscale);
use Slic3r::Geometry::Clipper qw(union_ex intersection_pl);
use SVG;
                use Slic3r::SVG;

has 'scale' => (is => 'ro', default => sub {30});
has 'print' => (is => 'ro', required => 1);
has 'y_percent' => (is => 'ro', default => sub {0.5});
has 'line'  => (is => 'rw');
has 'height' => (is => 'rw');

sub BUILD {
    my $self = shift;
    
    my $bb = $self->print->bounding_box;
    my $y = ($bb->y_min + $bb->y_max) * $self->y_percent;
    my $line = Slic3r::Line->new([ $bb->x_min, $y ], [ $bb->x_max, $y ]);
    $self->line($line);
}

sub export_svg {
    my $self = shift;
    my ($filename) = @_;
    
    my $print_size = $self->print->size;
    $self->height(max(map $_->print_z, map @{$_->layers}, @{$self->print->objects}));
    my $svg = SVG->new(
        width  => $self->scale * unscale($print_size->[X]),
        height => $self->scale * $self->height,
    );
    
    my $group = sub {
        my %p = @_;
        my $g = $svg->group(style => $p{style});
        my $items = $self->_plot($p{filter});
        $g->rectangle(%$_) for @{ $items->{rectangles} };
        $g->circle(%$_)    for @{ $items->{circles} };
    };
    
    $group->(
        filter => sub { map @{$_->perimeters}, @{$_[0]->regions} },
        style  => {
            'stroke-width'  => 1,
            'stroke'        => 'grey',
            'fill'          => 'red',
        },
    );
    
    $group->(
        filter => sub { map @{$_->fills}, @{$_[0]->regions} },
        style  => {
            'stroke-width'  => 1,
            'stroke'        => '#444444',
            'fill'          => 'grey',
        },
    );
    
    $group->(
        filter => sub { $_[0]->isa('Slic3r::Layer::Support') ? ($_[0]->support_fills, $_[0]->support_interface_fills) : () },
        style  => {
            'stroke-width'  => 1,
            'stroke'        => '#444444',
            'fill'          => '#22FF00',
        },
    );
    
    Slic3r::open(\my $fh, '>', $filename);
    print $fh $svg->xmlify;
    close $fh;
    printf "Section cut SVG written to %s\n", $filename;
}

sub _plot {
    my $self = shift;
    my ($filter) = @_;
    
    my (@rectangles, @circles) = ();
    
    foreach my $object (@{$self->print->objects}) {
        foreach my $copy (@{$object->_shifted_copies}) {
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                # get all ExtrusionPath objects
                my @paths = 
                    map { $_->isa('Slic3r::ExtrusionLoop') ? $_->split_at_first_point : $_->clone }
                    map { $_->isa('Slic3r::ExtrusionPath::Collection') ? @$_ : $_ }
                    grep defined $_,
                    $filter->($layer);
                
                $_->polyline->translate(@$copy) for @paths;
                
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(
                    "line.svg",
                    no_arrows => 1,
                    #polygon => $line->grow(Slic3r::Geometry::scale $path->width/2),
                    polygons => [ $object->bounding_box->polygon ],
                    lines => [ $self->line ],
                    red_polylines => [ map $_->polyline, @paths ],
                );
                exit;
                
                foreach my $path (@paths) {
                    foreach my $line (@{$path->lines}) {
                        my @intersections = @{intersection_pl(
                            [ $self->line->as_polyline ],
                            $line->grow(Slic3r::Geometry::scale $path->width/2),
                        )};
                        
                        die "Intersection has more than two points!\n" if first { @$_ > 2 } @intersections;
                        
                        if ($path->is_bridge) {
                            foreach my $line (@intersections) {
                                my $radius = $path->width / 2;
                                my $width = unscale abs($line->[B][X] - $line->[A][X]);
                                if ((10 * Slic3r::Geometry::scale $radius) < $width) {
                                    # we're cutting the path in the longitudinal direction, so we've got a rectangle
                                    push @rectangles, {
                                        'x'         => $self->scale * unscale $line->[A][X],
                                        'y'         => $self->scale * $self->_y($layer->print_z),
                                        'width'     => $self->scale * $width,
                                        'height'    => $self->scale * $radius * 2,
                                        'rx'        => $self->scale * $radius * 0.35,
                                        'ry'        => $self->scale * $radius * 0.35,
                                    };
                                } else {
                                    push @circles, {
                                        'cx'        => $self->scale * (unscale($line->[A][X]) + $radius),
                                        'cy'        => $self->scale * $self->_y($layer->print_z - $radius),
                                        'r'         => $self->scale * $radius,
                                    };
                                }
                            }
                        } else {
                            push @rectangles, map {
                                my $height = $path->height;
                                $height = $layer->height if $height == -1;
                                {
                                    'x'         => $self->scale * unscale $_->[A][X],
                                    'y'         => $self->scale * $self->_y($layer->print_z),
                                    'width'     => $self->scale * unscale(abs($_->[B][X] - $_->[A][X])),
                                    'height'    => $self->scale * $height,
                                    'rx'        => $self->scale * $height * 0.35,
                                    'ry'        => $self->scale * $height * 0.35,
                                };
                            } @intersections;
                        }
                    }
                }
            }
        }
    }
    
    return {
        rectangles  => \@rectangles,
        circles     => \@circles,
    };
}

sub _y {
    my $self = shift;
    my ($y) = @_;
    return $y;
    return $self->height - $y;
}

1;
