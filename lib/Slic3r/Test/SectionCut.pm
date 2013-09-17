package Slic3r::Test::SectionCut;
use Moo;

use List::Util qw(first max);
use Slic3r::Geometry qw(X Y A B X1 Y1 X2 Y2 unscale);
use Slic3r::Geometry::Clipper qw(union_ex);
use SVG;

has 'scale' => (is => 'ro', default => sub {30});
has 'print' => (is => 'ro', required => 1);
has 'y_percent' => (is => 'ro', default => sub {0.5});
has 'line'  => (is => 'lazy');
has 'height' => (is => 'rw');

sub _build_line {
    my $self = shift;
    
    my $bb = $self->print->bounding_box;
    my $y = $bb->size->[Y] * $self->y_percent;
    return [ [ $bb->x_min, $y ], [ $bb->x_max, $y ] ]
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
        foreach my $copy (@{$object->copies}) {
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                # get all ExtrusionPath objects
                my @paths = 
                    map { $_->polyline->translate(@$copy); $_ }
                    map { $_->isa('Slic3r::ExtrusionLoop') ? $_->split_at_first_point : $_ }
                    map { $_->isa('Slic3r::ExtrusionPath::Collection') ? @$_ : $_ }
                    grep defined $_,
                    $filter->($layer);
                
                foreach my $path (@paths) {
                    foreach my $line (@{$path->lines}) {
                        my @intersections = @{ Boost::Geometry::Utils::polygon_multi_linestring_intersection(
                            Slic3r::ExPolygon->new($line->grow(Slic3r::Geometry::scale $path->flow_spacing/2))->pp,
                            [ $self->line ],
                        ) };
                        die "Intersection has more than two points!\n" if first { @$_ > 2 } @intersections;
                        
                        if ($path->is_bridge) {
                            foreach my $line (@intersections) {
                                my $radius = $path->flow_spacing / 2;
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
    
    return $self->height - $y;
}

1;
