package Slic3r::GUI::Plater::2D;
use strict;
use warnings;
use utf8;

use List::Util qw(min max first);
use Slic3r::Geometry qw(X Y scale unscale convex_hull);
use Slic3r::Geometry::Clipper qw(offset JT_ROUND intersection_pl);
use Wx qw(:misc :pen :brush :sizer :font :cursor wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_MOUSE_EVENTS EVT_PAINT EVT_SIZE);
use base 'Wx::Panel';

use constant CANVAS_TEXT => join('-', +(localtime)[3,4]) eq '13-8'
    ? 'What do you want to print today? ™' # Sept. 13, 2006. The first part ever printed by a RepRap to make another RepRap.
    : 'Drag your objects here';

sub new {
    my $class = shift;
    my ($parent, $size, $objects, $model, $config) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, $size, wxTAB_TRAVERSAL);
    $self->SetBackgroundColour(Wx::wxWHITE);
    
    $self->{objects}            = $objects;
    $self->{model}              = $model;
    $self->{config}             = $config;
    $self->{on_select_object}   = sub {};
    $self->{on_double_click}    = sub {};
    $self->{on_right_click}     = sub {};
    $self->{on_instances_moved} = sub {};
    
    $self->{objects_brush}      = Wx::Brush->new(Wx::Colour->new(210,210,210), wxSOLID);
    $self->{selected_brush}     = Wx::Brush->new(Wx::Colour->new(255,128,128), wxSOLID);
    $self->{dragged_brush}      = Wx::Brush->new(Wx::Colour->new(128,128,255), wxSOLID);
    $self->{transparent_brush}  = Wx::Brush->new(Wx::Colour->new(0,0,0), wxTRANSPARENT);
    $self->{grid_pen}           = Wx::Pen->new(Wx::Colour->new(230,230,230), 1, wxSOLID);
    $self->{print_center_pen}   = Wx::Pen->new(Wx::Colour->new(200,200,200), 1, wxSOLID);
    $self->{clearance_pen}      = Wx::Pen->new(Wx::Colour->new(0,0,200), 1, wxSOLID);
    $self->{skirt_pen}          = Wx::Pen->new(Wx::Colour->new(150,150,150), 1, wxSOLID);
    
    EVT_PAINT($self, \&repaint);
    EVT_MOUSE_EVENTS($self, \&mouse_event);
    EVT_SIZE($self, sub {
        $self->update_bed_size;
        $self->Refresh;
    });
    
    return $self;
}

sub on_select_object {
    my ($self, $cb) = @_;
    $self->{on_select_object} = $cb;
}

sub on_double_click {
    my ($self, $cb) = @_;
    $self->{on_double_click} = $cb;
}

sub on_right_click {
    my ($self, $cb) = @_;
    $self->{on_right_click} = $cb;
}

sub on_instances_moved {
    my ($self, $cb) = @_;
    $self->{on_instances_moved} = $cb;
}

sub repaint {
    my ($self, $event) = @_;
    
    my $dc = Wx::PaintDC->new($self);
    my $size = $self->GetSize;
    my @size = ($size->GetWidth, $size->GetHeight);
    
    # draw grid
    $dc->SetPen($self->{grid_pen});
    $dc->DrawLine(map @$_, @$_) for @{$self->{grid}};
    
    # draw bed
    {
        $dc->SetPen($self->{print_center_pen});
        $dc->SetBrush($self->{transparent_brush});
        $dc->DrawPolygon($self->scaled_points_to_pixel($self->{bed_polygon}, 1), 0, 0);
    }
    
    # draw print center
    if (@{$self->{objects}} && $Slic3r::GUI::Settings->{_}{autocenter}) {
        my $center = $self->unscaled_point_to_pixel($self->{print_center});
        $dc->SetPen($self->{print_center_pen});
        $dc->DrawLine($center->[X], 0, $center->[X], $size[Y]);
        $dc->DrawLine(0, $center->[Y], $size[X], $center->[Y]);
        $dc->SetTextForeground(Wx::Colour->new(0,0,0));
        $dc->SetFont(Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL));
        $dc->DrawLabel("X = " . sprintf('%.0f', $self->{print_center}->[X]), Wx::Rect->new(0, 0, $center->[X]*2, $self->GetSize->GetHeight), wxALIGN_CENTER_HORIZONTAL | wxALIGN_BOTTOM);
        $dc->DrawRotatedText("Y = " . sprintf('%.0f', $self->{print_center}->[Y]), 0, $center->[Y]+15, 90);
    }
    
    # draw frame
    if (0) {
        $dc->SetPen(wxBLACK_PEN);
        $dc->SetBrush($self->{transparent_brush});
        $dc->DrawRectangle(0, 0, @size);
    }
    
    # draw text if plate is empty
    if (!@{$self->{objects}}) {
        $dc->SetTextForeground(Wx::Colour->new(150,50,50));
        $dc->SetFont(Wx::Font->new(14, wxDEFAULT, wxNORMAL, wxNORMAL));
        $dc->DrawLabel(CANVAS_TEXT, Wx::Rect->new(0, 0, $self->GetSize->GetWidth, $self->GetSize->GetHeight), wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
    }
    
    # draw thumbnails
    $dc->SetPen(wxBLACK_PEN);
    $self->clean_instance_thumbnails;
    for my $obj_idx (0 .. $#{$self->{objects}}) {
        my $object = $self->{objects}[$obj_idx];
        my $model_object = $self->{model}->objects->[$obj_idx];
        next unless defined $object->thumbnail;
        for my $instance_idx (0 .. $#{$model_object->instances}) {
            my $instance = $model_object->instances->[$instance_idx];
            next if !defined $object->transformed_thumbnail;
            
            my $thumbnail = $object->transformed_thumbnail->clone;      # in scaled model coordinates
            $thumbnail->translate(map scale($_), @{$instance->offset});
            
            $object->instance_thumbnails->[$instance_idx] = $thumbnail;
            
            if (defined $self->{drag_object} && $self->{drag_object}[0] == $obj_idx && $self->{drag_object}[1] == $instance_idx) {
                $dc->SetBrush($self->{dragged_brush});
            } elsif ($object->selected) {
                $dc->SetBrush($self->{selected_brush});
            } else {
                $dc->SetBrush($self->{objects_brush});
            }
            foreach my $expolygon (@$thumbnail) {
                foreach my $points (@{$expolygon->pp}) {
                    $dc->DrawPolygon($self->scaled_points_to_pixel($points, 1), 0, 0);
                }
            }
            
            if (0) {
                # draw bounding box for debugging purposes
                my $bb = $model_object->instance_bounding_box($instance_idx);
                $bb->scale($self->{scaling_factor});
                # no need to translate by instance offset because instance_bounding_box() does that
                my $points = $bb->polygon->pp;
                $dc->SetPen($self->{clearance_pen});
                $dc->SetBrush($self->{transparent_brush});
                $dc->DrawPolygon($self->_y($points), 0, 0);
            }
            
            # if sequential printing is enabled and we have more than one object, draw clearance area
            if ($self->{config}->complete_objects && (map @{$_->instances}, @{$self->{model}->objects}) > 1) {
                my ($clearance) = @{offset([$thumbnail->convex_hull], (scale($self->{config}->extruder_clearance_radius) / 2), 1, JT_ROUND, scale(0.1))};
                $dc->SetPen($self->{clearance_pen});
                $dc->SetBrush($self->{transparent_brush});
                $dc->DrawPolygon($self->scaled_points_to_pixel($clearance, 1), 0, 0);
            }
        }
    }
    
    # draw skirt
    if (@{$self->{objects}} && $self->{config}->skirts) {
        my @points = map @{$_->contour}, map @$_, map @{$_->instance_thumbnails}, @{$self->{objects}};
        if (@points >= 3) {
            my ($convex_hull) = @{offset([convex_hull(\@points)], scale max($self->{config}->brim_width + $self->{config}->skirt_distance), 1, JT_ROUND, scale(0.1))};
            $dc->SetPen($self->{skirt_pen});
            $dc->SetBrush($self->{transparent_brush});
            $dc->DrawPolygon($self->scaled_points_to_pixel($convex_hull, 1), 0, 0);
        }
    }
    
    $event->Skip;
}

sub mouse_event {
    my ($self, $event) = @_;
    
    my $pos = $event->GetPosition;
    my $point = $self->point_to_model_units([ $pos->x, $pos->y ]);  #]]
    if ($event->ButtonDown) {
        $self->{on_select_object}->(undef);
        # traverse objects and instances in reverse order, so that if they're overlapping
        # we get the one that gets drawn last, thus on top (as user expects that to move)
        OBJECTS: for my $obj_idx (reverse 0 .. $#{$self->{objects}}) {
            my $object = $self->{objects}->[$obj_idx];
            for my $instance_idx (reverse 0 .. $#{ $object->instance_thumbnails }) {
                my $thumbnail = $object->instance_thumbnails->[$instance_idx];
                if (defined first { $_->contour->contains_point($point) } @$thumbnail) {
                    $self->{on_select_object}->($obj_idx);
                    
                    if ($event->LeftDown) {
                        # start dragging
                        my $instance = $self->{model}->objects->[$obj_idx]->instances->[$instance_idx];
                        my $instance_origin = [ map scale($_), @{$instance->offset} ];
                        $self->{drag_start_pos} = [   # displacement between the click and the instance origin in scaled model units
                            $point->x - $instance_origin->[X],
                            $point->y - $instance_origin->[Y],  #-
                        ];
                        $self->{drag_object} = [ $obj_idx, $instance_idx ];
                    } elsif ($event->RightDown) {
                        $self->{on_right_click}->($pos);
                    }
                    
                    last OBJECTS;
                }
            }
        }
        $self->Refresh;
    } elsif ($event->LeftUp) {
        $self->{on_instances_moved}->()
            if $self->{drag_object};
        $self->{drag_start_pos} = undef;
        $self->{drag_object} = undef;
        $self->SetCursor(wxSTANDARD_CURSOR);
    } elsif ($event->LeftDClick) {
    	$self->{on_double_click}->();
    } elsif ($event->Dragging) {
        return if !$self->{drag_start_pos}; # concurrency problems
        my ($obj_idx, $instance_idx) = @{ $self->{drag_object} };
        my $model_object = $self->{model}->objects->[$obj_idx];
        $model_object->instances->[$instance_idx]->set_offset(
            Slic3r::Pointf->new(
                unscale($point->[X] - $self->{drag_start_pos}[X]),
                unscale($point->[Y] - $self->{drag_start_pos}[Y]),
            ));
        $model_object->update_bounding_box;
        $self->Refresh;
    } elsif ($event->Moving) {
        my $cursor = wxSTANDARD_CURSOR;
        if (defined first { $_->contour->contains_point($point) } map @$_, map @{$_->instance_thumbnails}, @{ $self->{objects} }) {
            $cursor = Wx::Cursor->new(wxCURSOR_HAND);
        }
        $self->SetCursor($cursor);
    }
}

sub update_bed_size {
    my $self = shift;
    
    # when the canvas is not rendered yet, its GetSize() method returns 0,0
    my $canvas_size = $self->GetSize;
    my ($canvas_w, $canvas_h) = ($canvas_size->GetWidth, $canvas_size->GetHeight);
    return if $canvas_w == 0;
    
    # get bed shape polygon
    $self->{bed_polygon} = my $polygon = Slic3r::Polygon->new_scale(@{$self->{config}->bed_shape});
    my $bb = $polygon->bounding_box;
    my $size = $bb->size;
    
    # calculate the scaling factor needed for constraining print bed area inside preview
    # scaling_factor is expressed in pixel / mm
    $self->{scaling_factor} = min($canvas_w / unscale($size->x), $canvas_h / unscale($size->y)); #)
    
    # calculate the displacement needed to center bed
    $self->{bed_origin} = [
        $canvas_w/2  - (unscale($bb->x_max + $bb->x_min)/2 * $self->{scaling_factor}),
        $canvas_h - ($canvas_h/2 - (unscale($bb->y_max + $bb->y_min)/2 * $self->{scaling_factor})),
    ];
    
    # calculate print center
    my $center = $bb->center;
    $self->{print_center} = [ unscale($center->x), unscale($center->y) ]; #))
    
    # cache bed contours and grid
    {
        my $step = scale 10;  # 1cm grid
        my @polylines = ();
        for (my $x = $bb->x_min - ($bb->x_min % $step) + $step; $x < $bb->x_max; $x += $step) {
            push @polylines, Slic3r::Polyline->new([$x, $bb->y_min], [$x, $bb->y_max]);
        }
        for (my $y = $bb->y_min - ($bb->y_min % $step) + $step; $y < $bb->y_max; $y += $step) {
            push @polylines, Slic3r::Polyline->new([$bb->x_min, $y], [$bb->x_max, $y]);
        }
        @polylines = @{intersection_pl(\@polylines, [$polygon])};
        $self->{grid} = [ map $self->scaled_points_to_pixel([ @$_[0,-1] ], 1), @polylines ];
    }
}

sub clean_instance_thumbnails {
    my ($self) = @_;
    
    foreach my $object (@{ $self->{objects} }) {
        @{ $object->instance_thumbnails } = ();
    }
}

# convert a model coordinate into a pixel coordinate
sub unscaled_point_to_pixel {
    my ($self, $point) = @_;
    
    my $canvas_height = $self->GetSize->GetHeight;
    my $zero = $self->{bed_origin};
    return [
        $point->[X] * $self->{scaling_factor} + $zero->[X],
        $canvas_height - $point->[Y] * $self->{scaling_factor} + ($zero->[Y] - $canvas_height),
    ];
}

sub scaled_points_to_pixel {
    my ($self, $points, $unscale) = @_;
    
    my $result = [];
    foreach my $point (@$points) {
        $point = [ map unscale($_), @$point ] if $unscale;
        push @$result, $self->unscaled_point_to_pixel($point);
    }
    return $result;
}

sub point_to_model_units {
    my ($self, $point) = @_;
    
    my $zero = $self->{bed_origin};
    return Slic3r::Point->new(
        scale ($point->[X] - $zero->[X]) / $self->{scaling_factor},
        scale ($zero->[Y] - $point->[Y]) / $self->{scaling_factor},
    );
}

1;
