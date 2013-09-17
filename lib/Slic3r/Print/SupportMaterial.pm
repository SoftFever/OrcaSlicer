package Slic3r::Print::SupportMaterial;
use Moo;

use List::Util qw(sum min max);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale PI);
use Slic3r::Geometry::Clipper qw(offset diff union_ex intersection offset_ex offset2);
use Slic3r::Surface ':types';

has 'object' => (is => 'ro', required => 1);

sub flow {
    my ($self) = @_;
    return $self->object->print->support_material_flow;
}

sub generate {
    my $self = shift;
    
    my $flow = $self->flow;
    
    # how much we extend support around the actual contact area
    #my $margin      = $flow->scaled_width / 2;
    my $margin      = scale 3;
    
    # increment used to reach $margin in steps to avoid trespassing thin objects
    my $margin_step = $margin/3;
    
    # if user specified a custom angle threshold, convert it to radians
    my $threshold_rad;
    if ($self->object->config->support_material_threshold) {
        $threshold_rad = deg2rad($self->object->config->support_material_threshold + 1);  # +1 makes the threshold inclusive
        Slic3r::debugf "Threshold angle = %d°\n", rad2deg($threshold_rad);
    }
    
    # shape of contact area
    my $contact_loops   = 1;
    my $circle_radius   = 1.5 * $flow->scaled_width;
    my $circle_distance = 3 * $circle_radius;
    my $circle          = Slic3r::Polygon->new(map [ $circle_radius * cos $_, $circle_radius * sin $_ ], (5*PI/3, 4*PI/3, PI, 2*PI/3, PI/3, 0));
    
    # determine contact areas
    my %contact  = ();  # contact_z => [ polygons ]
    my %overhang = ();  # contact_z => [ expolygons ] - this stores the actual overhang supported by each contact layer
    for my $layer_id (1 .. $#{$self->object->layers}) {
        last if $layer_id > $self->object->config->raft_layers && !$self->object->config->support_material;
        my $layer = $self->object->layers->[$layer_id];
        my $lower_layer = $self->object->layers->[$layer_id-1];
        
        # detect overhangs and contact areas needed to support them
        my (@overhang, @contact) = ();
        foreach my $layerm (@{$layer->regions}) {
            my $fw = $layerm->perimeter_flow->scaled_width;
            my $diff;
            
            # If a threshold angle was specified, use a different logic for detecting overhangs.
            if (defined $threshold_rad
                || $layer_id <= $self->object->config->support_material_enforce_layers
                || $layer_id <= $self->object->config->raft_layers) {
                my $d = defined $threshold_rad
                    ? scale $lower_layer->height * ((cos $threshold_rad) / (sin $threshold_rad))
                    : 0;
                
                $diff = diff(
                    offset([ map $_->p, @{$layerm->slices} ], -$d),
                    [ map @$_, @{$lower_layer->slices} ],
                );
                
                # only enforce spacing from the object ($fw/2) if the threshold angle
                # is not too high: in that case, $d will be very small (as we need to catch
                # very short overhangs), and such contact area would be eaten by the
                # enforced spacing, resulting in high threshold angles to be almost ignored
                $diff = diff(
                    offset($diff, $d - $fw/2),
                    [ map @$_, @{$lower_layer->slices} ],
                ) if $d > $fw/2;
            } else {
                $diff = diff(
                    offset([ map $_->p, @{$layerm->slices} ], -$fw/2),
                    [ map @$_, @{$lower_layer->slices} ],
                );
                # $diff now contains the ring or stripe comprised between the boundary of 
                # lower slices and the centerline of the last perimeter in this overhanging layer.
                # Void $diff means that there's no upper perimeter whose centerline is
                # outside the lower slice boundary, thus no overhang
            }
            
            next if !@$diff;
            push @overhang, @{union_ex($diff)};  # NOTE: this is not the full overhang as it misses the outermost half of the perimeter width!
            
            # Let's define the required contact area by using a max gap of half the upper 
            # extrusion width and extending the area according to the configured margin.
            # We increment the area in steps because we don't want our support to overflow
            # on the other side of the object (if it's very thin).
            {
                my @slices_margin = @{offset([ map @$_, @{$lower_layer->slices} ], $fw/2)};
                for ($fw/2, map {$margin_step} 1..($margin / $margin_step)) {
                    $diff = diff(
                        offset($diff, $_),
                        \@slices_margin,
                    );
                }
            }
            push @contact, @$diff;
        }
        next if !@contact;
        
        # now apply the contact areas to the layer were they need to be made
        {
            # get the average nozzle diameter used on this layer
            my @nozzle_diameters = map $_->nozzle_diameter,
                map { $_->perimeter_flow, $_->solid_infill_flow }
                @{$layer->regions};
            my $nozzle_diameter = sum(@nozzle_diameters)/@nozzle_diameters;
            
            my $contact_z = $layer->print_z - $nozzle_diameter * 1.5;
            ###$contact_z = $layer->print_z - $layer->height;
            
            # ignore this contact area if it's too low
            next if $contact_z < $Slic3r::Config->get_value('first_layer_height');
            
            $contact{$contact_z}  = [ @contact ];
            $overhang{$contact_z} = [ @overhang ];
            
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("contact_" . $contact_z . ".svg",
                    expolygons      => union_ex(\@contact),
                    red_expolygons  => \@overhang,
                );
            }
        }
    }
    my @contact_z = sort keys %contact;
    
    # find object top surfaces
    # we'll use them to clip our support and detect where does it stick
    my %top = ();  # print_z => [ expolygons ]
    {
        my $projection = [];
        foreach my $layer (reverse @{$self->object->layers}) {
            if (my @top = map @{$_->slices->filter_by_type(S_TYPE_TOP)}, @{$layer->regions}) {
                # compute projection of the contact areas above this top layer
                # first add all the 'new' contact areas to the current projection
                # ('new' means all the areas that are lower than the last top layer
                # we considered)
                my $min_top = min(keys %top) // max(keys %contact);
                # use <= instead of just < because otherwise we'd ignore any contact regions
                # having the same Z of top layers
                push @$projection, map @{$contact{$_}}, grep { $_ > $layer->print_z && $_ <= $min_top } keys %contact;
                
                # now find whether any projection falls onto this top surface
                my $touching = intersection($projection, [ map $_->p, @top ]);
                if (@$touching) {
                    # grow top surfaces so that interface and support generation are generated
                    # with some spacing from object - it looks we don't need the actual
                    # top shapes so this can be done here
                    $top{ $layer->print_z } = offset($touching, $flow->scaled_spacing);
                }
                
                # remove the areas that touched from the projection that will continue on 
                # next, lower, top surfaces
                $projection = diff($projection, $touching);
            }
        }
    }
    my @top_z = sort keys %top;
    
    # we now know the upper and lower boundaries for our support material object
    # (@contact_z and @top_z), so we can generate intermediate layers
    my @support_layers = $self->_compute_support_layers(\@contact_z, \@top_z);
    
    # if we wanted to apply some special logic to the first support layers lying on
    # object's top surfaces this is the place to detect them
    
    # let's now generate interface layers below contact areas
    my %interface = ();  # layer_id => [ polygons ]
    my $interface_layers = $self->object->config->support_material_interface_layers;
    for my $layer_id (0 .. $#support_layers) {
        my $z = $support_layers[$layer_id];
        my $this = $contact{$z} // next;
        
        # count contact layer as interface layer
        for (my $i = $layer_id-1; $i >= 0 && $i > $layer_id-$interface_layers; $i--) {
            $z = $support_layers[$i];
            # Compute interface area on this layer as diff of upper contact area
            # (or upper interface area) and layer slices.
            # This diff is responsible of the contact between support material and
            # the top surfaces of the object. We should probably offset the top 
            # surfaces before performing the diff, but this needs investigation.
            $this = $interface{$i} = diff(
                [
                    @$this,                         # clipped projection of the current contact regions
                    @{ $interface{$i} || [] },      # interface regions already applied to this layer
                ],
                [
                    @{ $top{$z} || [] },            # top slices on this layer
                    @{ $contact{$z} || [] },        # contact regions on this layer
                ],
                1,
            );
        }
    }
    
    # let's now generate support layers under interface layers
    my %support   = ();  # layer_id => [ polygons ]
    {
        for my $i (reverse 0 .. $#support_layers-1) {
            my $z = $support_layers[$i];
            $support{$i} = diff(
                [
                    @{ $support{$i+1} || [] },      # support regions on upper layer
                    @{ $interface{$i+1} || [] },    # interface regions on upper layer
                ],
                [
                    @{ $top{$z} || [] },            # top slices on this layer
                    @{ $interface{$i} || [] },      # interface regions on this layer
                    @{ $contact{$z} || [] },        # contact regions on this layer
                ],
                1,
            );
        }
    }
    
    push @{$self->object->support_layers}, map Slic3r::Layer::Support->new(
        object  => $self->object,
        id      => $_,
        height  => ($_ == 0) ? $support_layers[$_] : ($support_layers[$_] - $support_layers[$_-1]),
        print_z => $support_layers[$_],
        slice_z => -1,
        slices  => [],
    ), 0 .. $#support_layers;

    Slic3r::debugf "Generating patterns\n";
    
    # prepare fillers
    my $pattern = $self->object->config->support_material_pattern;
    my @angles = ($self->object->config->support_material_angle);
    if ($pattern eq 'rectilinear-grid') {
        $pattern = 'rectilinear';
        push @angles, $angles[0] + 90;
    }
    
    my %fillers = (
        interface   => $self->object->fill_maker->filler('rectilinear'),
        support     => $self->object->fill_maker->filler($pattern),
    );
    
    my $interface_angle = $self->object->config->support_material_angle + 90;
    my $interface_spacing = $self->object->config->support_material_interface_spacing + $flow->spacing;
    my $interface_density = $interface_spacing == 0 ? 1 : $flow->spacing / $interface_spacing;
    my $support_spacing = $self->object->config->support_material_spacing + $flow->spacing;
    my $support_density = $support_spacing == 0 ? 1 : $flow->spacing / $support_spacing;
    
    my $process_layer = sub {
        my ($layer_id) = @_;
        my $layer = $self->object->support_layers->[$layer_id];
        
        my $overhang    = $overhang{$support_layers[$layer_id]} || [];
        my $contact     = $contact{$support_layers[$layer_id]}  || [];
        my $interface   = $interface{$layer_id} || [];
        my $support     = $support{$layer_id}   || [];
        
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("layer_" . $support_layers[$layer_id] . ".svg",
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
            );
        }
        
        # islands
        $layer->support_islands->append(@{union_ex([ @$interface, @$support, @$contact ])});
        
        # contact
        my $contact_infill = [];
        if ($contact && $contact_loops > 0) {
            $contact = [ grep $_->is_counter_clockwise, @$contact ];
            
            # generate the outermost loop
            my @loops0;
            {
                # find centerline of the external loop of the contours
                my @external_loops = @{offset($contact, -$flow->scaled_width/2)};
                
                # apply a pattern to the loop
                my @positions = map Slic3r::Polygon->new(@$_)->split_at_first_point->regular_points($circle_distance), @external_loops;
                @loops0 = @{diff(
                    [ @external_loops ],
                    [ map $circle->clone->translate(@$_), @positions ],
                )};
            }
            
            # make more loops
            my @loops = @loops0;
            for my $i (2..$contact_loops) {
                my $d = ($i-1) * $flow->scaled_spacing;
                push @loops, @{offset2(\@loops0, -$d -0.5*$flow->scaled_spacing, +0.5*$flow->scaled_spacing)};
            }
            
            # clip such loops to the side oriented towards the object
            @loops = map Slic3r::Polyline->new(@$_),
                @{ Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection(
                    [ map $_->pp, @{offset_ex([ map @$_, @$overhang ], +scale 3)} ],
                    [ map Slic3r::Polygon->new(@$_)->split_at_first_point->pp, @loops ],
                ) };
            
            # add the contact infill area to the interface area
            $contact_infill = offset2(\@loops0, -($contact_loops + 0.5) * $flow->scaled_spacing, +0.5*$flow->scaled_spacing);
            
            # transform loops into ExtrusionPath objects
            @loops = map Slic3r::ExtrusionPath->new(
                polyline        => $_,
                role            => EXTR_ROLE_SUPPORTMATERIAL,
                flow_spacing    => $flow->spacing,
            ), @loops;
            
            $layer->support_interface_fills->append(@loops);
        }
        
        # interface and contact infill
        if (@$interface || @$contact_infill) {
            $fillers{interface}->angle($interface_angle);
            
            # steal some space from support
            $interface = intersection(
                offset([ @$interface, @$contact_infill ], scale 3),
                [ @$interface, @$support, @$contact_infill ],
                1,
            );
            $support = diff(
                $support,
                $interface,
            );
            
            my @paths = ();
            foreach my $expolygon (@{union_ex($interface)}) {
                my @p = $fillers{interface}->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon, surface_type => S_TYPE_INTERNAL),
                    density         => $interface_density,
                    flow_spacing    => $flow->spacing,
                    complete        => 1,
                );
                my $params = shift @p;
                
                push @paths, map Slic3r::ExtrusionPath->new(
                    polyline        => Slic3r::Polyline->new(@$_),
                    role            => EXTR_ROLE_SUPPORTMATERIAL,
                    height          => undef,
                    flow_spacing    => $params->{flow_spacing},
                ), @p;
            }            
            $layer->support_interface_fills->append(@paths);
        }
        
        # support or flange
        if (@$support) {
            my $filler = $fillers{support};
            $filler->angle($angles[ ($layer_id) % @angles ]);
            my $density         = $support_density;
            my $flow_spacing    = $flow->spacing;
            
            # TODO: use offset2_ex()
            my $to_infill = union_ex($support, 1);
            my @paths = ();
            
            # base flange
            if ($layer_id == 0) {
                $filler = $fillers{interface};
                $filler->angle($self->object->config->support_material_angle + 90);
                $density        = 0.5;
                $flow_spacing   = $self->object->print->first_layer_support_material_flow->spacing;
            } else {
                # draw a perimeter all around support infill
                # TODO: use brim ordering algorithm
                push @paths, map Slic3r::ExtrusionPath->new(
                    polyline        => $_->split_at_first_point,
                    role            => EXTR_ROLE_SUPPORTMATERIAL,
                    height          => undef,
                    flow_spacing    => $flow->spacing,
                ), map @$_, @$to_infill;
                
                # TODO: use offset2_ex()
                $to_infill = offset_ex([ map @$_, @$to_infill ], -$flow->scaled_spacing);
            }
            
            foreach my $expolygon (@$to_infill) {
                my @p = $filler->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon, surface_type => S_TYPE_INTERNAL),
                    density         => $density,
                    flow_spacing    => $flow_spacing,
                    complete        => 1,
                );
                my $params = shift @p;
                
                push @paths, map Slic3r::ExtrusionPath->new(
                    polyline        => Slic3r::Polyline->new(@$_),
                    role            => EXTR_ROLE_SUPPORTMATERIAL,
                    height          => undef,
                    flow_spacing    => $params->{flow_spacing},
                ), @p;
            }
            
            $layer->support_fills->append(@paths);
        }
        
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("islands_" . $support_layers[$layer_id] . ".svg",
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
                green_polylines     => [ map $_->unpack->polyline, @{$layer->support_contact_fills} ],
                polylines           => [ map $_->unpack->polyline, @{$layer->support_fills} ],
            );
        }
    };
    
    Slic3r::parallelize(
        items => [ 0 .. $#{$self->object->support_layers} ],
        thread_cb => sub {
            my $q = shift;
            while (defined (my $layer_id = $q->dequeue)) {
                $process_layer->($layer_id);
            }
        },
        no_threads_cb => sub {
            $process_layer->($_) for 0 .. $#{$self->object->support_layers};
        },
    );
}

sub _compute_support_layers {
    my ($self, $contact_z, $top_z) = @_;
    
    my $flow = $self->flow;
    
    # quick table to check whether a given Z is a top surface
    my %top = map { $_ => 1 } @$top_z;
    
    # determine layer height for any non-contact layer
    # we use max() to prevent many ultra-thin layers to be inserted in case
    # layer_height > nozzle_diameter * 0.75
    my $support_material_height = max($self->object->config->layer_height, $flow->nozzle_diameter * 0.75);
    
    my @support_layers = sort { $a <=> $b } @$contact_z, @$top_z,
        (map { $_ + $flow->nozzle_diameter } @$top_z);
    
    # enforce first layer height
    my $first_layer_height = $self->object->config->get_value('first_layer_height');
    shift @support_layers while @support_layers && $support_layers[0] <= $first_layer_height;
    unshift @support_layers, $first_layer_height;
    
    for (my $i = $#support_layers; $i >= 0; $i--) {
        my $target_height = $support_material_height;
        if ($i > 0 && $top{ $support_layers[$i-1] }) {
            $target_height = $flow->nozzle_diameter;
        }
        
        # enforce first layer height
        if (($i == 0 && $support_layers[$i] > $target_height + $first_layer_height)
            || ($support_layers[$i] - $support_layers[$i-1] > $target_height + Slic3r::Geometry::epsilon)) {
            splice @support_layers, $i, 0, ($support_layers[$i] - $target_height);
            $i++;
        }
    }
    
    # remove duplicates and make sure all 0.x values have the leading 0
    {
        my %sl = map { 1 * $_ => 1 } @support_layers;
        @support_layers = sort { $a <=> $b } keys %sl;
    }
    
    return @support_layers;
}

1;
