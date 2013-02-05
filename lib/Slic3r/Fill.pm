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
use Slic3r::Geometry::Clipper qw(union_ex diff_ex);
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
    my ($layer) = @_;
    
    Slic3r::debugf "Filling layer %d:\n", $layer->id;
    
    # merge overlapping surfaces
    my @surfaces = ();
    {
        my @surfaces_with_bridge_angle = grep defined $_->bridge_angle, @{$layer->fill_surfaces};
        
        # give priority to bridges
        my @groups = Slic3r::Surface->group({merge_solid => 1}, @{$layer->fill_surfaces});
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
            
            push @surfaces, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => $group->[0]->surface_type,
                bridge_angle => $group->[0]->bridge_angle,
                depth_layers => $group->[0]->depth_layers,
            ), @$union;
        }
    }
    
    # add spacing between adjacent surfaces
    {
        my $distance = $layer->infill_flow->scaled_spacing / 2;
        my @offsets = ();
        foreach my $surface (@surfaces) {
            my $expolygon = $surface->expolygon;
            my $diff = diff_ex(
                [ $expolygon->offset($distance) ],
                $expolygon,
                1,
            );
            push @offsets, map @$_, @$diff;
        }
        
        my @new_surfaces = ();
        foreach my $surface (@surfaces) {
            my $diff = diff_ex(
                $surface->expolygon,
                [ @offsets ],
            );
            
            push @new_surfaces, map Slic3r::Surface->new(
                expolygon => $_,
                surface_type => $surface->surface_type,
                bridge_angle => $surface->bridge_angle,
                depth_layers => $surface->depth_layers,
            ), @$diff;
        }
        @surfaces = @new_surfaces;
    }
    
    my @fills = ();
    my @fills_ordering_points =  ();
    SURFACE: foreach my $surface (@surfaces) {
        my $filler          = $Slic3r::Config->fill_pattern;
        my $density         = $Slic3r::Config->fill_density;
        my $flow_spacing    = $layer->infill_flow->spacing;
        my $is_bridge       = $layer->id > 0 && $surface->surface_type == S_TYPE_BOTTOM;
        my $is_solid        = (grep { $surface->surface_type == $_ } S_TYPE_TOP, S_TYPE_BOTTOM, S_TYPE_INTERNALSOLID) ? 1 : 0;
        
        # force 100% density and rectilinear fill for external surfaces
        if ($surface->surface_type != S_TYPE_INTERNAL) {
            $density = 1;
            $filler = $Slic3r::Config->solid_fill_pattern;
            if ($is_bridge) {
                $filler = 'rectilinear';
                $flow_spacing = $layer->infill_flow->bridge_spacing;
            } elsif ($surface->surface_type == S_TYPE_INTERNALSOLID) {
                $filler = 'rectilinear';
            }
        } else {
            next SURFACE unless $density > 0;
        }
        
        my @paths;
        {
            my $f = $self->filler($filler);
            $f->layer_id($layer->id);
            @paths = $f->fill_surface(
                $surface,
                density         => $density,
                flow_spacing    => $flow_spacing,
                dont_adjust     => $is_bridge,
            );
        }
        my $params = shift @paths;
        
        # ugly hack(tm) to get the right amount of flow (GCode.pm should be fixed)
        $params->{flow_spacing} = $layer->infill_flow->bridge_width if $is_bridge;
        
        # save into layer
        next unless @paths;
        push @fills, Slic3r::ExtrusionPath::Collection->new(
            paths => [
                map Slic3r::ExtrusionPath->pack(
                    polyline => Slic3r::Polyline->new(@$_),
                    role => ($is_bridge
                        ? EXTR_ROLE_BRIDGE
                        : $is_solid
                            ? ($surface->surface_type == S_TYPE_TOP ? EXTR_ROLE_TOPSOLIDFILL : EXTR_ROLE_SOLIDFILL)
                            : EXTR_ROLE_FILL),
                    height => $surface->depth_layers * $layer->height,
                    flow_spacing => $params->{flow_spacing} || (warn "Warning: no flow_spacing was returned by the infill engine, please report this to the developer\n"),
                ), @paths,
            ],
        );
        push @fills_ordering_points, $paths[0][0];
    }
    
    # add thin fill regions
    push @fills, @{$layer->thin_fills};
    push @fills_ordering_points, map $_->unpack->points->[0], @{$layer->thin_fills};
    
    # organize infill paths using a shortest path search
    @fills = @{chained_path([
        map [ $fills_ordering_points[$_], $fills[$_] ], 0..$#fills,
    ])};
    
    return @fills;
}

1;
