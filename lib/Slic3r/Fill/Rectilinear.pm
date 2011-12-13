package Slic3r::Fill::Rectilinear;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(X1 Y1 X2 Y2 A B X Y scale unscale epsilon);
use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $expolygon = $surface->expolygon;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my $bounding_box = [ $expolygon->bounding_box ];
    $bounding_box->[X1] += scale 0.1;
    $bounding_box->[X2] -= scale 0.1;
    
    my $min_spacing = scale $params{flow_width};
    my $distance_between_lines = $min_spacing / $params{density};
    my $line_oscillation = $distance_between_lines - $min_spacing;
    
    my $number_of_lines = int(($bounding_box->[X2] - $bounding_box->[X1]) / $distance_between_lines) + 1;
    my $flow_width = undef;
    if ($params{density} == 1) {
        my $extra_space = ($bounding_box->[X2] - $bounding_box->[X1]) % $distance_between_lines;
        $distance_between_lines += $extra_space / ($number_of_lines - 1) if $number_of_lines > 1;
        $flow_width = unscale $distance_between_lines;
    }
    
    my @paths = ();
    my $x = $bounding_box->[X1];
    my $is_line_pattern = $self->isa('Slic3r::Fill::Line');
    for (my $i = 0; $i < $number_of_lines; $i++) {
        my $vertical_line = [ [$x, $bounding_box->[Y2]], [$x, $bounding_box->[Y1]] ];
        if ($is_line_pattern && $i % 2) {
            $vertical_line->[A][X] -= $line_oscillation;
            $vertical_line->[B][X] += $line_oscillation;
        }
        push @paths, @{ $expolygon->clip_line($vertical_line) };
        $x += $distance_between_lines;
    }
    
    # connect lines
    {
        my $collection = Slic3r::ExtrusionPath::Collection->new(
            paths => [ map Slic3r::ExtrusionPath->cast([ @$_ ], role => 'bogus'), @paths ],
        );
        @paths = ();
        
        my $can_connect = $is_line_pattern
            ? sub { $_[X] <= (abs((($_[2][Y] - $bounding_box->[Y1])*(2 * $line_oscillation)/($bounding_box->[Y2] - $bounding_box->[Y1])) - $line_oscillation) + $distance_between_lines) && $_[Y] <= $distance_between_lines * 5 }
            : sub { ($_[X] <= $distance_between_lines) && ($_[Y] <= $distance_between_lines * 5) };
        
        foreach my $path ($collection->shortest_path) {
            if (@paths) {
                my @distance = map abs($paths[-1][-1][$_] - $path->points->[0][$_]), (X,Y);
                
                # TODO: we should also check that both points are on a fill_boundary to avoid 
                # connecting paths on the boundaries of internal regions
                if ($can_connect->(@distance, $paths[-1][-1])) {
                    push @{$paths[-1]}, @{$path->points};
                    next;
                }
            }
            push @paths, [@{$path->points}];
        }
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return { flow_width => $flow_width }, @paths;
}

1;
