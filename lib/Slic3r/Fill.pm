package Slic3r::Fill;
use Moo;

use Slic3r::Fill::ArchimedeanChords;
use Slic3r::Fill::Base;
use Slic3r::Fill::Concentric;
use Slic3r::Fill::Flowsnake;
use Slic3r::Fill::HilbertCurve;
use Slic3r::Fill::Line;
use Slic3r::Fill::OctagramSpiral;
use Slic3r::Fill::PlanePath;
use Slic3r::Fill::Rectilinear;
use Slic3r::Fill::Rectilinear2;
use Slic3r::Geometry qw(scale shortest_path);
use Slic3r::Geometry::Clipper qw(union_ex diff_ex);

use XXX;

has 'print'     => (is => 'ro', required => 1);
has 'fillers'   => (is => 'rw', default => sub { {} });

our %FillTypes = (
    archimedeanchords   => 'Slic3r::Fill::ArchimedeanChords',
    rectilinear         => 'Slic3r::Fill::Rectilinear',
    rectilinear2        => 'Slic3r::Fill::Rectilinear2',
    flowsnake           => 'Slic3r::Fill::Flowsnake',
    octagramspiral      => 'Slic3r::Fill::OctagramSpiral',
    hilbertcurve        => 'Slic3r::Fill::HilbertCurve',
    line                => 'Slic3r::Fill::Line',
    concentric          => 'Slic3r::Fill::Concentric',
);

sub BUILD {
    my $self = shift;
    $self->fillers->{$_} ||= $FillTypes{$_}->new(print => $self->print)
        for ('rectilinear', $Slic3r::fill_pattern, $Slic3r::solid_fill_pattern);
}

sub make_fill {
    my $self = shift;
    my ($layer) = @_;
    
    my $max_print_dimension = $self->print->max_length * sqrt(2);
    for (values %{$self->fillers}) {
        $_->layer($layer);
        $_->max_print_dimension($max_print_dimension);
    }
    
    printf "Filling layer %d:\n", $layer->id;
    
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
        my $distance = scale $Slic3r::flow_spacing / 2;
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
    
    # organize infill surfaces using a shortest path search
    @surfaces = @{shortest_path([
        map [ $_->contour->[0], $_ ], @surfaces,
    ])};
    
    SURFACE: foreach my $surface (@surfaces) {
        my $filler          = $Slic3r::fill_pattern;
        my $density         = $Slic3r::fill_density;
        my $flow_spacing    = $Slic3r::flow_spacing;
        my $is_bridge       = $layer->id > 0 && $surface->surface_type eq 'bottom';
        my $is_solid        = $surface->surface_type =~ /^(top|bottom)$/;
        
        # force 100% density and rectilinear fill for external surfaces
        if ($surface->surface_type ne 'internal') {
            $density = 1;
            $filler = $Slic3r::solid_fill_pattern;
            if ($is_bridge) {
                $filler = 'rectilinear';
                $flow_spacing = sqrt($Slic3r::bridge_flow_ratio * ($Slic3r::nozzle_diameter**2));
            } elsif ($surface->surface_type eq 'internal-solid') {
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
        push @{ $layer->fills }, Slic3r::ExtrusionPath::Collection->new(
            paths => [
                map Slic3r::ExtrusionPath->new(
                    polyline => Slic3r::Polyline->new(@$_),
                    role => ($is_bridge ? 'bridge' : $is_solid ? 'solid-fill' : 'fill'),
                    depth_layers => $surface->depth_layers,
                    flow_spacing => $params->{flow_spacing},
                ), @paths,
            ],
        ) if @paths;
    }
    
    # add thin fill regions
    push @{ $layer->fills }, Slic3r::ExtrusionPath::Collection->new(
        paths => [
            map {
                $_->isa('Slic3r::Polygon')
                    ? Slic3r::ExtrusionLoop->new(polygon => $_, role => 'solid-fill')->split_at($_->[0])
                    : Slic3r::ExtrusionPath->new(polyline => $_, role => 'solid-fill')
            } @{$layer->thin_fills},
        ],
    ) if @{$layer->thin_fills};
}

1;
