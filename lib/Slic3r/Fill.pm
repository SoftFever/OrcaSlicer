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
use Slic3r::Geometry qw(X Y scale shortest_path);
use Slic3r::Geometry::Clipper qw(union_ex diff_ex);
use Slic3r::Surface ':types';


has 'print'     => (is => 'ro', required => 1);
has 'max_print_dimension' => (is => 'rw');
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

sub BUILD {
    my $self = shift;
    
    my $print_size = $self->print->size;
    my $max_print_dimension = ($print_size->[X] > $print_size->[Y] ? $print_size->[X] : $print_size->[Y]) * sqrt(2);
    $self->max_print_dimension($max_print_dimension);
    
    $self->filler($_) for ('rectilinear', $Slic3r::Config->fill_pattern, $Slic3r::Config->solid_fill_pattern);
}

sub filler {
    my $self = shift;
    my ($filler) = @_;
    if (!$self->fillers->{$filler}) {
        $self->fillers->{$filler} = $FillTypes{$filler}->new(print => $self->print);
        $self->fillers->{$filler}->max_print_dimension($self->max_print_dimension);
    }
    return $self->fillers->{$filler};
}

sub make_fill {
    my $self = shift;
    my ($layer) = @_;
    
    $_->layer($layer) for values %{$self->fillers};
    
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
        my $distance = scale $layer->infill_flow->spacing / 2;
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
                $flow_spacing = sqrt($Slic3r::Config->bridge_flow_ratio * ($layer->infill_flow->nozzle_diameter**2));
            } elsif ($surface->surface_type == S_TYPE_INTERNALSOLID) {
                $filler = 'rectilinear';
            }
        } else {
            next SURFACE unless $density > 0;
        }
        
        my @paths = $self->fillers->{$filler}->fill_surface(
            $surface,
            density         => $density,
            flow_spacing    => $flow_spacing,
        );
        my $params = shift @paths;
        
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
                    depth_layers => $surface->depth_layers,
                    flow_spacing => $params->{flow_spacing} || (warn "Warning: no flow_spacing was returned by the infill engine, please report this to the developer\n"),
                ), @paths,
            ],
        );
        push @fills_ordering_points, $paths[0][0];
    }
    
    # add thin fill regions
    {
        my %args = (
            role            => EXTR_ROLE_SOLIDFILL,
            flow_spacing    => $layer->perimeter_flow->spacing,
        );
        push @fills, map {
            $_->isa('Slic3r::Polygon')
                ? (map $_->pack, Slic3r::ExtrusionLoop->new(polygon  => $_, %args)->split_at_first_point)
                : Slic3r::ExtrusionPath->pack(polyline => $_, %args),
        } @{$layer->thin_fills};
    }
    push @fills_ordering_points, map $_->[0], @{$layer->thin_fills};
    
    # organize infill paths using a shortest path search
    @fills = @{shortest_path([
        map [ $fills_ordering_points[$_], $fills[$_] ], 0..$#fills,
    ])};
    
    return @fills;
}

1;
