package Slic3r::GCode::ArcFitting;
use Moo;

use Slic3r::Geometry qw(X Y PI scale unscale epsilon scaled_epsilon deg2rad angle3points);

extends 'Slic3r::GCode::Reader';
has 'config'                    => (is => 'ro', required => 0);
has 'min_segments'              => (is => 'rw', default => sub { 2 });
has 'min_total_angle'           => (is => 'rw', default => sub { deg2rad(30) });
has 'max_relative_angle'        => (is => 'rw', default => sub { deg2rad(15) });
has 'len_epsilon'               => (is => 'rw', default => sub { scale 0.2 });
has 'angle_epsilon'             => (is => 'rw', default => sub { abs(deg2rad(10)) });
has '_extrusion_axis'           => (is => 'lazy');
has '_path'                     => (is => 'rw');
has '_cur_F'                    => (is => 'rw');
has '_cur_E'                    => (is => 'rw');
has '_cur_E0'                   => (is => 'rw');
has '_comment'                  => (is => 'rw');

sub _build__extrusion_axis {
    my ($self) = @_;
    return $self->config ? $self->config->get_extrusion_axis : 'E';
}

sub process {
    my $self = shift;
    my ($gcode) = @_;
    
    die "Arc fitting is not available (incomplete feature)\n";
    die "Arc fitting doesn't support extrusion axis not being E\n" if $self->_extrusion_axis ne 'E';
    
    my $new_gcode = "";
    
    $self->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($info->{extruding} && $info->{dist_XY} > 0) {
            # this is an extrusion segment
            
            # get segment
            my $line = Slic3r::Line->new(
                Slic3r::Point->new_scale($self->X, $self->Y),
                Slic3r::Point->new_scale($args->{X}, $args->{Y}),
            );
            
            # get segment speed
            my $F = $args->{F} // $reader->F;
            
            # get extrusion per unscaled distance unit
            my $e = $info->{dist_E} / unscale($line->length);
            
            if ($self->_path && $F == $self->_cur_F && abs($e - $self->_cur_E) < epsilon) {
                # if speed and extrusion per unit are the same as the previous segments,
                # append this segment to path
                $self->_path->append($line->b);
            } elsif ($self->_path) {
                # segment can't be appended to previous path, so we flush the previous one
                # and start over
                $new_gcode .= $self->path_to_gcode;
                $self->_path(undef);
            }
            
            if (!$self->_path) {
                # if this is the first segment of a path, start it from scratch
                $self->_path(Slic3r::Polyline->new(@$line));
                $self->_cur_F($F);
                $self->_cur_E($e);
                $self->_cur_E0($self->E);
                $self->_comment($info->{comment});
            }
        } else {
            # if we have a path, we flush it and go on
            $new_gcode .= $self->path_to_gcode if $self->_path;
            $new_gcode .= $info->{raw} . "\n";
            $self->_path(undef);
        }
    });
    
    $new_gcode .= $self->path_to_gcode if $self->_path;
    return $new_gcode;
}

sub path_to_gcode {
    my ($self) = @_;
    
    my @chunks = $self->detect_arcs($self->_path);
    
    my $gcode = "";
    my $E = $self->_cur_E0;
    foreach my $chunk (@chunks) {
        if ($chunk->isa('Slic3r::Polyline')) {
            my @lines = @{$chunk->lines};
            
            $gcode .= sprintf "G1 F%s\n", $self->_cur_F;
            foreach my $line (@lines) {
                $E += $self->_cur_E * unscale($line->length);
                $gcode .= sprintf "G1 X%.3f Y%.3f %s%.5f",
                    (map unscale($_), @{$line->b}),
                    $self->_extrusion_axis, $E;
                $gcode .= sprintf " ; %s", $self->_comment if $self->_comment;
                $gcode .= "\n";
            }
        } elsif ($chunk->isa('Slic3r::GCode::ArcFitting::Arc')) {
            $gcode .= !$chunk->is_ccw ? "G2" : "G3";
            $gcode .= sprintf " X%.3f Y%.3f", map unscale($_), @{$chunk->end};  # destination point
            
            # XY distance of the center from the start position
            $gcode .= sprintf " I%.3f", unscale($chunk->center->[X] - $chunk->start->[X]);
            $gcode .= sprintf " J%.3f", unscale($chunk->center->[Y] - $chunk->start->[Y]);
            
            $E += $self->_cur_E * unscale($chunk->length);
            $gcode .= sprintf " %s%.5f", $self->_extrusion_axis, $E;
            
            $gcode .= sprintf " F%s\n", $self->_cur_F;
        }
    }
    return $gcode;
}

sub detect_arcs {
    my ($self, $path) = @_;
    
    my @chunks = ();
    my @arc_points = ();
    my $polyline = undef;
    my $arc_start = undef;
    
    my @points = @$path;
    for (my $i = 1; $i <= $#points; ++$i) {
        my $end = undef;
        
        # we need at least three points to check whether they form an arc
        if ($i < $#points) {
            my $len = $points[$i-1]->distance_to($points[$i]);
            my $rel_angle = PI - angle3points(@points[$i, $i-1, $i+1]);
            if (abs($rel_angle) <= $self->max_relative_angle) {
                for (my $j = $i+1; $j <= $#points; ++$j) {
                    # check whether @points[($i-1)..$j] form an arc
                    last if abs($points[$j-1]->distance_to($points[$j]) - $len) > $self->len_epsilon;
                    last if abs(PI - angle3points(@points[$j-1, $j-2, $j]) - $rel_angle) > $self->angle_epsilon;
                    
                    $end = $j;
                }
            }
        }
        
        if (defined $end && ($end - $i + 1) >= $self->min_segments) {
            my $arc = polyline_to_arc(Slic3r::Polyline->new(@points[($i-1)..$end]));
            
            if (1||$arc->angle >= $self->min_total_angle) {
                push @chunks, $arc;
                
                # continue scanning after arc points
                $i = $end;
                next;
            }
        }
        
        # if last chunk was a polyline, append to it
        if (@chunks && $chunks[-1]->isa('Slic3r::Polyline')) {
            $chunks[-1]->append($points[$i]);
        } else {
            push @chunks, Slic3r::Polyline->new(@points[($i-1)..$i]);
        }
    }
    
    return @chunks;
}

sub polyline_to_arc {
    my ($polyline) = @_;
    
    my @points = @$polyline;
    
    my $is_ccw = $points[2]->ccw(@points[0,1]) > 0;
        
    # to find the center, we intersect the perpendicular lines
    # passing by first and last vertex;
    # a better method would be to draw all the perpendicular lines
    # and find the centroid of the enclosed polygon, or to
    # intersect multiple lines and find the centroid of the convex hull
    # around the intersections
    my $arc_center;
    {
        my $first_ray = Slic3r::Line->new(@points[0,1]);
        $first_ray->rotate(PI/2 * ($is_ccw ? 1 : -1), $points[0]);
        
        my $last_ray = Slic3r::Line->new(@points[-2,-1]);
        $last_ray->rotate(PI/2 * ($is_ccw ? -1 : 1), $points[-1]);
        
        # require non-parallel rays in order to compute an accurate center
        return if abs($first_ray->atan2_ - $last_ray->atan2_) < deg2rad(30);
        
        $arc_center = $first_ray->intersection($last_ray, 0) or return;
    }
    
    # angle measured in ccw orientation
    my $abs_angle = Slic3r::Geometry::angle3points($arc_center, @points[0,-1]);
    
    my $rel_angle = $is_ccw
        ? $abs_angle
        : (2*PI - $abs_angle);
    
    my $arc = Slic3r::GCode::ArcFitting::Arc->new(
        start   => $points[0]->clone,
        end     => $points[-1]->clone,
        center  => $arc_center,
        is_ccw  => $is_ccw || 0,
        angle   => $rel_angle,
    );
    
    if (0) {
        printf "points = %d, path length = %f, arc angle = %f, arc length = %f\n",
            scalar(@points),
            unscale(Slic3r::Polyline->new(@points)->length),
            Slic3r::Geometry::rad2deg($rel_angle),
            unscale($arc->length);
    }
    
    return $arc;
}

package Slic3r::GCode::ArcFitting::Arc;
use Moo;

has 'start'  => (is => 'ro', required => 1);
has 'end'    => (is => 'ro', required => 1);
has 'center' => (is => 'ro', required => 1);
has 'is_ccw' => (is => 'ro', required => 1);
has 'angle'  => (is => 'ro', required => 1);

sub radius {
    my ($self) = @_;
    return $self->start->distance_to($self->center);
}

sub length {
    my ($self) = @_;
    return $self->radius * $self->angle;
}

1;
