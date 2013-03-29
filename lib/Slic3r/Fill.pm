package Slic3r::Fill;
use Moo;

use Slic3r::Fill::ArchimedeanChords;
use Slic3r::Fill::Base;
use Slic3r::Fill::Concentric;
use Slic3r::Fill::Flowsnake;
use Slic3r::Fill::HilbertCurve;
use Slic3r::Fill::Honeycomb;
use Slic3r::Fill::Line;
use Slic3r::Fill::OctagramSpiral;
use Slic3r::Fill::PlanePath;
use Slic3r::Fill::Rectilinear;
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(X Y PI scale chained_path);
use Slic3r::Geometry::Clipper qw(union_ex diff diff_ex intersection_ex offset);
use Slic3r::Surface ':types';


has 'print'     => (is => 'ro', required => 1);
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
);

sub filler {
    my $self = shift;
    my ($filler) = @_;
    
    if (!ref $self) {
        return $FillTypes{$filler}->new;
    }
    
    if (!$self->fillers->{$filler}) {
        my $f = $self->fillers->{$filler} = $FillTypes{$filler}->new;
        $f->bounding_box([ $self->print->bounding_box ]) if $f->can('bounding_box');
    }
    return $self->fillers->{$filler};
}

sub make_fill {
    my $self = shift;
    my ($layerm) = @_;
    
    Slic3r::debugf "Filling layer %d:\n", $layerm->id;
    
    my @surfaces = ();
    
    # if hollow object is requested, remove internal surfaces
    # (this needs to be done after internal-solid shells are created)
    if ($Slic3r::Config->fill_density == 0) {
        @surfaces = grep $_->surface_type != S_TYPE_INTERNAL, @surfaces;
    }
    
    # merge adjacent surfaces
    # in case of bridge surfaces, the ones with defined angle will be attached to the ones
    # without any angle (shouldn't this logic be moved to process_external_surfaces()?)
    {
        my @surfaces_with_bridge_angle = grep defined $_->bridge_angle, @{$layerm->fill_surfaces};
        
        # give priority to bridges
        my @groups = Slic3r::Surface->group({merge_solid => 1}, @{$layerm->fill_surfaces});
        @groups = sort { defined $a->[0]->bridge_angle ? -1 : 0 } @groups;
        
        foreach my $group (@groups) {
            my $union = union_ex([ map $_->p, @$group ], undef, 1);
            
            # subtract surfaces having a defined bridge_angle from any other
            if (@surfaces_with_bridge_angle && !defined $group->[0]->bridge_angle) {
                $union = diff_ex(
                    [ map @$_, @$union ],
                    [ map $_->p, @surfaces_with_bridge_angle ],
                    1,
                );
            }
            
            # subtract any other surface already processed
            $union = diff_ex(
                [ map @$_, @$union ],
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
    my $distance_between_surfaces = $layerm->solid_infill_flow->scaled_spacing;
    {
        my $collapsed = diff(
            [ map @{$_->expolygon}, @surfaces ],
            [ offset(
                [ offset([ map @{$_->expolygon}, @surfaces ], -$distance_between_surfaces/2) ],
                +$distance_between_surfaces/2
            ) ],
            1,
        );
        push @surfaces, map Slic3r::Surface->new(
            expolygon       => $_,
            surface_type    => S_TYPE_INTERNALSOLID,
        ), @{intersection_ex(
            [ offset($collapsed, $distance_between_surfaces) ],
            [
                (map @{$_->expolygon}, grep $_->surface_type == S_TYPE_INTERNALVOID, @surfaces),
                (@$collapsed),
            ],
            undef,
            1,
        )};
    }
    
    # add spacing between surfaces
    @surfaces = map $_->offset(-$distance_between_surfaces / 2 * &Slic3r::INFILL_OVERLAP_OVER_SPACING), @surfaces;
    
    my @fills = ();
    my @fills_ordering_points =  ();
    SURFACE: foreach my $surface (@surfaces) {
        next if $surface->surface_type == S_TYPE_INTERNALVOID;
        my $filler          = $Slic3r::Config->fill_pattern;
        my $density         = $Slic3r::Config->fill_density;
        my $flow            = ($surface->surface_type == S_TYPE_TOP)
            ? $layerm->top_infill_flow
            : $surface->is_solid
                ? $layerm->solid_infill_flow
                : $layerm->infill_flow;
        my $flow_spacing    = $flow->spacing;
        my $is_bridge       = $layerm->id > 0 && $surface->is_bridge;
        my $is_solid        = $surface->is_solid;
        
        # force 100% density and rectilinear fill for external surfaces
        if ($surface->surface_type != S_TYPE_INTERNAL) {
            $density = 1;
            $filler = $Slic3r::Config->solid_fill_pattern;
            if ($is_bridge) {
                $filler = 'rectilinear';
                $flow_spacing = $layerm->extruders->{infill}->bridge_flow->spacing;
            } elsif ($surface->surface_type == S_TYPE_INTERNALSOLID) {
                $filler = 'rectilinear';
            }
        } else {
            next SURFACE unless $density > 0;
        }
        
        my @paths;
        {
            my $f = $self->filler($filler);
            $f->layer_id($layerm->id);
            @paths = $f->fill_surface(
                $surface,
                density         => $density,
                flow_spacing    => $flow_spacing,
                dont_adjust     => $is_bridge,
            );
        }
        my $params = shift @paths;
        
        # ugly hack(tm) to get the right amount of flow (GCode.pm should be fixed)
        $params->{flow_spacing} = $layerm->extruders->{infill}->bridge_flow->width if $is_bridge;
        
        # save into layer
        next unless @paths;
        push @fills, Slic3r::ExtrusionPath::Collection->new(
            paths => [
                map Slic3r::ExtrusionPath->pack(
                    polyline => Slic3r::Polyline->new(@$_),
                    role => ($surface->surface_type == S_TYPE_INTERNALBRIDGE
                        ? EXTR_ROLE_INTERNALBRIDGE
                        : $is_bridge
                            ? EXTR_ROLE_BRIDGE
                            : $is_solid
                                ? ($surface->surface_type == S_TYPE_TOP ? EXTR_ROLE_TOPSOLIDFILL : EXTR_ROLE_SOLIDFILL)
                                : EXTR_ROLE_FILL),
                    height => $surface->thickness,
                    flow_spacing => $params->{flow_spacing} || (warn "Warning: no flow_spacing was returned by the infill engine, please report this to the developer\n"),
                ), @paths,
            ],
        );
        push @fills_ordering_points, $paths[0][0];
    }
    
    # add thin fill regions
    push @fills, @{$layerm->thin_fills};
    push @fills_ordering_points, map $_->unpack->points->[0], @{$layerm->thin_fills};
    
    # organize infill paths using a nearest-neighbor search
    @fills = @fills[ chained_path(\@fills_ordering_points) ];
    
    return @fills;
}

1;
