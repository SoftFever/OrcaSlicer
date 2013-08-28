package Slic3r::GCode::ArcFitting;
use Moo;

use Slic3r::Geometry qw(X Y PI scale unscale deg2rad);

extends 'Slic3r::GCode::Reader';
has 'config'                    => (is => 'ro', required => 1);
has 'max_angle'                 => (is => 'rw', default => sub { deg2rad(15) });
has 'len_epsilon'               => (is => 'rw', default => sub { scale 10 });
has 'parallel_degrees_limit'    => (is => 'rw', default => sub { abs(deg2rad(3)) });

sub process {
    my $self = shift;
    my ($gcode) = @_;
    
    my $new_gcode           = "";
    my $buffer              = "";
    my @cur_path            = ();
    my $cur_len             = 0;
    my $cur_relative_angle  = 0;
    
    $self->parse($gcode, sub {
        my ($reader, $cmd, $args, $info) = @_;
        
        if ($info->{extruding} && $info->{dist_XY} > 0) {
            my $point = Slic3r::Point->new_scale($args->{X}, $args->{Y});
            
            if (@cur_path >= 2) {
                if ($cur_path[-1]->distance_to($point) > $self->len_epsilon) {
                    # if the last distance is not compatible with the current arc, flush it
                    $new_gcode .= $self->flush_path(\@cur_path, \$buffer);
                } elsif (@cur_path >= 3) {
                    my $rel_angle = relative_angle(@cur_path[-2,-1], $point);
                    if (($cur_relative_angle != 0 && abs($rel_angle - $cur_relative_angle) > $self->parallel_degrees_limit)   # relative angle is too different from the previous one
                        || abs($rel_angle) < $self->parallel_degrees_limit                            # relative angle is almost parallel
                        || $rel_angle > $self->max_angle) {                                           # relative angle is excessive (too sharp)
                        # in these cases, $point does not really look like an additional point of the current arc
                        $new_gcode .= $self->flush_path(\@cur_path, \$buffer);
                    }
                }
            }
            
            if (@cur_path == 0) {
                # we're starting a path, so let's prepend the previous position
                push @cur_path, Slic3r::Point->new_scale($self->X, $self->Y), $point;
                $buffer .= $info->{raw} . "\n";
                $cur_len = $cur_path[0]->distance_to($cur_path[1]);
            } else {
                push @cur_path, $point;
                $buffer .= $info->{raw} . "\n";
                if (@cur_path == 3) {
                    # we have two segments, time to compute a reference angle
                    $cur_relative_angle = relative_angle(@cur_path[0,1,2]);
                }
            }
        } else {
            $new_gcode .= $self->flush_path(\@cur_path, \$buffer);
            $new_gcode .= $info->{raw} . "\n";
        }
    });
    
    $new_gcode .= $self->flush_path(\@cur_path, \$buffer);
    return $new_gcode;
}

sub flush_path {
    my ($self, $cur_path, $buffer) = @_;
    
    my $gcode = "";
    
    if (@$cur_path >= 3) {
        # if we have enough points, then we have an arc
        $$buffer =~ s/^/;/mg;
        $gcode = "; these moves were replaced by an arc:\n" . $$buffer;
        
        my $orientation = Slic3r::Geometry::point_is_on_left_of_segment($cur_path->[2], [ @$cur_path[0,1] ]) ? 'ccw' : 'cw';
        
        # to find the center, we intersect the perpendicular lines
        # passing by midpoints of $s1 and last segment
        # a better method would be to draw all the perpendicular lines
        # and find the centroid of the enclosed polygon, or to
        # intersect multiple lines and find the centroid of the convex hull
        # around the intersections
        my $arc_center;
        {
            my $s1_mid      = Slic3r::Line->new(@$cur_path[0,1])->midpoint;
            my $last_mid    = Slic3r::Line->new(@$cur_path[-2,-1])->midpoint;
            my $rotation_angle = PI/2 * ($orientation eq 'ccw' ? -1 : 1);
            my $ray1        = Slic3r::Line->new($s1_mid,   $cur_path->[1]->clone->rotate($rotation_angle, $s1_mid));
            my $last_ray    = Slic3r::Line->new($last_mid, $cur_path->[-1]->clone->rotate($rotation_angle, $last_mid));
            $arc_center     = $ray1->intersection($last_ray, 0) or next POINT;
        }
        my $radius = $arc_center->distance_to($cur_path->[0]);
        my $total_angle = Slic3r::Geometry::angle3points($arc_center, @$cur_path[0,-1]);
        my $length = $orientation eq 'ccw'
            ? $radius * $total_angle
            : $radius * (2*PI - $total_angle);
        
        # compose G-code line
        $gcode .= $orientation eq 'cw' ? "G2" : "G3";
        $gcode .= sprintf " X%.3f Y%.3f", map unscale($_), @{$cur_path->[-1]};  # destination point
        
        # XY distance of the center from the start position
        $gcode .= sprintf " I%.3f J%.3f", map { unscale($arc_center->[$_] - $cur_path->[0][$_]) } (X,Y);
        
        my $E = 0;  # TODO: compute E using $length
        $gcode .= sprintf(" %s%.5f", $self->config->extrusion_axis, $E)
            if $E;
        
        my $F = 0;  # TODO: extract F from original moves
        $gcode .= " F$F\n";
    } else {
        $gcode = $$buffer;
    }
    
    $$buffer = "";
    splice @$cur_path, 0, $#$cur_path;  # keep last point as starting position for next path
    return $gcode;
}

sub relative_angle {
    my ($p1, $p2, $p3) = @_;
    
    my $s1 = Slic3r::Line->new($p1, $p2);
    my $s2 = Slic3r::Line->new($p2, $p3);
    my $s1_angle = $s1->atan;
    my $s2_angle = $s2->atan;
    $s1_angle += 2*PI if $s1_angle < 0;
    $s2_angle += 2*PI if $s2_angle < 0;
    return $s2_angle - $s1_angle;
}

1;
