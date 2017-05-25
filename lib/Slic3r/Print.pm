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
use Slic3r::Geometry qw(X Y Z X1 Y1 X2 Y2 MIN MAX PI scale unscale convex_hull);
use Slic3r::Geometry::Clipper qw(diff_ex union_ex intersection_ex intersection offset
    offset2 union union_pt_chained JT_ROUND JT_SQUARE);
use Slic3r::Print::State ':steps';

our $status_cb;

sub set_status_cb {
    my ($class, $cb) = @_;
    $status_cb = $cb;
}

sub status_cb {
    return $status_cb // sub {};
}

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

# Slicing process, running at a background thread.
sub process {
    my ($self) = @_;
    
    Slic3r::trace(3, "Staring the slicing process.");
    $_->make_perimeters for @{$self->objects};
    
    $self->status_cb->(70, "Infilling layers");
    $_->infill for @{$self->objects};
    
    $_->generate_support_material for @{$self->objects};
    $self->make_skirt;
    $self->make_brim;  # must come after make_skirt
    $self->make_wipe_tower;
    
    # time to make some statistics
    if (0) {
        eval "use Devel::Size";
        print  "MEMORY USAGE:\n";
        printf "  meshes        = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->meshes), @{$self->objects})/1024/1024;
        printf "  layer slices  = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  region slices = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  perimeters    = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->perimeters), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  fills         = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->fills), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  print object  = %.1fMb\n", Devel::Size::total_size($self)/1024/1024;
    }
    if (0) {
        eval "use Slic3r::Test::SectionCut";
        Slic3r::Test::SectionCut->new(print => $self)->export_svg("section_cut.svg");
    }
    Slic3r::trace(3, "Slicing process finished.")
}

sub export_gcode {
    my $self = shift;
    my %params = @_;
    
    # prerequisites
    $self->process;
    
    # output everything to a G-code file
    my $output_file = $self->output_filepath($params{output_file} // '');
    $self->status_cb->(90, "Exporting G-code" . ($output_file ? " to $output_file" : ""));
    
    {
        # open output gcode file if we weren't supplied a file-handle
        my $tempfile = "$output_file.tmp";
        my $gcode    = Slic3r::GCode->new();
        my $result   = $gcode->do_export($self, Slic3r::encode_path($tempfile));
        die $result . "\n" if ($result ne '');
        my $i;
        for ($i = 0; $i < 5; $i += 1)  {
            last if (rename Slic3r::encode_path($tempfile), Slic3r::encode_path($output_file));
            # Wait for 1/4 seconds and try to rename once again.
            select(undef, undef, undef, 0.25);
        }
        Slic3r::debugf "Failed to remove the output G-code file from $tempfile to $output_file. Is $tempfile locked?\n" if ($i == 5);
    }
    
    # run post-processing scripts
    if (@{$self->config->post_process}) {
        $self->status_cb->(95, "Running post-processing scripts");
        $self->config->setenv;
        for my $script (@{$self->config->post_process}) {
            Slic3r::debugf "  '%s' '%s'\n", $script, $output_file;
            # -x doesn't return true on Windows except for .exe files
            if (($^O eq 'MSWin32') ? !(-e $script) : !(-x $script)) {
                die "The configured post-processing script is not executable: check permissions. ($script)\n";
            }
            system($script, $output_file);
        }
    }
}

# Export SVG slices for the offline SLA printing.
sub export_svg {
    my $self = shift;
    my %params = @_;
    
    $_->slice for @{$self->objects};
    
    my $fh = $params{output_fh};
    if (!$fh) {
        my $output_file = $self->output_filepath($params{output_file});
        $output_file =~ s/\.gcode$/.svg/i;
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

sub make_skirt {
    my $self = shift;
    
    # prerequisites
    $_->make_perimeters for @{$self->objects};
    $_->infill for @{$self->objects};
    $_->generate_support_material for @{$self->objects};
    
    return if $self->step_done(STEP_SKIRT);

    $self->set_step_started(STEP_SKIRT);
    $self->skirt->clear;    
    if ($self->has_skirt) {
        $self->status_cb->(88, "Generating skirt");
        $self->_make_skirt();
    }
    $self->set_step_done(STEP_SKIRT);
}

sub make_brim {
    my $self = shift;
    
    # prerequisites
    $_->make_perimeters for @{$self->objects};
    $_->infill for @{$self->objects};
    $_->generate_support_material for @{$self->objects};
    $self->make_skirt;
    
    return if $self->step_done(STEP_BRIM);
    $self->set_step_started(STEP_BRIM);
    
    # since this method must be idempotent, we clear brim paths *before*
    # checking whether we need to generate them
    $self->brim->clear;
    
    if ($self->config->brim_width == 0) {
        $self->set_step_done(STEP_BRIM);
        return;
    }
    $self->status_cb->(88, "Generating brim");
    
    # brim is only printed on first layer and uses perimeter extruder
    my $first_layer_height = $self->skirt_first_layer_height;
    my $flow = $self->brim_flow;
    my $mm3_per_mm = $flow->mm3_per_mm;
    
    my $grow_distance = $flow->scaled_width / 2;
    my @islands = (); # array of polygons
    foreach my $obj_idx (0 .. ($self->object_count - 1)) {
        my $object = $self->objects->[$obj_idx];
        my $layer0 = $object->get_layer(0);
        my @object_islands = (
            (map $_->contour, @{$layer0->slices}),
        );
        if (@{ $object->support_layers }) {
            my $support_layer0 = $object->support_layers->[0];
            push @object_islands,
                (map @{$_->polyline->grow($grow_distance)}, @{$support_layer0->support_fills})
                if $support_layer0->support_fills;
        }
        foreach my $copy (@{$object->_shifted_copies}) {
            push @islands, map { $_->translate(@$copy); $_ } map $_->clone, @object_islands;
        }
    }
    
    my @loops = ();
    my $num_loops = sprintf "%.0f", $self->config->brim_width / $flow->width;
    for my $i (reverse 1 .. $num_loops) {
        # JT_SQUARE ensures no vertex is outside the given offset distance
        # -0.5 because islands are not represented by their centerlines
        # (first offset more, then step back - reverse order than the one used for 
        # perimeters because here we're offsetting outwards)
        push @loops, @{offset2(\@islands, ($i + 0.5) * $flow->scaled_spacing, -1.0 * $flow->scaled_spacing, JT_SQUARE)};
    }
    
    $self->brim->append(map Slic3r::ExtrusionLoop->new_from_paths(
        Slic3r::ExtrusionPath->new(
            polyline        => Slic3r::Polygon->new(@$_)->split_at_first_point,
            role            => EXTR_ROLE_SKIRT,
            mm3_per_mm      => $mm3_per_mm,
            width           => $flow->width,
            height          => $first_layer_height,
        ),
    ), reverse @{union_pt_chained(\@loops)});
    
    $self->set_step_done(STEP_BRIM);
}

sub make_wipe_tower {
    my $self = shift;
    
    # prerequisites
    $_->make_perimeters for @{$self->objects};
    $_->infill for @{$self->objects};
    $_->generate_support_material for @{$self->objects};
    $self->make_skirt;
    $self->make_brim;
    
    return if $self->step_done(STEP_WIPE_TOWER);
    
    $self->set_step_started(STEP_WIPE_TOWER);
    $self->_clear_wipe_tower;
    if ($self->has_wipe_tower) {
#       $self->status_cb->(95, "Generating wipe tower");
        $self->_make_wipe_tower;
    }
    $self->set_step_done(STEP_WIPE_TOWER);
}

# Wrapper around the C++ Slic3r::Print::validate()
# to produce a Perl exception without a hang-up on some Strawberry perls.
sub validate
{
    my $self = shift;
    my $err = $self->_validate;
    die $err . "\n" if (defined($err) && $err ne '');
}

1;
