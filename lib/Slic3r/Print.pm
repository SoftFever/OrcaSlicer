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
#        $self->status_cb->(95, "Running post-processing scripts");
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
        $self->status_cb->(($oi + 1)*100/$objnum - 1, "Slicing...");
    }

    my $fh = $params{output_file};
    $self->status_cb->(90, "Exporting zipped archive...");
    $self->print_to_png($fh);
    $self->status_cb->(100, "Done.");
}

# Export SVG slices for the offline SLA printing.
# The export_svg is expected to be executed inside an eval block.
sub export_svg {
    my $self = shift;
    my %params = @_;
    
    my @sobjects =  @{$self->objects};
    my $objnum = scalar @sobjects;
    for(my $oi = 0; $oi < $objnum; $oi++)
    { 
        $sobjects[$oi]->slice;
        $self->status_cb->(($oi + 1)*100/$objnum - 1, "Slicing...");
    }
    
    my $fh = $params{output_fh};
    if (!$fh) {
        # The following line may die if the output_filename_format template substitution fails.
        my $output_file = $self->output_filepath($params{output_file});
        $output_file =~ s/\.[gG][cC][oO][dD][eE]$/.svg/;
        Slic3r::open(\$fh, ">", $output_file) or die "Failed to open $output_file for writing\n";
        print "Exporting to $output_file..." unless $params{quiet};
    }
    
    my $print_bb = $self->bounding_box;
    my $print_size = $print_bb->size;
    print $fh sprintf <<"EOF", unscale($print_size->[X]), unscale($print_size->[Y]);
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">
<svg width="%s" height="%s" xmlns="http://www.w3.org/2000/svg" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:slic3r="http://slic3r.org/namespaces/slic3r">
  <!-- 
  Generated using Slic3r $Slic3r::VERSION
  http://slic3r.org/
   -->
EOF
    
    my $print_polygon = sub {
        my ($polygon, $type) = @_;
        printf $fh qq{    <polygon slic3r:type="%s" points="%s" style="fill: %s" />\n},
            $type, (join ' ', map { join ',', map unscale $_, @$_ } @$polygon),
            ($type eq 'contour' ? 'white' : 'black');
    };
    
    my @layers = sort { $a->print_z <=> $b->print_z }
        map { @{$_->layers}, @{$_->support_layers} }
        @{$self->objects};
    
    my $layer_id = -1;
    my @previous_layer_slices = ();
    for my $layer (@layers) {
        $layer_id++;
        if ($layer->slice_z == -1) {
            printf $fh qq{  <g id="layer%d">\n}, $layer_id;
        } else {
            printf $fh qq{  <g id="layer%d" slic3r:z="%s">\n}, $layer_id, unscale($layer->slice_z);
        }
        
        my @current_layer_slices = ();
        # sort slices so that the outermost ones come first
        my @slices = sort { $a->contour->contains_point($b->contour->first_point) ? 0 : 1 } @{$layer->slices};
        foreach my $copy (@{$layer->object->_shifted_copies}) {
            foreach my $slice (@slices) {
                my $expolygon = $slice->clone;
                $expolygon->translate(@$copy);
                $expolygon->translate(-$print_bb->x_min, -$print_bb->y_min);
                $print_polygon->($expolygon->contour, 'contour');
                $print_polygon->($_, 'hole') for @{$expolygon->holes};
                push @current_layer_slices, $expolygon;
            }
        }
        # generate support material
        if ($self->has_support_material && $layer->id > 0) {
            my (@supported_slices, @unsupported_slices) = ();
            foreach my $expolygon (@current_layer_slices) {
                my $intersection = intersection_ex(
                    [ map @$_, @previous_layer_slices ],
                    [ @$expolygon ],
                );
                @$intersection
                    ? push @supported_slices, $expolygon
                    : push @unsupported_slices, $expolygon;
            }
            my @supported_points = map @$_, @$_, @supported_slices;
            foreach my $expolygon (@unsupported_slices) {
                # look for the nearest point to this island among all
                # supported points
                my $contour = $expolygon->contour;
                my $support_point = $contour->first_point->nearest_point(\@supported_points)
                    or next;
                my $anchor_point = $support_point->nearest_point([ @$contour ]);
                printf $fh qq{    <line x1="%s" y1="%s" x2="%s" y2="%s" style="stroke-width: 2; stroke: white" />\n},
                    map @$_, $support_point, $anchor_point;
            }
        }
        print $fh qq{  </g>\n};
        @previous_layer_slices = @current_layer_slices;
    }
    
    print $fh "</svg>\n";
    close $fh;
    print "Done.\n" unless $params{quiet};
}

1;
