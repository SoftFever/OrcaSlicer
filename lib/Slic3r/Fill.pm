package Slic3r::Fill;
use Moo;

use Slic3r::Fill::ArchimedeanChords;
use Slic3r::Fill::Base;
use Slic3r::Fill::Flowsnake;
use Slic3r::Fill::HilbertCurve;
use Slic3r::Fill::OctagramSpiral;
use Slic3r::Fill::Rectilinear;
use Slic3r::Fill::Rectilinear2;

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
    foreach my $surfaces (@{ $layer->fill_surfaces }) {
        my @path_collection = ();
        
        SURFACE: foreach my $surface (@$surfaces) {
            Slic3r::debugf " Processing surface %s:\n", $surface->id;
            
            my $filler = $Slic3r::fill_pattern;
            my $density = $Slic3r::fill_density;
            
            # force 100% density and rectilinear fill for external surfaces
            if ($surface->surface_type ne 'internal') {
                $density = 1;
                $filler = $surface->isa('Slic3r::Surface::Bridge')
                    ? 'rectilinear'
                    : $Slic3r::solid_fill_pattern;
            } else {
                next SURFACE unless $density > 0;
            }
            
            my @paths = $self->fillers->{$filler}->fill_surface(
                $surface,
                density => $density,
            );
            
            push @path_collection, map Slic3r::ExtrusionPath->cast(
                [ @$_ ],
                depth_layers => $surface->depth_layers,
            ), @paths;
        }
        
        # save into layer
        push @{ $layer->fills }, Slic3r::ExtrusionPath::Collection->new(
            paths => [ @path_collection ],
        );
        $layer->fills->[-1]->cleanup;
    }
}

1;
