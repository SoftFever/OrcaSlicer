package Slic3r::Test::SectionCut;
use Moo;

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
    
    my @bb = $self->print->bounding_box;
    my $y = ($bb[Y2]-$bb[Y1]) * $self->y_percent;
    return [ [ $bb[X1], $y ], [ $bb[X2], $y ] ]
}

sub export_svg {
    my $self = shift;
    my ($filename) = @_;
    
    my $print_size = $self->print->size;
    $self->height(unscale($print_size->[X]));
    my $svg = SVG->new(
        width  => $self->scale * unscale($print_size->[X]),
        height => $self->scale * $self->height,
    );
    
    my $group = sub {
        my %p = @_;
        my $g = $svg->group(style => $p{style});
        $g->rectangle(%$_) for $self->_get_rectangles($p{filter});
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
        filter => sub { $_[0]->support_fills, $_[0]->support_contact_fills },
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

sub _get_rectangles {
    my $self = shift;
    my ($filter) = @_;
    
    my @rectangles = ();
    
    foreach my $object (@{$self->print->objects}) {
        foreach my $copy (@{$object->copies}) {
            foreach my $layer (@{$object->layers}) {
                # get all ExtrusionPath objects
                my @paths = 
                    map { $_->polyline->translate(@$copy); $_ }
                    map { $_->isa('Slic3r::ExtrusionLoop') ? $_->split_at_first_point : $_ }
                    map { ref($_) =~ /::Packed$/ ? $_->unpack : $_ }
                    map { $_->isa('Slic3r::ExtrusionPath::Collection') ? @{$_->paths} : $_ }
                    grep defined $_,
                    $filter->($layer);
                
                foreach my $path (@paths) {
                    foreach my $line ($path->lines) {
                        my @intersections = @{ Boost::Geometry::Utils::polygon_multi_linestring_intersection(
                            Slic3r::ExPolygon->new($line->grow(Slic3r::Geometry::scale $path->flow_spacing/2)),
                            [ $self->line ],
                        ) };
                        
                        push @rectangles, map {
                            die "Intersection has more than two points!\n" if @$_ > 2;
                            my $height = $path->height // $layer->height;
                            {
                                'x'         => $self->scale * unscale $_->[A][X],
                                'y'         => $self->scale * $self->_y(unscale($layer->print_z)),
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
    
    return @rectangles;
}

sub _y {
    my $self = shift;
    my ($y) = @_;
    
    return $self->height - $y;
}

1;
