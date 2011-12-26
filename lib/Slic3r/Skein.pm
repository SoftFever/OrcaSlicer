package Slic3r::Skein;
use Moo;

use Slic3r::Geometry qw(PI);
use Time::HiRes qw(gettimeofday tv_interval);
use XXX;

has 'input_file'    => (is => 'ro', required => 1);
has 'output_file'    => (is => 'rw', required => 0);
has 'status_cb'     => (is => 'rw', required => 0, default => sub { sub {} });
has 'processing_time' => (is => 'rw', required => 0);

sub go {
    my $self = shift;
    
    die "Input file must have .stl extension\n" 
        if $self->input_file !~ /\.stl$/i;
    
    my $t0 = [gettimeofday];
    
    # skein the STL into layers
    # each layer has surfaces with holes
    $self->status_cb->(10, "Processing triangulated mesh...");
    my $print;
    {
        my $mesh = Slic3r::STL->read_file($self->input_file);
        $mesh->check_manifoldness;
        $print = Slic3r::Print->new_from_mesh($mesh);
    }
    
    # make skirt
    $self->status_cb->(15, "Generating skirt...");
    $print->extrude_skirt;
    
    # make perimeters
    # this will add a set of extrusion loops to each layer
    # as well as generate infill boundaries
    $self->status_cb->(20, "Generating perimeters...");
    {
        my $perimeter_maker = Slic3r::Perimeter->new;
        $perimeter_maker->make_perimeter($_) for @{$print->layers};
    }
    
    # this will clip $layer->surfaces to the infill boundaries 
    # and split them in top/bottom/internal surfaces;
    $self->status_cb->(30, "Detecting solid surfaces...");
    $print->detect_surfaces_type;
    
    # decide what surfaces are to be filled
    $self->status_cb->(35, "Preparing infill surfaces...");
    $_->prepare_fill_surfaces for @{$print->layers};
    
    # this will remove unprintable surfaces
    # (those that are too tight for extrusion)
    $self->status_cb->(40, "Cleaning up...");
    $_->remove_small_surfaces for @{$print->layers};
    
    # this will detect bridges and reverse bridges
    # and rearrange top/bottom/internal surfaces
    $self->status_cb->(45, "Detect bridges...");
    $_->process_bridges for @{$print->layers};
    
    # this will remove unprintable perimeter loops
    # (those that are too tight for extrusion)
    $self->status_cb->(50, "Cleaning up the perimeters...");
    $_->remove_small_perimeters for @{$print->layers};
    
    # detect which fill surfaces are near external layers
    # they will be split in internal and internal-solid surfaces
    $self->status_cb->(60, "Generating horizontal shells...");
    $print->discover_horizontal_shells;
    
    # combine fill surfaces to honor the "infill every N layers" option
    $self->status_cb->(70, "Combining infill...");
    $print->infill_every_layers;
    
    # this will generate extrusion paths for each layer
    $self->status_cb->(80, "Infilling layers...");
    {
        my $fill_maker = Slic3r::Fill->new('print' => $print);
        $fill_maker->make_fill($_) for @{$print->layers};
    }
    
    # output everything to a GCODE file
    $self->status_cb->(90, "Exporting GCODE...");
    if ($self->output_file) {
        $print->export_gcode($self->output_file);
    } else {
        $print->export_gcode($self->get_output_filename);
    }
    
    # output some statistics
    $self->processing_time(tv_interval($t0));
    printf "Done. Process took %d minutes and %.3f seconds\n", 
        int($self->processing_time/60),
        $self->processing_time - int($self->processing_time/60)*60;
    
    # TODO: more statistics!
    printf "Filament required: %.1fmm (%.1fcm3)\n",
        $print->total_extrusion_length, $print->total_extrusion_volume;
}

sub get_output_filename {
    my $self = shift;
    my $filename = Slic3r::Config->get('output_filename_format');
    my %opts = %$Slic3r::Config::Options;
    my $input = $self->input_file;
    # these pseudo-options are the path and filename, without and with extension, of the input file
    $filename =~ s/\[input_filename\]/$input/g;
    $input =~ s/\.stl$//i;
    $filename =~ s/\[input_filename_base\]/$input/g;
    # make a list of options
    my $options = join '|', keys %$Slic3r::Config::Options;
    # use that list to search and replace option names with option values
    $filename =~ s/\[($options)\]/Slic3r::Config->get($1)/eg;
    return $filename;
}

1;
