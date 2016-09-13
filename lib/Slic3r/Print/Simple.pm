# A simple wrapper to quickly print a single model without a GUI.
# Used by the command line slic3r.pl, by command line utilities pdf-slic3s.pl and view-toolpaths.pl,
# and by the quick slice menu of the Slic3r GUI.
#
# It creates and owns an instance of Slic3r::Print  to perform the slicing
# and it accepts an instance of Slic3r::Model from the outside.

package Slic3r::Print::Simple;
use Moo;

use Slic3r::Geometry qw(X Y);

has '_print' => (
    is      => 'ro',
    default => sub { Slic3r::Print->new },
    handles => [qw(apply_config extruders expanded_output_filepath
                    total_used_filament total_extruded_volume
                    placeholder_parser process)],
);

has 'duplicate' => (
    is      => 'rw',
    default => sub { 1 },
);

has 'scale' => (
    is      => 'rw',
    default => sub { 1 },
);

has 'rotate' => (
    is      => 'rw',
    default => sub { 0 },
);

has 'duplicate_grid' => (
    is      => 'rw',
    default => sub { [1,1] },
);

has 'status_cb' => (
    is      => 'rw',
    default => sub { sub {} },
);

has 'print_center' => (
    is      => 'rw',
    default => sub { Slic3r::Pointf->new(100,100) },
);

has 'dont_arrange' => (
    is      => 'rw',
    default => sub { 0 },
);

has 'output_file' => (
    is      => 'rw',
);

sub set_model {
    # $model is of type Slic3r::Model
    my ($self, $model) = @_;
    
    # make method idempotent so that the object is reusable
    $self->_print->clear_objects;
    
    # make sure all objects have at least one defined instance
    my $need_arrange = $model->add_default_instances && ! $self->dont_arrange;
    
    # apply scaling and rotation supplied from command line if any
    foreach my $instance (map @{$_->instances}, @{$model->objects}) {
        $instance->set_scaling_factor($instance->scaling_factor * $self->scale);
        $instance->set_rotation($instance->rotation + $self->rotate);
    }
    
    if ($self->duplicate_grid->[X] > 1 || $self->duplicate_grid->[Y] > 1) {
        $model->duplicate_objects_grid($self->duplicate_grid->[X], $self->duplicate_grid->[Y], $self->_print->config->duplicate_distance);
    } elsif ($need_arrange) {
        $model->duplicate_objects($self->duplicate, $self->_print->config->min_object_distance);
    } elsif ($self->duplicate > 1) {
        # if all input objects have defined position(s) apply duplication to the whole model
        $model->duplicate($self->duplicate, $self->_print->config->min_object_distance);
    }
    $_->translate(0,0,-$_->bounding_box->z_min) for @{$model->objects};
    $model->center_instances_around_point($self->print_center) if (! $self->dont_arrange);
    
    foreach my $model_object (@{$model->objects}) {
        $self->_print->auto_assign_extruders($model_object);
        $self->_print->add_model_object($model_object);
    }
}

sub _before_export {
    my ($self) = @_;
    
    $self->_print->set_status_cb($self->status_cb);
    $self->_print->validate;
}

sub _after_export {
    my ($self) = @_;
    
    $self->_print->set_status_cb(undef);
}

sub export_gcode {
    my ($self) = @_;
    
    $self->_before_export;
    $self->_print->export_gcode(output_file => $self->output_file);
    $self->_after_export;
}

sub export_svg {
    my ($self) = @_;
    
    $self->_before_export;
    
    $self->_print->export_svg(output_file => $self->output_file);
    
    $self->_after_export;
}

1;
