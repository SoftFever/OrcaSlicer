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
    # each layer has surfaces with holes; surfaces are distinguished
    # in top/bottom/internal
    my $print = Slic3r::Print->new_from_stl($self->input_file);
    
    # this will remove unprintable surfaces
    # (those that are too tight for extrusion)
    $print->remove_small_surfaces;
    
    # make bridges printable
    # this will add a set of bridges to each layer
    $print->process_bridges;
    
    # make perimeters
    # this will add a set of extrusion loops to each layer
    # as well as a set of surfaces to be filled
    $print->extrude_perimeters;
    
    # this will remove unprintable perimeter loops
    # (those that are too tight for extrusion)
    $print->remove_small_perimeters;
    
    # split fill_surfaces in internal and bridge surfaces
    $print->split_bridges_fills;
    
    # detect which fill surfaces are near external layers
    # they will be split in internal and internal-solid surfaces
    $print->discover_horizontal_shells;
    
    # this will generate extrusion paths for each layer
    $print->extrude_fills;
    
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
