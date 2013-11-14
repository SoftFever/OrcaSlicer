package Slic3r::Print::SupportMaterial;
use Moo;

use List::Util qw(sum min max);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale scaled_epsilon PI rad2deg deg2rad);
use Slic3r::Geometry::Clipper qw(offset diff union union_ex intersection offset_ex offset2);
use Slic3r::Surface ':types';

has 'config' => (is => 'rw', required => 1);
has 'flow'   => (is => 'rw', required => 1);

use constant DEBUG_CONTACT_ONLY => 0;

# how much we extend support around the actual contact area
use constant MARGIN => 1.5;
    
# increment used to reach MARGIN in steps to avoid trespassing thin objects
use constant MARGIN_STEP => MARGIN/3;

sub generate {
    my ($self, $object) = @_;
    
    # Determine the top surfaces of the support, defined as:
    # contact = overhangs - clearance + margin
    # This method is responsible for identifying what contact surfaces
    # should the support material expose to the object in order to guarantee
    # that it will be effective, regardless of how it's built below.
    my ($contact, $overhang) = $self->contact_area($object);
    
    # Determine the top surfaces of the object. We need these to determine 
    # the layer heights of support material and to clip support to the object
    # silhouette.
    my ($top) = $self->object_top($object, $contact);
    
    # We now know the upper and lower boundaries for our support material object
    # (@$contact_z and @$top_z), so we can generate intermediate layers.
    my ($support_z) = $self->support_layers_z(
        [ sort keys %$contact ],
        [ sort keys %$top ],
        max(map $_->height, @{$object->layers})
    );
    
    # If we wanted to apply some special logic to the first support layers lying on
    # object's top surfaces this is the place to detect them
    
    # Propagate contact layers downwards to generate interface layers
    my ($interface) = $self->generate_interface_layers($support_z, $contact, $top);
    $self->clip_with_object($interface, $support_z, $object);
    
    # Propagate contact layers and interface layers downwards to generate
    # the main support layers.
    my ($base) = $self->generate_base_layers($support_z, $contact, $interface, $top);
    $self->clip_with_object($base, $support_z, $object);
    
    # Install support layers into object.
    push @{$object->support_layers}, map Slic3r::Layer::Support->new(
        object  => $object,
        id      => $_,
        height  => ($_ == 0) ? $support_z->[$_] : ($support_z->[$_] - $support_z->[$_-1]),
        print_z => $support_z->[$_],
        slice_z => -1,
        slices  => [],
    ), 0 .. $#$support_z;
    
    # Generate the actual toolpaths and save them into each layer.
    $self->generate_toolpaths($object, $overhang, $contact, $interface, $base);
}

sub contact_area {
    my ($self, $object) = @_;
    
    # if user specified a custom angle threshold, convert it to radians
    my $threshold_rad;
    if ($self->config->support_material_threshold) {
        $threshold_rad = deg2rad($self->config->support_material_threshold + 1);  # +1 makes the threshold inclusive
        Slic3r::debugf "Threshold angle = %d°\n", rad2deg($threshold_rad);
    }
    
    # determine contact areas
    my %contact  = ();  # contact_z => [ polygons ]
    my %overhang = ();  # contact_z => [ polygons ] - this stores the actual overhang supported by each contact layer
    for my $layer_id (0 .. $#{$object->layers}) {
        if ($self->config->raft_layers == 0) {
            next if $layer_id == 0;
        } elsif (!$self->config->support_material) {
            # if we are only going to generate raft just check 
            # the 'overhangs' of the first object layer
            last if $layer_id > 0;
        }
        my $layer = $object->layers->[$layer_id];
        my $lower_layer = $object->layers->[$layer_id-1];
        
        # detect overhangs and contact areas needed to support them
        my (@overhang, @contact) = ();
        foreach my $layerm (@{$layer->regions}) {
            my $fw = $layerm->perimeter_flow->scaled_width;
            my $diff;
            
            # If a threshold angle was specified, use a different logic for detecting overhangs.
            if (defined $threshold_rad
                || $layer_id < $self->config->support_material_enforce_layers
                || $self->config->raft_layers > 0) {
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
                
                # collapse very tiny spots
                $diff = offset2($diff, -$fw/10, +$fw/10);
                
                # $diff now contains the ring or stripe comprised between the boundary of 
                # lower slices and the centerline of the last perimeter in this overhanging layer.
                # Void $diff means that there's no upper perimeter whose centerline is
                # outside the lower slice boundary, thus no overhang
            }
            
            # TODO: this is the place to remove bridged areas
            
            next if !@$diff;
            push @overhang, @$diff;  # NOTE: this is not the full overhang as it misses the outermost half of the perimeter width!
            
            # Let's define the required contact area by using a max gap of half the upper 
            # extrusion width and extending the area according to the configured margin.
            # We increment the area in steps because we don't want our support to overflow
            # on the other side of the object (if it's very thin).
            {
                my @slices_margin = @{offset([ map @$_, @{$lower_layer->slices} ], $fw/2)};
                for ($fw/2, map {scale MARGIN_STEP} 1..(MARGIN / MARGIN_STEP)) {
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
    
    return (\%contact, \%overhang);
}

sub object_top {
    my ($self, $object, $contact) = @_;
    
    # find object top surfaces
    # we'll use them to clip our support and detect where does it stick
    my %top = ();  # print_z => [ expolygons ]
    my $projection = [];
    foreach my $layer (reverse @{$object->layers}) {
        if (my @top = map @{$_->slices->filter_by_type(S_TYPE_TOP)}, @{$layer->regions}) {
            # compute projection of the contact areas above this top layer
            # first add all the 'new' contact areas to the current projection
            # ('new' means all the areas that are lower than the last top layer
            # we considered)
            my $min_top = min(keys %top) // max(keys %$contact);
            # use <= instead of just < because otherwise we'd ignore any contact regions
            # having the same Z of top layers
            push @$projection, map @{$contact->{$_}}, grep { $_ > $layer->print_z && $_ <= $min_top } keys %$contact;
            
            # now find whether any projection falls onto this top surface
            my $touching = intersection($projection, [ map $_->p, @top ]);
            if (@$touching) {
                # grow top surfaces so that interface and support generation are generated
                # with some spacing from object - it looks we don't need the actual
                # top shapes so this can be done here
                $top{ $layer->print_z } = offset($touching, $self->flow->scaled_width);
            }
            
            # remove the areas that touched from the projection that will continue on 
            # next, lower, top surfaces
            $projection = diff($projection, $touching);
        }
    }
    
    return \%top;
}

sub support_layers_z {
    my ($self, $contact_z, $top_z, $max_object_layer_height) = @_;
    
    # quick table to check whether a given Z is a top surface
    my %top = map { $_ => 1 } @$top_z;
    
    # determine layer height for any non-contact layer
    # we use max() to prevent many ultra-thin layers to be inserted in case
    # layer_height > nozzle_diameter * 0.75
    my $nozzle_diameter = $self->flow->nozzle_diameter;
    my $support_material_height = max($max_object_layer_height, $nozzle_diameter * 0.75);
    
    my @z = sort { $a <=> $b } @$contact_z, @$top_z, (map $_ + $nozzle_diameter, @$top_z);
    
    # enforce first layer height
    my $first_layer_height = $self->config->get_value('first_layer_height');
    shift @z while @z && $z[0] <= $first_layer_height;
    unshift @z, $first_layer_height;
    
    # add raft layers by dividing the space between first layer and
    # first contact layer evenly
    if ($self->config->raft_layers > 1 && @z >= 2) {
        # $z[1] is last raft layer (contact layer for the first layer object)
        my $height = ($z[1] - $z[0]) / ($self->config->raft_layers - 1);
        splice @z, 1, 0,
            map { int($_*100)/100 }
            map { $z[0] + $height * $_ }
            0..($self->config->raft_layers - 1);
    }
    
    for (my $i = $#z; $i >= 0; $i--) {
        my $target_height = $support_material_height;
        if ($i > 0 && $top{ $z[$i-1] }) {
            $target_height = $nozzle_diameter;
        }
        
        # enforce first layer height
        if (($i == 0 && $z[$i] > $target_height + $first_layer_height)
            || ($z[$i] - $z[$i-1] > $target_height + Slic3r::Geometry::epsilon)) {
            splice @z, $i, 0, ($z[$i] - $target_height);
            $i++;
        }
    }
    
    # remove duplicates and make sure all 0.x values have the leading 0
    {
        my %sl = map { 1 * $_ => 1 } @z;
        @z = sort { $a <=> $b } keys %sl;
    }
    
    return \@z;
}

sub generate_interface_layers {
    my ($self, $support_z, $contact, $top) = @_;
    
    # let's now generate interface layers below contact areas
    my %interface = ();  # layer_id => [ polygons ]
    my $interface_layers = $self->config->support_material_interface_layers;
    for my $layer_id (0 .. $#$support_z) {
        my $z = $support_z->[$layer_id];
        my $this = $contact->{$z} // next;
        
        # count contact layer as interface layer
        for (my $i = $layer_id-1; $i >= 0 && $i > $layer_id-$interface_layers; $i--) {
            $z = $support_z->[$i];
            my @overlapping_layers = $self->overlapping_layers($i, $support_z);
            my @overlapping_z = map $support_z->[$_], @overlapping_layers;
            
            # Compute interface area on this layer as diff of upper contact area
            # (or upper interface area) and layer slices.
            # This diff is responsible of the contact between support material and
            # the top surfaces of the object. We should probably offset the top 
            # surfaces vertically before performing the diff, but this needs 
            # investigation.
            $this = $interface{$i} = diff(
                [
                    @$this,                         # clipped projection of the current contact regions
                    @{ $interface{$i} || [] },      # interface regions already applied to this layer
                ],
                [
                    (map @$_, map $top->{$_}, grep exists $top->{$_}, @overlapping_z),  # top slices on this layer
                    (map @$_, map $contact->{$_}, grep exists $contact->{$_}, @overlapping_z),  # contact regions on this layer
                ],
                1,
            );
        }
    }
    
    return \%interface;
}

sub generate_base_layers {
    my ($self, $support_z, $contact, $interface, $top) = @_;
    
    # let's now generate support layers under interface layers
    my $base = {};  # layer_id => [ polygons ]
    {
        for my $i (reverse 0 .. $#$support_z-1) {
            my $z = $support_z->[$i];
            my @overlapping_layers = $self->overlapping_layers($i, $support_z);
            my @overlapping_z = map $support_z->[$_], @overlapping_layers;
            
            # in case we have no interface layers, look at upper contact
            # (1 interface layer means we only have contact layer, so $interface->{$i+1} is empty)
            my @upper_contact = ();
            if ($self->config->support_material_interface_layers <= 1) {
                @upper_contact = @{ $contact->{$support_z->[$i+1]} || [] };
            }
            
            $base->{$i} = diff(
                [
                    @{ $base->{$i+1} || [] },         # support regions on upper layer
                    @{ $interface->{$i+1} || [] },    # interface regions on upper layer
                    @upper_contact,                   # contact regions on upper layer
                ],
                [
                    (map @$_, map $top->{$_}, grep exists $top->{$_}, @overlapping_z),  # top slices on this layer
                    (map @$_, map $interface->{$_}, grep exists $interface->{$_}, @overlapping_layers),  # interface regions on this layer
                    (map @$_, map $contact->{$_}, grep exists $contact->{$_}, @overlapping_z),  # contact regions on this layer
                ],
                1,
            );
        }
    }
    
    return $base;
}

sub clip_with_object {
    my ($self, $support, $support_z, $object) = @_;
    
    foreach my $i (keys %$support) {
        next if !@{$support->{$i}};
        
        my $zmax = $support_z->[$i];
        my $zmin = ($i == 0) ? 0 : $support_z->[$i-1];
        my @layers = grep { $_->print_z > $zmin && ($_->print_z - $_->height) < $zmax }
            @{$object->layers};
        
        $support->{$i} = diff(
            $support->{$i},
            offset([ map @$_, map @{$_->slices}, @layers ], +$self->flow->scaled_width),
        );
    }
}

sub generate_toolpaths {
    my ($self, $object, $overhang, $contact, $interface, $base) = @_;
    
    my $flow = $self->flow;
    
    # shape of contact area
    my $contact_loops   = 1;
    my $circle_radius   = 1.5 * $flow->scaled_width;
    my $circle_distance = 3 * $circle_radius;
    my $circle          = Slic3r::Polygon->new(map [ $circle_radius * cos $_, $circle_radius * sin $_ ],
                            (5*PI/3, 4*PI/3, PI, 2*PI/3, PI/3, 0));
    
    Slic3r::debugf "Generating patterns\n";
    
    # prepare fillers
    my $pattern = $self->config->support_material_pattern;
    my @angles = ($self->config->support_material_angle);
    if ($pattern eq 'rectilinear-grid') {
        $pattern = 'rectilinear';
        push @angles, $angles[0] + 90;
    }
    
    my %fillers = (
        interface   => $object->fill_maker->filler('rectilinear'),
        support     => $object->fill_maker->filler($pattern),
    );
    
    my $interface_angle = $self->config->support_material_angle + 90;
    my $interface_spacing = $self->config->support_material_interface_spacing + $flow->spacing;
    my $interface_density = $interface_spacing == 0 ? 1 : $flow->spacing / $interface_spacing;
    my $support_spacing = $self->config->support_material_spacing + $flow->spacing;
    my $support_density = $support_spacing == 0 ? 1 : $flow->spacing / $support_spacing;
    
    my $process_layer = sub {
        my ($layer_id) = @_;
        my $layer = $object->support_layers->[$layer_id];
        my $z = $layer->print_z;
        
        my $overhang    = $overhang->{$z}           || [];
        my $contact     = $contact->{$z}            || [];
        my $interface   = $interface->{$layer_id}   || [];
        my $base        = $base->{$layer_id}        || [];
        
        if (DEBUG_CONTACT_ONLY) {
            $interface = [];
            $base = [];
        }
        
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("layer_" . $z . ".svg",
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
            );
        }
        
        # islands
        $layer->support_islands->append(@{union_ex([ @$interface, @$base, @$contact ])});
        
        # contact
        my $contact_infill = [];
        if ($self->config->support_material_interface_layers == 0) {
            # if no interface layers were requested we treat the contact layer
            # exactly as a generic base layer
            push @$base, @$contact;
        } elsif (@$contact && $contact_loops > 0) {
            # generate the outermost loop
            my @loops0 = ();
            {
                # find centerline of the external loop of the contours
                my @external_loops = @{offset($contact, -$flow->scaled_width/2)};
                
                # only consider the loops facing the overhang
                {
                    my $overhang_with_margin = offset_ex($overhang, +$flow->scaled_width/2);
                    @external_loops = grep {
                        @{ Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection(
                            [ map $_->pp, @$overhang_with_margin ],
                            [ $_->split_at_first_point->pp ],
                        ) }
                    } @external_loops;
                }
                
                # apply a pattern to the loop
                my @positions = map @{Slic3r::Polygon->new(@$_)->equally_spaced_points($circle_distance)}, @external_loops;
                @loops0 = @{diff(
                    [ @external_loops ],
                    [ map { my $c = $circle->clone; $c->translate(@$_); $c } @positions ],
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
                    [ map $_->pp, @{offset_ex($overhang, +scale MARGIN)} ],
                    [ map $_->split_at_first_point->pp, @loops ],
                ) };
            
            # add the contact infill area to the interface area
            # note that growing loops by $circle_radius ensures no tiny
            # extrusions are left inside the circles; however it creates
            # a very large gap between loops and contact_infill, so maybe another
            # solution should be found to achieve both goals
            $contact_infill = diff(
                $contact,
                [ map $_->grow($circle_radius*1.1), @loops ],
            );
            
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
            
            # join regions by offsetting them to ensure they're merged
            $interface = offset([ @$interface, @$contact_infill ], scaled_epsilon);
            
            # turn base support into interface when it's contained in our holes
            # (this way we get wider interface anchoring)
            {
                my @p = @$interface;
                @$interface = ();
                foreach my $p (@p) {
                    if ($p->is_clockwise) {
                        my $p2 = $p->clone;
                        $p2->make_counter_clockwise;
                        next if !@{diff([$p2], $base, 1)};
                    }
                    push @$interface, $p;
                }
            }
            $base = diff($base, $interface);
            
            my @paths = ();
            foreach my $expolygon (@{union_ex($interface)}) {
                my ($params, @p) = $fillers{interface}->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon, surface_type => S_TYPE_INTERNAL),
                    density         => $interface_density,
                    flow_spacing    => $flow->spacing,
                    complete        => 1,
                );
                
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
        if (@$base) {
            my $filler = $fillers{support};
            $filler->angle($angles[ ($layer_id) % @angles ]);
            my $density         = $support_density;
            my $flow_spacing    = $flow->spacing;
            
            # TODO: use offset2_ex()
            my $to_infill = union_ex($base, 1);
            my @paths = ();
            
            # base flange
            if ($layer_id == 0) {
                $filler = $fillers{interface};
                $filler->angle($self->config->support_material_angle + 90);
                $density        = 0.5;
                $flow_spacing   = $object->print->first_layer_support_material_flow->spacing;
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
                my ($params, @p) = $filler->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon, surface_type => S_TYPE_INTERNAL),
                    density         => $density,
                    flow_spacing    => $flow_spacing,
                    complete        => 1,
                );
                
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
            Slic3r::SVG::output("islands_" . $z . ".svg",
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
                green_polylines     => [ map $_->unpack->polyline, @{$layer->support_contact_fills} ],
                polylines           => [ map $_->unpack->polyline, @{$layer->support_fills} ],
            );
        }
    };
    
    Slic3r::parallelize(
        items => [ 0 .. $#{$object->support_layers} ],
        thread_cb => sub {
            my $q = shift;
            while (defined (my $layer_id = $q->dequeue)) {
                $process_layer->($layer_id);
            }
        },
        no_threads_cb => sub {
            $process_layer->($_) for 0 .. $#{$object->support_layers};
        },
    );
}

# this method returns the indices of the layers overlapping with the given one
sub overlapping_layers {
    my ($self, $i, $support_z) = @_;
    
    my $zmax = $support_z->[$i];
    my $zmin = ($i == 0) ? 0 : $support_z->[$i-1];
    
    return grep {
        my $zmax2 = $support_z->[$_];
        my $zmin2 = ($_ == 0) ? 0 : $support_z->[$_-1];
        $zmax > $zmin2 && $zmin < $zmax2;
    } 0..$#$support_z;
}

1;
