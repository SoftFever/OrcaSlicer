package Slic3r::Fill;
use Moo;

use Slic3r::ExtrusionPath ':roles';
use Slic3r::Fill::3DHoneycomb;
use Slic3r::Fill::Base;
use Slic3r::Fill::Concentric;
use Slic3r::Fill::Honeycomb;
use Slic3r::Fill::PlanePath;
use Slic3r::Fill::Rectilinear;
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(X Y PI scale chained_path deg2rad);
use Slic3r::Geometry::Clipper qw(union union_ex diff diff_ex intersection_ex offset offset2);
use Slic3r::Surface ':types';


has 'bounding_box' => (is => 'ro', required => 0);
has 'fillers'   => (is => 'rw', default => sub { {} });

our %FillTypes = (
    archimedeanchords   => 'Slic3r::Fill::ArchimedeanChords',
    rectilinear         => 'Slic3r::Fill::Rectilinear',
    flowsnake           => 'Slic3r::Fill::Flowsnake',
    octagramspiral      => 'Slic3r::Fill::OctagramSpiral',
    hilbertcurve        => 'Slic3r::Fill::HilbertCurve',
    line                => 'Slic3r::Fill::Line',
    concentric          => 'Slic3r::Fill::Concentric',
    honeycomb           => 'Slic3r::Fill::Honeycomb',
    '3dhoneycomb'       => 'Slic3r::Fill::3DHoneycomb',
);

sub filler {
    my $self = shift;
    my ($filler) = @_;
    
    if (!ref $self) {
        return $FillTypes{$filler}->new;
    }
    
    $self->fillers->{$filler} ||= $FillTypes{$filler}->new(
        bounding_box => $self->bounding_box,
    );
    return $self->fillers->{$filler};
}

sub make_fill {
    my $self = shift;
    my ($layerm) = @_;
    
    Slic3r::debugf "Filling layer %d:\n", $layerm->id;
    
    my $fill_density            = $layerm->config->fill_density;
    my $infill_flow             = $layerm->flow(FLOW_ROLE_INFILL);
    my $solid_infill_flow       = $layerm->flow(FLOW_ROLE_SOLID_INFILL);
    my $top_solid_infill_flow   = $layerm->flow(FLOW_ROLE_TOP_SOLID_INFILL);
    
    my @surfaces = ();
    
    # merge adjacent surfaces
    # in case of bridge surfaces, the ones with defined angle will be attached to the ones
    # without any angle (shouldn't this logic be moved to process_external_surfaces()?)
    {
        my @surfaces_with_bridge_angle = grep { $_->bridge_angle >= 0 } @{$layerm->fill_surfaces};
        
        # group surfaces by distinct properties
        my @groups = @{$layerm->fill_surfaces->group};
        
        # merge compatible groups (we can generate continuous infill for them)
        {
            # cache flow widths and patterns used for all solid groups
            # (we'll use them for comparing compatible groups)
            my @is_solid = my @fw = my @pattern = ();
            for (my $i = 0; $i <= $#groups; $i++) {
                # we can only merge solid non-bridge surfaces, so discard
                # non-solid surfaces
                if ($groups[$i][0]->is_solid && (!$groups[$i][0]->is_bridge || $layerm->id == 0)) {
                    $is_solid[$i] = 1;
                    $fw[$i] = ($groups[$i][0]->surface_type == S_TYPE_TOP)
                        ? $top_solid_infill_flow->width
                        : $solid_infill_flow->width;
                    $pattern[$i] = $groups[$i][0]->is_external
                        ? $layerm->config->external_fill_pattern
                        : 'rectilinear';
                } else {
                    $is_solid[$i]   = 0;
                    $fw[$i]         = 0;
                    $pattern[$i]    = 'none';
                }
            }
            
            # loop through solid groups
            for (my $i = 0; $i <= $#groups; $i++) {
                next if !$is_solid[$i];
                
                # find compatible groups and append them to this one
                for (my $j = $i+1; $j <= $#groups; $j++) {
                    next if !$is_solid[$j];
                
                    if ($fw[$i] == $fw[$j] && $pattern[$i] eq $pattern[$j]) {
                        # groups are compatible, merge them
                        push @{$groups[$i]}, @{$groups[$j]};
                        splice @groups,     $j, 1;
                        splice @is_solid,   $j, 1;
                        splice @fw,         $j, 1;
                        splice @pattern,    $j, 1;
                    }
                }
            }
        }
        
        # give priority to bridges
        @groups = sort { ($a->[0]->bridge_angle >= 0) ? -1 : 0 } @groups;
        
        foreach my $group (@groups) {
            my $union_p = union([ map $_->p, @$group ], 1);
            
            # subtract surfaces having a defined bridge_angle from any other
            if (@surfaces_with_bridge_angle && $group->[0]->bridge_angle < 0) {
                $union_p = diff(
                    $union_p,
                    [ map $_->p, @surfaces_with_bridge_angle ],
                    1,
                );
            }
            
            # subtract any other surface already processed
            my $union = diff_ex(
                $union_p,
                [ map $_->p, @surfaces ],
                1,
            );
            
            push @surfaces, map $group->[0]->clone(expolygon => $_), @$union;
        }
    }
    
    # we need to detect any narrow surfaces that might collapse
    # when adding spacing below
    # such narrow surfaces are often generated in sloping walls
    # by bridge_over_infill() and combine_infill() as a result of the
    # subtraction of the combinable area from the layer infill area,
    # which leaves small areas near the perimeters
    # we are going to grow such regions by overlapping them with the void (if any)
    # TODO: detect and investigate whether there could be narrow regions without
    # any void neighbors
    my $distance_between_surfaces = $infill_flow->scaled_spacing;
    {
        my $collapsed = diff(
            [ map @{$_->expolygon}, @surfaces ],
            offset2([ map @{$_->expolygon}, @surfaces ], -$distance_between_surfaces/2, +$distance_between_surfaces/2),
            1,
        );
        push @surfaces, map Slic3r::Surface->new(
            expolygon       => $_,
            surface_type    => S_TYPE_INTERNALSOLID,
        ), @{intersection_ex(
            offset($collapsed, $distance_between_surfaces),
            [
                (map @{$_->expolygon}, grep $_->surface_type == S_TYPE_INTERNALVOID, @surfaces),
                (@$collapsed),
            ],
            1,
        )};
    }
    
    # add spacing between surfaces
    @surfaces = map @{$_->offset(-$distance_between_surfaces / 2)}, @surfaces;
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("fill_" . $layerm->print_z . ".svg",
            expolygons      => [ map $_->expolygon, grep !$_->is_solid, @surfaces ],
            red_expolygons  => [ map $_->expolygon, grep  $_->is_solid, @surfaces ],
        );
    }
    
    my @fills = ();
    SURFACE: foreach my $surface (@surfaces) {
        next if $surface->surface_type == S_TYPE_INTERNALVOID;
        my $filler          = $layerm->config->fill_pattern;
        my $density         = $fill_density;
        my $role = ($surface->surface_type == S_TYPE_TOP) ? FLOW_ROLE_TOP_SOLID_INFILL
            : $surface->is_solid ? FLOW_ROLE_SOLID_INFILL
            : FLOW_ROLE_INFILL;
        my $is_bridge       = $layerm->id > 0 && $surface->is_bridge;
        my $is_solid        = $surface->is_solid;
        
        if ($surface->is_solid) {
            $density = 100;
            $filler = 'rectilinear';
            if ($surface->is_external && !$is_bridge) {
                $filler = $layerm->config->external_fill_pattern;
            }
        } else {
            next SURFACE unless $density > 0;
        }
        
        # get filler object
        my $f = $self->filler($filler);
        
        # calculate the actual flow we'll be using for this infill
        my $h = $surface->thickness == -1 ? $layerm->height : $surface->thickness;
        my $flow = $layerm->region->flow(
            $role,
            $h,
            $is_bridge || $f->use_bridge_flow,
            $layerm->id == 0,
            -1,
            $layerm->object,
        );
        
        # calculate flow spacing for infill pattern generation
        my $using_internal_flow = 0;
        if (!$is_solid && !$is_bridge) {
            # it's internal infill, so we can calculate a generic flow spacing 
            # for all layers, for avoiding the ugly effect of
            # misaligned infill on first layer because of different extrusion width and
            # layer height
            my $internal_flow = $layerm->region->flow(
                FLOW_ROLE_INFILL,
                $layerm->object->config->layer_height,  # TODO: handle infill_every_layers?
                0,  # no bridge
                0,  # no first layer
                -1, # auto width
                $layerm->object,
            );
            $f->spacing($internal_flow->spacing);
            $using_internal_flow = 1;
        } else {
            $f->spacing($flow->spacing);
        }
        
        $f->layer_id($layerm->id);
        $f->z($layerm->print_z);
        $f->angle(deg2rad($layerm->config->fill_angle));
        $f->loop_clipping(scale($flow->nozzle_diameter) * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);
        my @polylines = $f->fill_surface(
            $surface,
            density         => $density/100,
            layer_height    => $h,
        );
        next unless @polylines;
        
        # calculate actual flow from spacing (which might have been adjusted by the infill
        # pattern generator)
        if ($using_internal_flow) {
            # if we used the internal flow we're not doing a solid infill
            # so we can safely ignore the slight variation that might have
            # been applied to $f->flow_spacing
        } else {
            $flow = Slic3r::Flow->new_from_spacing(
                spacing         => $f->spacing,
                nozzle_diameter => $flow->nozzle_diameter,
                layer_height    => $h,
                bridge          => $is_bridge || $f->use_bridge_flow,
            );
        }
        my $mm3_per_mm = $flow->mm3_per_mm;
        
        # save into layer
        {
            my $role = $is_bridge ? EXTR_ROLE_BRIDGE
                : $is_solid ? (($surface->surface_type == S_TYPE_TOP) ? EXTR_ROLE_TOPSOLIDFILL : EXTR_ROLE_SOLIDFILL)
                : EXTR_ROLE_FILL;
            
            push @fills, my $collection = Slic3r::ExtrusionPath::Collection->new;
            $collection->no_sort($f->no_sort);
            $collection->append(
                map Slic3r::ExtrusionPath->new(
                    polyline    => $_,
                    role        => $role,
                    mm3_per_mm  => $mm3_per_mm,
                    width       => $flow->width,
                    height      => $flow->height,
                ), @polylines,
            );
        }
    }
    
    # add thin fill regions
    foreach my $thin_fill (@{$layerm->thin_fills}) {
        push @fills, Slic3r::ExtrusionPath::Collection->new($thin_fill);
    }
    
    return @fills;
}

1;
