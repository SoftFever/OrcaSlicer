package Slic3r::GCode::MotionPlanner;
use Moo;

has 'islands'           => (is => 'ro', required => 1);  # arrayref of ExPolygons
has 'internal'          => (is => 'ro', default => sub { 1 });
has '_space'            => (is => 'ro', default => sub { Slic3r::GCode::MotionPlanner::ConfigurationSpace->new });
has '_inner'            => (is => 'ro', default => sub { [] });  # arrayref of ExPolygons

use List::Util qw(first max);
use Slic3r::Geometry qw(A B scale epsilon);
use Slic3r::Geometry::Clipper qw(offset offset_ex diff_ex intersection_pl);

# clearance (in mm) from the perimeters
has '_inner_margin' => (is => 'ro', default => sub { scale 1 });
has '_outer_margin' => (is => 'ro', default => sub { scale 2 });

# this factor weigths the crossing of a perimeter 
# vs. the alternative path. a value of 5 means that
# a perimeter will be crossed if the alternative path
# is >= 5x the length of the straight line we could
# follow if we decided to cross the perimeter.
# a nearly-infinite value for this will only permit
# perimeter crossing when there's no alternative path.
use constant CROSSING_PENALTY => 20;

use constant POINT_DISTANCE => 10;  # unscaled

# setup our configuration space
sub BUILD {
    my $self = shift;
    
    my $point_distance = scale POINT_DISTANCE;
    my $nodes = $self->_space->nodes;
    my $edges = $self->_space->edges;
    
    # process individual islands
    for my $i (0 .. $#{$self->islands}) {
        my $expolygon = $self->islands->[$i];
            
        # find external margin
        my $outer = offset([ @$expolygon ], +$self->_outer_margin);
        my @outer_points = map @{$_->equally_spaced_points($point_distance)}, @$outer;
        
        # add outer points to graph
        my $o_outer = $self->_space->add_nodes(@outer_points);
        
        # find pairs of visible outer points and add them to the graph
        for my $i (0 .. $#outer_points) {
            for my $j (($i+1) .. $#outer_points) {
                my ($a, $b) = ($outer_points[$i], $outer_points[$j]);
                my $line = Slic3r::Polyline->new($a, $b);
                # outer points are visible when their line has empty intersection with islands
                my $intersection = intersection_pl(
                    [ $line ],
                    [ map @$_, @{$self->islands} ],
                );
                if (!@$intersection) {
                    $self->_space->add_edge($i+$o_outer, $j+$o_outer, $line->length);
                }
            }
        }
        
        if ($self->internal) {
            # find internal margin
            my $inner = offset_ex([ @$expolygon ], -$self->_inner_margin);
            push @{ $self->_inner }, @$inner;
            my @inner_points = map @{$_->equally_spaced_points($point_distance)}, map @$_, @$inner;
            
            # add points to graph and get their offset
            my $o_inner = $self->_space->add_nodes(@inner_points);
            
            # find pairs of visible inner points and add them to the graph
            for my $i (0 .. $#inner_points) {
                for my $j (($i+1) .. $#inner_points) {
                    my ($a, $b) = ($inner_points[$i], $inner_points[$j]);
                    my $line = Slic3r::Line->new($a, $b);
                    # turn $inner into an ExPolygonCollection and use $inner->contains_line()
                    if (first { $_->contains_line($line) } @$inner) {
                        $self->_space->add_edge($i+$o_inner, $j+$o_inner, $line->length);
                    }
                }
            }
            
            # generate the stripe around slice contours
            my $contour = diff_ex(
                $outer,
                [ map @$_, @$inner ],
            );
            
            # find pairs of visible points in this area and add them to the graph
            for my $i (0 .. $#inner_points) {
                for my $j (0 .. $#outer_points) {
                    my ($a, $b) = ($inner_points[$i], $outer_points[$j]);
                    my $line = Slic3r::Line->new($a, $b);
                    # turn $contour into an ExPolygonCollection and use $contour->contains_line()
                    if (first { $_->contains_line($line) } @$contour) {
                        $self->_space->add_edge($i+$o_inner, $j+$o_outer, $line->length * CROSSING_PENALTY);
                    }
                }
            }
        }
    }
    
    # since Perl has no infinity symbol and we don't want to overcomplicate
    # the Dijkstra algorithm with string constants or -1 values
    $self->_space->_infinity(10 * (max(map values %$_, values %{$self->_space->edges}) // 0));
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("space.svg",
            no_arrows       => 1,
            expolygons      => $self->islands,
            lines           => $self->_space->get_lines,
            points          => $self->_space->nodes,
        );
        printf "%d islands\n", scalar @{$self->islands};
        
        eval "use Devel::Size";
        print  "MEMORY USAGE:\n";
        printf "  %-19s = %.1fMb\n", $_, Devel::Size::total_size($self->$_)/1024/1024
            for qw(_space islands);
        printf "  %-19s = %.1fMb\n", $_, Devel::Size::total_size($self->_space->$_)/1024/1024
            for qw(nodes edges);
        printf "  %-19s = %.1fMb\n", 'self', Devel::Size::total_size($self)/1024/1024;
        
        exit if $self->internal;
    }
}

sub shortest_path {
    my $self = shift;
    my ($from, $to) = @_;
    
    return Slic3r::Polyline->new($from, $to)
        if !@{$self->_space->nodes};
    
    # create a temporary configuration space
    my $space = $self->_space->clone;
    
    # add from/to points to the temporary configuration space
    my $node_from   = $self->_add_point_to_space($from, $space);
    my $node_to     = $self->_add_point_to_space($to, $space);
    
    # compute shortest path
    my $path = $space->shortest_path($node_from, $node_to);
    
    if (!$path->is_valid) {
        Slic3r::debugf "Failed to compute shortest path.\n";
        return Slic3r::Polyline->new($from, $to);
    }
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("path.svg",
            no_arrows       => 1,
            expolygons      => $self->islands,
            lines           => $space->get_lines,
            red_points      => [$from, $to],
            red_polylines   => [$path],
        );
        exit;
    }
    
    return $path;
}

# returns the index of the new node
sub _add_point_to_space {
    my ($self, $point, $space) = @_;
    
    my $n = $space->add_nodes($point);
    
    # check whether we are inside an island or outside
    my $inside = defined first { $self->islands->[$_]->contains_point($point) } 0..$#{$self->islands};

    # find candidates by checking visibility from $from to them
    foreach my $idx (0..$#{$space->nodes}) {
        my $line = Slic3r::Line->new($point, $space->nodes->[$idx]);
        # if $point is inside an island, it is visible from $idx when island contains their line
        # if $point is outside an island, it is visible from $idx when their line does not cross any island
        if (
            ($inside && defined first { $_->contains_line($line) } @{$self->_inner})
                || (!$inside && !@{intersection_pl(
                    [ $line->as_polyline ],
                    [ map @$_, @{$self->islands} ],
                )})
            ) {
            # $n ($point) and $idx are visible
            $space->add_edge($n, $idx, $line->length);
        }
    }
    
    # if we found no visibility, retry with larger margins
    if (!exists $space->edges->{$n} && $inside) {
        foreach my $idx (0..$#{$space->nodes}) {
            my $line = Slic3r::Line->new($point, $space->nodes->[$idx]);
            if (defined first { $_->contains_line($line) } @{$self->islands}) {
                # $n ($point) and $idx are visible
                $space->add_edge($n, $idx, $line->length);
            }
        }
    }
    
    warn "Temporary node is not visible from any other node"
        if !exists $space->edges->{$n};
    
    return $n;
}

package Slic3r::GCode::MotionPlanner::ConfigurationSpace;
use Moo;

has 'nodes'     => (is => 'rw', default => sub { [] });  # [ Point, ... ]
has 'edges'     => (is => 'rw', default => sub { {} });  # node_idx => { node_idx => distance, ... }
has '_infinity' => (is => 'rw');

sub clone {
    my $self = shift;
    
    return (ref $self)->new(
        nodes       => [ map $_->clone, @{$self->nodes} ],
        edges       => { map { $_ => { %{$self->edges->{$_}} } } keys %{$self->edges} },
        _infinity   => $self->_infinity,
    );
}

sub nodes_count {
    my $self = shift;
    return scalar(@{ $self->nodes });
}

sub add_nodes {
    my ($self, @nodes) = @_;
    
    my $offset = $self->nodes_count;
    push @{ $self->nodes }, @nodes;
    return $offset;
}

sub add_edge {
    my ($self, $a, $b, $dist) = @_;
    $self->edges->{$a}{$b} = $self->edges->{$b}{$a} = $dist;
}

sub shortest_path {
    my ($self, $node_from, $node_to) = @_;
    
    my $edges = $self->edges;
    my (%dist, %visited, %prev);
    $dist{$_} = $self->_infinity for keys %$edges;
    $dist{$node_from} = 0;
    
    my @queue = ($node_from);
    while (@queue) {
        my $u = -1;
        {
            # find node in @queue with smallest distance in %dist and has not been visited
            my $d = -1;
            foreach my $n (@queue) {
                next if $visited{$n};
                if ($u == -1 || $dist{$n} < $d) {
                    $u = $n;
                    $d = $dist{$n};
                }
            }
        }
        last if $u == $node_to;
        
        # remove $u from @queue
        @queue = grep $_ != $u, @queue;
        $visited{$u} = 1;
        
        # loop through neighbors of $u
        foreach my $v (keys %{ $edges->{$u} }) {
            my $alt = $dist{$u} + $edges->{$u}{$v};
            if ($alt < $dist{$v}) {
                $dist{$v} = $alt;
                $prev{$v} = $u;
                if (!$visited{$v}) {
                    push @queue, $v;
                }
            }
        }
    }
    
    my @points = ();
    {
        my $u = $node_to;
        while (exists $prev{$u}) {
            unshift @points, $self->nodes->[$u];
            $u = $prev{$u};
        }
        unshift @points, $self->nodes->[$node_from];
    }
    
    return Slic3r::Polyline->new(@points);
}

# for debugging purposes
sub get_lines {
    my $self = shift;
    
    my @lines = ();
    my %lines = ();
    for my $i (keys %{$self->edges}) {
        for my $j (keys %{$self->edges->{$i}}) {
            my $line_id = join '_', sort $i, $j;
            next if $lines{$line_id};
            $lines{$line_id} = 1;
            push @lines, Slic3r::Line->new(map $self->nodes->[$_], $i, $j);
        }
    }
    
    return [@lines];
}

1;
