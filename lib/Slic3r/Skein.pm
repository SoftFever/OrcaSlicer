package Slic3r::Skein;
use Moo;

use Time::HiRes qw(gettimeofday tv_interval);
use XXX;

has 'input_file'    => (is => 'ro', required => 1);
has 'output_file'   => (is => 'rw', required => 0);

sub go {
    my $self = shift;
    
    die "Input file must have .stl extension\n" 
        if $self->input_file !~ /\.stl$/i;
    
    my $t0 = [gettimeofday];
    
    # skein the STL into layers
    # each layer has surfaces with holes
    my $print = Slic3r::Print->new_from_stl($self->input_file);
    
    # make skirt
    $print->extrude_skirt;
    
    # make perimeters
    # this will add a set of extrusion loops to each layer
    # as well as generate infill boundaries
    {
        my $perimeter_maker = Slic3r::Perimeter->new;
        $perimeter_maker->make_perimeter($_) for @{$print->layers};
    }
    
    # this will prepare surfaces for perimeters by merging all
    # surfaces in each layer; it will also clip $layer->surfaces 
    # to infill boundaries and split them in top/bottom/internal surfaces
    $print->detect_surfaces_type;
    
    # this will remove unprintable surfaces
    # (those that are too tight for extrusion)
    $_->remove_small_surfaces for @{$print->layers};
    
    # this will detect bridges and reverse bridges
    # and rearrange top/bottom/internal surfaces
    $_->process_bridges for @{$print->layers};
    
    # this will remove unprintable perimeter loops
    # (those that are too tight for extrusion)
    $_->remove_small_perimeters for @{$print->layers};
    
    # detect which fill surfaces are near external layers
    # they will be split in internal and internal-solid surfaces
    $print->discover_horizontal_shells;
    
    # combine fill surfaces to honor the "infill every N layers" option
    $print->infill_every_layers;
    
    # this will generate extrusion paths for each layer
    {
        my $fill_maker = Slic3r::Fill->new('print' => $print);
        $fill_maker->make_fill($_) for @{$print->layers};
    }
    
    # output everything to a GCODE file
    if (!$self->output_file) {
        my $output_file = $self->input_file;
        $output_file =~ s/\.stl$/.gcode/i;
        $self->output_file($output_file);
    }
    $print->export_gcode($self->output_file);
    
    # output some statistics
    my $processing_time = tv_interval($t0);
    printf "Done. Process took %d minutes and %.3f seconds\n", 
        int($processing_time/60), $processing_time - int($processing_time/60)*60;
    
    # TODO: more statistics!
}

1;
