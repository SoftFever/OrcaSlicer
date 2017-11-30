package Slic3r::Print::Object;
# extends c++ class Slic3r::PrintObject (Print.xsp)
use strict;
use warnings;

use List::Util qw(min max sum first);
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(scale epsilon);
use Slic3r::Geometry::Clipper qw(diff diff_ex intersection intersection_ex union union_ex 
    offset offset2 offset_ex offset2_ex JT_MITER);
use Slic3r::Print::State ':steps';
use Slic3r::Surface ':types';

sub layers {
    my $self = shift;
    return [ map $self->get_layer($_), 0..($self->layer_count - 1) ];
}

sub support_layers {
    my $self = shift;
    return [ map $self->get_support_layer($_), 0..($self->support_layer_count - 1) ];
}

# 1) Decides Z positions of the layers,
# 2) Initializes layers and their regions
# 3) Slices the object meshes
# 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
# 5) Applies size compensation (offsets the slices in XY plane)
# 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
# Resulting expolygons of layer regions are marked as Internal.
#
# this should be idempotent
sub slice {
    my $self = shift;
    
    return if $self->step_done(STEP_SLICE);
    $self->set_step_started(STEP_SLICE);
    $self->print->status_cb->(10, "Processing triangulated mesh");
    
    $self->_slice;

    my $warning = $self->_fix_slicing_errors;
    warn $warning if (defined($warning) && $warning ne '');

    # simplify slices if required
    $self->_simplify_slices(scale($self->print->config->resolution))
        if ($self->print->config->resolution);
    
    die "No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n"
        if !@{$self->layers};
    
    $self->set_step_done(STEP_SLICE);
}

# 1) Merges typed region slices into stInternal type.
# 2) Increases an "extra perimeters" counter at region slices where needed.
# 3) Generates perimeters, gap fills and fill regions (fill regions of type stInternal).
sub make_perimeters {
    my ($self) = @_;
    
    # prerequisites
    $self->slice;

    if (! $self->step_done(STEP_PERIMETERS)) {
        $self->print->status_cb->(20, "Generating perimeters");
        $self->_make_perimeters;
    }
}

sub prepare_infill {
    my ($self) = @_;
    
    # prerequisites
    $self->make_perimeters;
    
    return if $self->step_done(STEP_PREPARE_INFILL);
    $self->set_step_started(STEP_PREPARE_INFILL);
    $self->print->status_cb->(30, "Preparing infill");
    
    $self->_prepare_infill;

    $self->set_step_done(STEP_PREPARE_INFILL);
}

sub infill {
    my ($self) = @_;
    
    # prerequisites
    $self->prepare_infill;
    $self->_infill;
}

sub generate_support_material {
    my $self = shift;
    
    # prerequisites
    $self->slice;
    
    return if $self->step_done(STEP_SUPPORTMATERIAL);
    $self->set_step_started(STEP_SUPPORTMATERIAL);
    
    $self->clear_support_layers;
    
    if (($self->config->support_material || $self->config->raft_layers > 0) && scalar(@{$self->layers}) > 1) {
        $self->print->status_cb->(85, "Generating support material");    
        # New supports, C++ implementation.
        $self->_generate_support_material;
    }
    
    $self->set_step_done(STEP_SUPPORTMATERIAL);
    my $stats = sprintf "Weight: %.1fg, Cost: %.1f" , $self->print->total_weight, $self->print->total_cost;
    $self->print->status_cb->(85, $stats);
}

1;
