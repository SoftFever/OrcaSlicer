package Slic3r::Layer::Region;
use strict;
use warnings;

use Slic3r::ExtrusionPath ':roles';
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(scale);
use Slic3r::Geometry::Clipper qw(diff_ex intersection_ex 
    );
use Slic3r::Surface ':types';


# TODO: lazy
sub infill_area_threshold {
    my $self = shift;
    return $self->flow(FLOW_ROLE_SOLID_INFILL)->scaled_spacing ** 2;
}

sub id      { return $_[0]->layer->id; }
sub slice_z { return $_[0]->layer->slice_z; }
sub print_z { return $_[0]->layer->print_z; }
sub height  { return $_[0]->layer->height; }
sub object  { return $_[0]->layer->object; }
sub print   { return $_[0]->layer->print; }

sub config  { return $_[0]->region->config; }

sub make_perimeters {
    my ($self, $slices, $fill_surfaces) = @_;
    
    $self->perimeters->clear;
    $self->thin_fills->clear;
    
    my $generator = Slic3r::Layer::PerimeterGenerator->new(
        # input:
        config              => $self->config,
        object_config       => $self->layer->object->config,
        print_config        => $self->layer->print->config,
        layer_height        => $self->height,
        layer_id            => $self->layer->id,
        slices              => $slices,
        lower_slices        => defined($self->layer->lower_layer) ? $self->layer->lower_layer->slices : undef,
        perimeter_flow      => $self->flow(FLOW_ROLE_PERIMETER),
        ext_perimeter_flow  => $self->flow(FLOW_ROLE_EXTERNAL_PERIMETER),
        overhang_flow       => $self->region->flow(FLOW_ROLE_PERIMETER, -1, 1, 0, -1, $self->layer->object),
        solid_infill_flow   => $self->flow(FLOW_ROLE_SOLID_INFILL),
        
        # output:
        loops               => $self->perimeters,
        gap_fill            => $self->thin_fills,
        fill_surfaces       => $fill_surfaces,
    );
    $generator->process;
}

sub process_external_surfaces {
    my ($self, $lower_layer) = @_;
    
    my @surfaces = @{$self->fill_surfaces};
    my $margin = scale &Slic3r::EXTERNAL_INFILL_MARGIN;
    
    my @bottom = ();
    foreach my $surface (grep $_->is_bottom, @surfaces) {
        my $grown = $surface->expolygon->offset_ex(+$margin);
        
        # detect bridge direction before merging grown surfaces otherwise adjacent bridges
        # would get merged into a single one while they need different directions
        # also, supply the original expolygon instead of the grown one, because in case
        # of very thin (but still working) anchors, the grown expolygon would go beyond them
        my $angle;
        if ($lower_layer) {
            my $bridge_detector = Slic3r::BridgeDetector->new(
                $surface->expolygon,
                $lower_layer->slices,
                $self->flow(FLOW_ROLE_INFILL, $self->height, 1)->scaled_width,
            );
            Slic3r::debugf "Processing bridge at layer %d:\n", $self->id;
            $bridge_detector->detect_angle;
            $angle = $bridge_detector->angle;
            
            if (defined $angle && $self->object->config->support_material) {
                $self->bridged->append(Slic3r::ExPolygon->new($_))
                    for @{ $bridge_detector->coverage_by_angle($angle) };
                $self->unsupported_bridge_edges->append($_) for @{ $bridge_detector->unsupported_edges }; 
            }
        }
        
        push @bottom, map $surface->clone(expolygon => $_, bridge_angle => $angle), @$grown;
    }
    
    my @top = ();
    foreach my $surface (grep $_->surface_type == S_TYPE_TOP, @surfaces) {
        # give priority to bottom surfaces
        my $grown = diff_ex(
            $surface->expolygon->offset(+$margin),
            [ map $_->p, @bottom ],
        );
        push @top, map $surface->clone(expolygon => $_), @$grown;
    }
    
    # if we're slicing with no infill, we can't extend external surfaces
    # over non-existent infill
    my @fill_boundaries = $self->config->fill_density > 0
        ? @surfaces
        : grep $_->surface_type != S_TYPE_INTERNAL, @surfaces;
    
    # intersect the grown surfaces with the actual fill boundaries
    my @new_surfaces = ();
    foreach my $group (@{Slic3r::Surface::Collection->new(@top, @bottom)->group}) {
        push @new_surfaces,
            map $group->[0]->clone(expolygon => $_),
            @{intersection_ex(
                [ map $_->p, @$group ],
                [ map $_->p, @fill_boundaries ],
                1,  # to ensure adjacent expolygons are unified
            )};
    }
    
    # subtract the new top surfaces from the other non-top surfaces and re-add them
    my @other = grep $_->surface_type != S_TYPE_TOP && !$_->is_bottom, @surfaces;
    foreach my $group (@{Slic3r::Surface::Collection->new(@other)->group}) {
        push @new_surfaces, map $group->[0]->clone(expolygon => $_), @{diff_ex(
            [ map $_->p, @$group ],
            [ map $_->p, @new_surfaces ],
        )};
    }
    $self->fill_surfaces->clear;
    $self->fill_surfaces->append($_) for @new_surfaces;
}

1;
