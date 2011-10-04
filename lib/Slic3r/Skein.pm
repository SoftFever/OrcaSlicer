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
    my $print = Slic3r::Print->new_from_stl($self->input_file);
    $print->extrude_perimeters;
    $print->remove_small_features;
    
    # detect which surfaces are near external layers
    $print->discover_horizontal_shells;
    
    $print->extrude_fills;
    
    
    if (!$self->output_file) {
        my $output_file = $self->input_file;
        $output_file =~ s/\.stl$/.gcode/i;
        $self->output_file($output_file);
    }
    $print->export_gcode($self->output_file);
    
    my $processing_time = tv_interval($t0);
    printf "Done. Process took %d minutes and %.3f seconds\n", 
        int($processing_time/60), $processing_time - int($processing_time/60)*60;
}

1;
