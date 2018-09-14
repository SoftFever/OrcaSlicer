# The slicing work horse.
# Extends C++ class Slic3r::Print
package Slic3r::Print;
use strict;
use warnings;

use File::Basename qw(basename fileparse);
use File::Spec;
use List::Util qw(min max first sum);
use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(X Y unscale);
use Slic3r::Geometry::Clipper qw(diff_ex union_ex intersection_ex intersection offset
    union JT_ROUND JT_SQUARE);
use Slic3r::Print::State ':steps';

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

sub run_post_process_scripts {
    my ($self, $output_file) = @_;
    # run post-processing scripts
    if (@{$self->config->post_process}) {
#        $self->set_status(95, "Running post-processing scripts");
        $self->config->setenv;
        for my $script (@{$self->config->post_process}) {
            # Ignore empty post processing script lines.
            next if $script =~ /^\s*$/;
            Slic3r::debugf "  '%s' '%s'\n", $script, $output_file;
            # -x doesn't return true on Windows except for .exe files
            if (($^O eq 'MSWin32') ? !(-e $script) : !(-x $script)) {
                die "The configured post-processing script is not executable: check permissions. ($script)\n";
            }
            if ($^O eq 'MSWin32' && $script =~ /\.[pP][lL]/) {
                # The current process (^X) may be slic3r.exe or slic3r-console.exe.
                # Replace it with the current perl interpreter.
                my($filename, $directories, $suffix) = fileparse($^X);
                $filename =~ s/^slic3r.*$/perl5\.24\.0\.exe/;
                my $interpreter = $directories . $filename;
                system($interpreter, $script, $output_file);
            } else {
                system($script, $output_file);
            }
        }
    }
}

sub export_png {
    my $self = shift;
    my %params = @_;

    my @sobjects =  @{$self->objects};
    my $objnum = scalar @sobjects;
    for(my $oi = 0; $oi < $objnum; $oi++)
    { 
        $sobjects[$oi]->slice;
        $self->set_status(($oi + 1)*100/$objnum - 1, "Slicing...");
    }

    my $fh = $params{output_file};
    $self->set_status(90, "Exporting zipped archive...");
    $self->print_to_png($fh);
    $self->set_status(100, "Done.");
}

1;
