package Slic3r::Fill;
use Moo;

use Slic3r::Fill::Base;
use Slic3r::Fill::Rectilinear;
use Slic3r::Fill::Rectilinear2;

has 'print'     => (is => 'ro', required => 1);
has 'fillers'   => (is => 'rw', default => sub { {} });

our %FillTypes = (
    rectilinear  => 'Slic3r::Fill::Rectilinear',
    rectilinear2 => 'Slic3r::Fill::Rectilinear2',
);

sub BUILD {
    my $self = shift;
    $self->fillers->{$_} ||= $FillTypes{$_}->new(print => $self->print)
        for ('rectilinear', $Slic3r::fill_type);
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
    foreach my $surface_collection (@{ $layer->fill_surfaces }) {
        my @path_collection = ();
        
        SURFACE: foreach my $surface (@{ $surface_collection->surfaces }) {
            Slic3r::debugf " Processing surface %s:\n", $surface->id;
            
            my $filler = $Slic3r::fill_type;
            my $density = $Slic3r::fill_density;
            next SURFACE unless $density > 0;
            
            # force 100% density and rectilinear fill for external surfaces
            if ($surface->surface_type ne 'internal') {
                $density = 1;
                $filler = 'rectilinear';
            }
            
            push @path_collection, $self->fillers->{$filler}->fill_surface($surface,
                density => $density,
            );
        }
        
        # save into layer
        push @{ $layer->fills }, Slic3r::ExtrusionPath::Collection->new(
            paths => [ map Slic3r::ExtrusionPath->cast([ @$_ ]), @path_collection ],
        );
        $layer->fills->[-1]->cleanup;
    }
}

1;
