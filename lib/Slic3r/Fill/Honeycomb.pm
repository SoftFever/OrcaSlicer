package Slic3r::Fill::Honeycomb;
use Moo;

extends 'Slic3r::Fill::Base';

has 'cache' => (is => 'rw', default => sub {{}});

use Slic3r::Geometry qw(PI X1 Y1 X2 Y2 X Y scale);
use Slic3r::Geometry::Clipper qw(intersection_ex);

sub angles () { [0, PI/3, PI/3*2] }

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $expolygon = $surface->expolygon->clone;
    my $rotate_vector = $self->infill_direction($surface);
    
    # infill math
    my $min_spacing = scale $params{flow_spacing};
    my $distance = $min_spacing / $params{density};
    my $overlap_distance = scale($self->layer ? $self->layer->flow->width : $Slic3r::flow->width) * 0.4;
    
    my $cache_id = sprintf "d%s_s%s_a%s",
        $params{density}, $params{flow_spacing}, $rotate_vector->[0][0];
    if (!$self->cache->{$cache_id}) {
        
        # hexagons math
        my $hex_side = $distance / (sqrt(3)/2);
        my $hex_width = $distance * 2;  # $hex_width == $hex_side * sqrt(3);
        my $hex_height = $hex_side * 2;
        my $pattern_height = $hex_height + $hex_side;
        my $y_short = $distance * sqrt(3)/3;
        my $x_offset = $min_spacing / 2;
        my $y_offset = $x_offset * sqrt(3)/3;
        my $hex_center = Slic3r::Point->new($hex_width/2, $hex_side);
        
        # adjust actual bounding box to the nearest multiple of our hex pattern
        # and align it so that it matches across layers
        my $print_bounding_box = [ $self->print->bounding_box ];
        my $bounding_box = [ 0, 0, $print_bounding_box->[X2], $print_bounding_box->[Y2] ];
        {
            my $bb_polygon = Slic3r::Polygon->new([
                [ $bounding_box->[X1], $bounding_box->[Y1] ],
                [ $bounding_box->[X2], $bounding_box->[Y1] ],
                [ $bounding_box->[X2], $bounding_box->[Y2] ],
                [ $bounding_box->[X1], $bounding_box->[Y2] ],
            ]);
            $bb_polygon->rotate($rotate_vector->[0][0], $hex_center);
            $bounding_box = [ Slic3r::Geometry::bounding_box($bb_polygon) ];
            # $bounding_box->[X1] and [Y1] represent the displacement between new bounding box offset and old one
            $bounding_box->[X1] -= $bounding_box->[X1] % $hex_width;
            $bounding_box->[Y1] -= $bounding_box->[Y1] % $pattern_height;
        }
        
        my @polygons = ();
        my $x = $bounding_box->[X1];
        while ($x <= $bounding_box->[X2]) {
            my $p = [];
            
            my @x = ($x + $x_offset, $x + $distance - $x_offset);
            for (1..2) {
                @$p = reverse @$p; # turn first half upside down
                my @p = ();
                for (my $y = $bounding_box->[Y1]; $y <= $bounding_box->[Y2]; $y += $y_short + $hex_side + $y_short + $hex_side) {
                    push @$p,
                        [ $x[1], $y + $y_offset ],
                        [ $x[0], $y + $y_short - $y_offset ],
                        [ $x[0], $y + $y_short + $hex_side + $y_offset ],
                        [ $x[1], $y + $y_short + $hex_side + $y_short - $y_offset ],
                        [ $x[1], $y + $y_short + $hex_side + $y_short + $hex_side + $y_offset ];
                }
                @x = map $_ + $distance, reverse @x; # draw symmetrical pattern
                $x += $distance;
            }
            
            push @polygons, Slic3r::Polygon->new($p);
        }
        
        $_->rotate(-$rotate_vector->[0][0], $hex_center) for @polygons;
        $self->cache->{$cache_id} = [@polygons];
    }
    
    # build polylines from polygons without re-appending the initial point:
    # this cuts the last segment on purpose, so that the jump to the next 
    # path is more straight
    my @paths = map Slic3r::Polyline->new(@$_), map @$_, @{intersection_ex(
        $self->cache->{$cache_id},
        [ map @$_, $expolygon->offset_ex($overlap_distance) ],
    )};
    my $collection = Slic3r::ExtrusionPath::Collection->new(
        paths => [ map Slic3r::ExtrusionPath->pack(polyline => $_, role => -1), @paths ],
    );
    
    return { flow_spacing => $params{flow_spacing} },
        map $_->polyline, $collection->shortest_path;
}

1;
