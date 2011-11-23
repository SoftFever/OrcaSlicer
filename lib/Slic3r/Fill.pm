package Slic3r::Fill;
use Moo;

use Slic3r::Fill::ArchimedeanChords;
use Slic3r::Fill::Base;
use Slic3r::Fill::Flowsnake;
use Slic3r::Fill::HilbertCurve;
use Slic3r::Fill::Line;
use Slic3r::Fill::OctagramSpiral;
use Slic3r::Fill::PlanePath;
use Slic3r::Fill::Rectilinear;
use Slic3r::Fill::Rectilinear2;
use Slic3r::Geometry qw(shortest_path);

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
    
    # organize $layer->fill_surfaces using a shortest path search
    @{ $layer->fill_surfaces } = @{shortest_path([
        map [ $_->[0]->contour->points->[0], $_ ], grep @$_, @{ $layer->fill_surfaces },
    ])};
    
    foreach my $surfaces (@{ $layer->fill_surfaces }) {
    
        # organize $surfaces using a shortest path search
        @$surfaces = @{shortest_path([
            map [ $_->contour->points->[0], $_ ], @$surfaces,
        ])};
    
        SURFACE: foreach my $surface (@$surfaces) {
            my $filler      = $Slic3r::fill_pattern;
            my $density     = $Slic3r::fill_density;
            my $flow_width  = $Slic3r::flow_width;
            
            # force 100% density and rectilinear fill for external surfaces
            if ($surface->surface_type ne 'internal') {
                my $is_bridge = $layer->id > 0 && $surface->surface_type eq 'bottom';
                $density = 1;
                $filler = $is_bridge ? 'rectilinear' : $Slic3r::solid_fill_pattern;
                $flow_width = $Slic3r::nozzle_diameter if $is_bridge;
            } else {
                next SURFACE unless $density > 0;
            }
            
            my @paths = $self->fillers->{$filler}->fill_surface(
                $surface,
                density     => $density,
                flow_width  => $flow_width,
            );
            
            # save into layer
            push @{ $layer->fills }, Slic3r::ExtrusionPath::Collection->new(
                paths => [
                    map Slic3r::ExtrusionPath->cast(
                        [ @$_ ],
                        depth_layers => $surface->depth_layers,
                    ), @paths,
                ],
            );
            $layer->fills->[-1]->cleanup;
        }
    }
}

1;
