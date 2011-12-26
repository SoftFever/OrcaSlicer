package Slic3r::Skein;
use Moo;

use File::Basename qw(basename fileparse);
use Slic3r::Geometry qw(PI);
use Time::HiRes qw(gettimeofday tv_interval);
use XXX;

# full path (relative or absolute) to the input file
has 'input_file'    => (is => 'ro', required => 1);

# full path (relative or absolute) to the output file; it may contain
# formatting variables like [layer_height] etc.
has 'output_file'   => (is => 'rw', required => 0);

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
    $print->export_gcode($self->expanded_output_filepath);
    
    # output some statistics
    $self->processing_time(tv_interval($t0));
    printf "Done. Process took %d minutes and %.3f seconds\n", 
        int($self->processing_time/60),
        $self->processing_time - int($self->processing_time/60)*60;
    
    # TODO: more statistics!
    printf "Filament required: %.1fmm (%.1fcm3)\n",
        $print->total_extrusion_length, $print->total_extrusion_volume;
}

# this method will return the value of $self->output_file after expanding its
# format variables with their values
sub expanded_output_filepath {
    my $self = shift;
    
    my $path = $self->output_file;
    
    # if no explicit output file was defined, we take the input
    # file directory and append the specified filename format
    $path ||= (fileparse($self->input_file))[1] . $Slic3r::output_filename_format;
    
    my $input_basename = basename($self->input_file);
    $path =~ s/\[input_filename\]/$input_basename/g;  
    $input_basename =~ s/\.stl$//i;
    $path =~ s/\[input_filename_base\]/$input_basename/g;
    
    # build a regexp to match the available options
    my $options = join '|',
        grep !$Slic3r::Config::Options->{$_}{multiline},
        keys %$Slic3r::Config::Options;
    
    # use that regexp to search and replace option names with option values
    $path =~ s/\[($options)\]/Slic3r::Config->serialize($1)/eg;
    return $path;
}

1;
