package Slic3r::Fill::Honeycomb;
use Moo;

extends 'Slic3r::Fill::Base';
with qw(Slic3r::Fill::WithDirection);

has 'cache'         => (is => 'rw', default => sub {{}});

use Slic3r::Geometry qw(PI X Y MIN MAX scale scaled_epsilon);
use Slic3r::Geometry::Clipper qw(intersection intersection_pl);

sub angles () { [0, PI/3, PI/3*2] }

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    my $rotate_vector = $self->infill_direction($surface);
    
    # cache hexagons math
    my $cache_id = sprintf "d%s_s%s", $params{density}, $self->spacing;
    my $m;
    if (!($m = $self->cache->{$cache_id})) {
        $m = $self->cache->{$cache_id} = {};
        my $min_spacing = scale($self->spacing);
        $m->{distance} = $min_spacing / $params{density};
        $m->{hex_side} = $m->{distance} / (sqrt(3)/2);
        $m->{hex_width} = $m->{distance} * 2;  # $m->{hex_width} == $m->{hex_side} * sqrt(3);
        my $hex_height = $m->{hex_side} * 2;
        $m->{pattern_height} = $hex_height + $m->{hex_side};
        $m->{y_short} = $m->{distance} * sqrt(3)/3;
        $m->{x_offset} = $min_spacing / 2;
        $m->{y_offset} = $m->{x_offset} * sqrt(3)/3;
        $m->{hex_center} = Slic3r::Point->new($m->{hex_width}/2, $m->{hex_side});
    }
    
    my @polygons = ();
    {
        # adjust actual bounding box to the nearest multiple of our hex pattern
        # and align it so that it matches across layers
        
        my $bounding_box = $surface->expolygon->bounding_box;
        {
            # rotate bounding box according to infill direction
            my $bb_polygon = $bounding_box->polygon;
            $bb_polygon->rotate($rotate_vector->[0][0], $m->{hex_center});
            $bounding_box = $bb_polygon->bounding_box;
            
            # extend bounding box so that our pattern will be aligned with other layers
            # $bounding_box->[X1] and [Y1] represent the displacement between new bounding box offset and old one
            $bounding_box->merge_point(Slic3r::Point->new(
                $bounding_box->x_min - ($bounding_box->x_min % $m->{hex_width}),
                $bounding_box->y_min - ($bounding_box->y_min % $m->{pattern_height}),
            ));
        }
        
        my $x = $bounding_box->x_min;
        while ($x <= $bounding_box->x_max) {
            my $p = [];
            
            my @x = ($x + $m->{x_offset}, $x + $m->{distance} - $m->{x_offset});
            for (1..2) {
                @$p = reverse @$p; # turn first half upside down
                my @p = ();
                for (my $y = $bounding_box->y_min; $y <= $bounding_box->y_max; $y += $m->{y_short} + $m->{hex_side} + $m->{y_short} + $m->{hex_side}) {
                    push @$p,
                        [ $x[1], $y + $m->{y_offset} ],
                        [ $x[0], $y + $m->{y_short} - $m->{y_offset} ],
                        [ $x[0], $y + $m->{y_short} + $m->{hex_side} + $m->{y_offset} ],
                        [ $x[1], $y + $m->{y_short} + $m->{hex_side} + $m->{y_short} - $m->{y_offset} ],
                        [ $x[1], $y + $m->{y_short} + $m->{hex_side} + $m->{y_short} + $m->{hex_side} + $m->{y_offset} ];
                }
                @x = map $_ + $m->{distance}, reverse @x; # draw symmetrical pattern
                $x += $m->{distance};
            }
            
            push @polygons, Slic3r::Polygon->new(@$p);
        }
        
        $_->rotate(-$rotate_vector->[0][0], $m->{hex_center}) for @polygons;
    }
    
    my @paths;
    if ($params{complete} || 1) {
        # we were requested to complete each loop;
        # in this case we don't try to make more continuous paths
        @paths = map $_->split_at_first_point,
            @{intersection([ $surface->p ], \@polygons)};
        
    } else {
        # consider polygons as polylines without re-appending the initial point:
        # this cuts the last segment on purpose, so that the jump to the next 
        # path is more straight
        @paths = @{intersection_pl(
            [ map Slic3r::Polyline->new(@$_), @polygons ],
            [ @{$surface->expolygon} ],
        )};
        
        # connect paths
        if (@paths) {  # prevent calling leftmost_point() on empty collections
            my $collection = Slic3r::Polyline::Collection->new(@paths);
            @paths = ();
            foreach my $path (@{$collection->chained_path_from($collection->leftmost_point, 0)}) {
                if (@paths) {
                    # distance between first point of this path and last point of last path
                    my $distance = $paths[-1]->last_point->distance_to($path->first_point);
                    
                    if ($distance <= $m->{hex_width}) {
                        $paths[-1]->append_polyline($path);
                        next;
                    }
                }
                
                # make a clone before $collection goes out of scope
                push @paths, $path->clone;
            }
        }
        
        # clip paths again to prevent connection segments from crossing the expolygon boundaries
        @paths = @{intersection_pl(
            \@paths,
            [ map @$_, @{$surface->expolygon->offset_ex(scaled_epsilon)} ],
        )};
    }
    
    return @paths;
}

1;
