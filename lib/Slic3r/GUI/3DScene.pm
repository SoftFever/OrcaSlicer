# Implements pure perl packages
#
# Slic3r::GUI::3DScene::Base;
# Slic3r::GUI::3DScene::Volume;
# Slic3r::GUI::3DScene;
#
# It uses static methods of a C++ class Slic3r::GUI::_3DScene::GLVertexArray
# for efficient building of vertex arrays for OpenGL rendering.
# 
# Slic3r::GUI::Plater::3D derives from Slic3r::GUI::3DScene,
# Slic3r::GUI::Plater::3DPreview, Slic3r::GUI::Plater::3DToolpaths, 
# Slic3r::GUI::Plater::ObjectCutDialog and Slic3r::GUI::Plater::ObjectPartsPanel
# own $self->{canvas} of the Slic3r::GUI::3DScene type.
#
# Therefore the 3DScene supports renderng of STLs, extrusions and cutting planes,
# and camera manipulation.

package Slic3r::GUI::3DScene::Base;
use strict;
use warnings;

use Wx qw(:timer :bitmap :icon :dialog);
use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS EVT_TIMER);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions :gluconstants);
use base qw(Wx::GLCanvas Class::Accessor);
use Math::Trig qw(asin tan);
use List::Util qw(reduce min max first);
use Slic3r::Geometry qw(X Y Z MIN MAX triangle_normal normalize deg2rad tan scale unscale scaled_epsilon);
use Slic3r::Geometry::Clipper qw(offset_ex intersection_pl);
use Wx::GLCanvas qw(:all);
use Slic3r::Geometry qw(PI);

# _dirty: boolean flag indicating, that the screen has to be redrawn on EVT_IDLE.
# volumes: reference to vector of Slic3r::GUI::3DScene::Volume.
# _camera_type: 'perspective' or 'ortho'
__PACKAGE__->mk_accessors( qw(_quat _dirty init
                              enable_picking
                              enable_moving
                              on_viewport_changed
                              on_hover
                              on_select
                              on_double_click
                              on_right_click
                              on_move
                              on_model_update
                              volumes
                              _sphi _stheta
                              cutting_plane_z
                              cut_lines_vertices
                              bed_shape
                              bed_triangles
                              bed_grid_lines
                              background
                              origin
                              _mouse_pos
                              _hover_volume_idx

                              _drag_volume_idx
                              _drag_start_pos
                              _drag_start_xy
                              _dragged

                              _layer_height_edited

                              _camera_type
                              _camera_target
                              _camera_distance
                              _zoom
                              ) );

use constant TRACKBALLSIZE  => 0.8;
use constant TURNTABLE_MODE => 1;
use constant GROUND_Z       => -0.02;
# For mesh selection: Not selected - bright yellow.
use constant DEFAULT_COLOR  => [1,1,0];
# For mesh selection: Selected - bright green.
use constant SELECTED_COLOR => [0,1,0,1];
# For mesh selection: Mouse hovers over the object, but object not selected yet - dark green.
use constant HOVER_COLOR    => [0.4,0.9,0,1];

# phi / theta angles to orient the camera.
use constant VIEW_DEFAULT    => [45.0,45.0];
use constant VIEW_LEFT       => [90.0,90.0];
use constant VIEW_RIGHT      => [-90.0,90.0];
use constant VIEW_TOP        => [0.0,0.0];
use constant VIEW_BOTTOM     => [0.0,180.0];
use constant VIEW_FRONT      => [0.0,90.0];
use constant VIEW_REAR       => [180.0,90.0];

use constant MANIPULATION_IDLE          => 0;
use constant MANIPULATION_DRAGGING      => 1;
use constant MANIPULATION_LAYER_HEIGHT  => 2;

use constant GIMBALL_LOCK_THETA_MAX => 170;

use constant VARIABLE_LAYER_THICKNESS_BAR_WIDTH => 70;
use constant VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT => 22;

# make OpenGL::Array thread-safe
{
    no warnings 'redefine';
    *OpenGL::Array::CLONE_SKIP = sub { 1 };
}

sub new {
    my ($class, $parent) = @_;
    
    # We can only enable multi sample anti aliasing wih wxWidgets 3.0.3 and with a hacked Wx::GLCanvas,
    # which exports some new WX_GL_XXX constants, namely WX_GL_SAMPLE_BUFFERS and WX_GL_SAMPLES.
    my $can_multisample =
        Wx::wxVERSION >= 3.000003 &&
        defined Wx::GLCanvas->can('WX_GL_SAMPLE_BUFFERS') &&
        defined Wx::GLCanvas->can('WX_GL_SAMPLES');
    my $attrib = [WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24];
    if ($can_multisample) {
        # Request a window with multi sampled anti aliasing. This is a new feature in Wx 3.0.3 (backported from 3.1.0).
        # Use eval to avoid compilation, if the subs WX_GL_SAMPLE_BUFFERS and WX_GL_SAMPLES are missing.
        eval 'push(@$attrib, (WX_GL_SAMPLE_BUFFERS, 1, WX_GL_SAMPLES, 4));';
    }
    # wxWidgets expect the attrib list to be ended by zero.
    push(@$attrib, 0);

    # we request a depth buffer explicitely because it looks like it's not created by 
    # default on Linux, causing transparency issues
    my $self = $class->SUPER::new($parent, -1, Wx::wxDefaultPosition, Wx::wxDefaultSize, 0, "", $attrib);
    if (Wx::wxVERSION >= 3.000003) {
        # Wx 3.0.3 contains an ugly hack to support some advanced OpenGL attributes through the attribute list.
        # The attribute list is transferred between the wxGLCanvas and wxGLContext constructors using a single static array s_wglContextAttribs.
        # Immediatelly force creation of the OpenGL context to consume the static variable s_wglContextAttribs.
        $self->GetContext();
    }

    $self->background(1);
    $self->_quat((0, 0, 0, 1));
    $self->_stheta(45);
    $self->_sphi(45);
    $self->_zoom(1);
    
    # 3D point in model space
    $self->_camera_type('ortho');
#    $self->_camera_type('perspective');
    $self->_camera_target(Slic3r::Pointf3->new(0,0,0));
    $self->_camera_distance(0.);

    # Size of a layer height texture, used by a shader to color map the object print layers.
    $self->layer_editing_enabled(0);
    # 512x512 bitmaps are supported everywhere, but that may not be sufficent for super large print volumes.
    $self->{layer_preview_z_texture_width} = 1024;
    $self->{layer_preview_z_texture_height} = 1024;
    $self->{layer_height_edit_band_width} = 2.;
    $self->{layer_height_edit_strength} = 0.005;
    $self->{layer_height_edit_last_object_id} = -1;
    $self->{layer_height_edit_last_z} = 0.;
    $self->{layer_height_edit_last_action} = 0;
    
    $self->reset_objects;
    
    EVT_PAINT($self, sub {
        my $dc = Wx::PaintDC->new($self);
        $self->Render($dc);
    });
    EVT_SIZE($self, sub { $self->_dirty(1) });
    EVT_IDLE($self, sub {
        return unless $self->_dirty;
        return if !$self->IsShownOnScreen;
        $self->Resize( $self->GetSizeWH );
        $self->Refresh;
    });
    EVT_MOUSEWHEEL($self, \&mouse_wheel_event);
    EVT_MOUSE_EVENTS($self, \&mouse_event);
    
    $self->{layer_height_edit_timer_id} = &Wx::NewId();
    $self->{layer_height_edit_timer} = Wx::Timer->new($self, $self->{layer_height_edit_timer_id});
    EVT_TIMER($self, $self->{layer_height_edit_timer_id}, sub {
        my ($self, $event) = @_;
        return if $self->_layer_height_edited != 1;
        return if $self->{layer_height_edit_last_object_id} == -1;
        $self->_variable_layer_thickness_action(undef);
    });
    
    return $self;
}

sub Destroy {
    my ($self) = @_;
    $self->{layer_height_edit_timer}->Stop;
    $self->DestroyGL;
    return $self->SUPER::Destroy;
}

sub layer_editing_enabled {
    my ($self, $value) = @_;
    if (@_ == 2) {
        $self->{layer_editing_enabled} = $value;
        if ($value) {
            if (! $self->{layer_editing_initialized}) {
                # Enabling the layer editing for the first time. This triggers compilation of the necessary OpenGL shaders.
                # If compilation fails, a message box is shown with the error codes.
                my $shader = $self->{shader} = new Slic3r::GUI::GLShader;
                my $error_message;
                if (ref($shader)) {
                    my $info = $shader->Load($self->_fragment_shader, $self->_vertex_shader);
                    if (defined($info)) {
                        # Compilation or linking of the shaders failed.
                        $error_message = "Cannot compile an OpenGL Shader, therefore the Variable Layer Editing will be disabled.\n\n" 
                            . $info;
                    } else {
                        ($self->{layer_preview_z_texture_id}) = glGenTextures_p(1);
                        glBindTexture(GL_TEXTURE_2D, $self->{layer_preview_z_texture_id});
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
                        glBindTexture(GL_TEXTURE_2D, 0);
                    }
                } else {
                    # Cannot initialize the Shader object, some of the OpenGL capabilities are missing.
                    $error_message = "Cannot instantiate an OpenGL Shader, therefore the Variable Layer Editing will be disabled.\n\n" 
                        . $shader;
                }
                if (defined($error_message)) {
                    # Don't enable the layer editing tool.
                    $self->{layer_editing_enabled} = 0;
                    # 2 means failed
                    $self->{layer_editing_initialized} = 2;
                    # Show the error message.
                    Wx::MessageBox($error_message, "Slic3r Error", wxOK | wxICON_EXCLAMATION, $self);
                } else {
                    $self->{layer_editing_initialized} = 1;
                }
            } elsif ($self->{layer_editing_initialized} == 2) {
                # Initilization failed before. Don't try to initialize and disable layer editing.
                $self->{layer_editing_enabled} = 0;
            }
        }
    }
    return $self->{layer_editing_enabled};
}

sub layer_editing_allowed {
    my ($self) = @_;
    # Allow layer editing if either the shaders were not initialized yet and we don't know
    # whether it will be possible to initialize them, 
    # or if the initialization was done already and it failed.
    return ! (defined($self->{layer_editing_initialized}) && $self->{layer_editing_initialized} == 2);
}

sub _first_selected_object_id {
    my ($self) = @_;
    for my $i (0..$#{$self->volumes}) {
        if ($self->volumes->[$i]->selected) {
            return int($self->volumes->[$i]->select_group_id / 1000000);
        }
    }
    return -1;
}

# Returns an array with (left, top, right, bottom) of the variable layer thickness bar on the screen.
sub _variable_layer_thickness_bar_rect_screen {
    my ($self) = @_;
    my ($cw, $ch) = $self->GetSizeWH;
    return ($cw - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, 0, $cw, $ch - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT);
}

sub _variable_layer_thickness_bar_rect_viewport {
    my ($self) = @_;
    my ($cw, $ch) = $self->GetSizeWH;
    return ((0.5*$cw-VARIABLE_LAYER_THICKNESS_BAR_WIDTH)/$self->_zoom, (-0.5*$ch+VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT)/$self->_zoom, $cw/(2*$self->_zoom), $ch/(2*$self->_zoom));
}

# Returns an array with (left, top, right, bottom) of the variable layer thickness bar on the screen.
sub _variable_layer_thickness_reset_rect_screen {
    my ($self) = @_;
    my ($cw, $ch) = $self->GetSizeWH;
    return ($cw - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, $ch - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT, $cw, $ch);
}

sub _variable_layer_thickness_reset_rect_viewport {
    my ($self) = @_;
    my ($cw, $ch) = $self->GetSizeWH;
    return ((0.5*$cw-VARIABLE_LAYER_THICKNESS_BAR_WIDTH)/$self->_zoom, -$ch/(2*$self->_zoom), $cw/(2*$self->_zoom), (-0.5*$ch+VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT)/$self->_zoom);
}

sub _variable_layer_thickness_bar_rect_mouse_inside {
   my ($self, $mouse_evt) = @_;
   my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_bar_rect_screen;
   return $mouse_evt->GetX >= $bar_left && $mouse_evt->GetX <= $bar_right && $mouse_evt->GetY >= $bar_top && $mouse_evt->GetY <= $bar_bottom;
}

sub _variable_layer_thickness_reset_rect_mouse_inside {
   my ($self, $mouse_evt) = @_;
   my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_reset_rect_screen;
   return $mouse_evt->GetX >= $bar_left && $mouse_evt->GetX <= $bar_right && $mouse_evt->GetY >= $bar_top && $mouse_evt->GetY <= $bar_bottom;
}

sub _variable_layer_thickness_bar_mouse_cursor_z {
   my ($self, $object_idx, $mouse_evt) = @_;
   my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_bar_rect_screen;
   return unscale($self->{print}->get_object($object_idx)->size->z) * ($bar_bottom - $mouse_evt->GetY - 1.) / ($bar_bottom - $bar_top);
}

sub _variable_layer_thickness_bar_mouse_cursor_z_relative {
   my ($self) = @_;
   my $mouse_pos = $self->ScreenToClientPoint(Wx::GetMousePosition());
   my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_bar_rect_screen;
   return ($mouse_pos->x >= $bar_left && $mouse_pos->x <= $bar_right && $mouse_pos->y >= $bar_top && $mouse_pos->y <= $bar_bottom) ?
        # Inside the bar.
        ($bar_bottom - $mouse_pos->y - 1.) / ($bar_bottom - $bar_top - 1) :
        # Outside the bar.
        -1000.;
}

sub _variable_layer_thickness_action {
    my ($self, $mouse_event, $do_modification) = @_;
    # A volume is selected. Test, whether hovering over a layer thickness bar.
    if (defined($mouse_event)) {
        $self->{layer_height_edit_last_z} = $self->_variable_layer_thickness_bar_mouse_cursor_z($self->{layer_height_edit_last_object_id}, $mouse_event);
        $self->{layer_height_edit_last_action} = $mouse_event->ShiftDown ? ($mouse_event->RightIsDown ? 3 : 2) : ($mouse_event->RightIsDown ? 0 : 1);
    }
    if ($self->{layer_height_edit_last_object_id} != -1) {
        # Mark the volume as modified, so Print will pick its layer height profile? Where to mark it?
        # Start a timer to refresh the print? schedule_background_process() ?
        $self->{print}->get_object($self->{layer_height_edit_last_object_id})->adjust_layer_height_profile(
            $self->{layer_height_edit_last_z},
            $self->{layer_height_edit_strength},
            $self->{layer_height_edit_band_width}, 
            $self->{layer_height_edit_last_action});
        $self->{print}->get_object($self->{layer_height_edit_last_object_id})->generate_layer_height_texture(
            $self->volumes->[$self->{layer_height_edit_last_object_id}]->layer_height_texture_data->ptr,
            $self->{layer_preview_z_texture_height},
            $self->{layer_preview_z_texture_width});
        $self->Refresh;
        # Automatic action on mouse down with the same coordinate.
        $self->{layer_height_edit_timer}->Start(100, wxTIMER_CONTINUOUS);
    }
}

sub mouse_event {
    my ($self, $e) = @_;
    
    my $pos = Slic3r::Pointf->new($e->GetPositionXY);
    my $object_idx_selected = $self->{layer_height_edit_last_object_id} = ($self->layer_editing_enabled && $self->{print}) ? $self->_first_selected_object_id : -1;

    if ($e->Entering && &Wx::wxMSW) {
        # wxMSW needs focus in order to catch mouse wheel events
        $self->SetFocus;
    } elsif ($e->LeftDClick) {
        if ($object_idx_selected != -1 && $self->_variable_layer_thickness_bar_rect_mouse_inside($e)) {
        } elsif ($self->on_double_click) {
            $self->on_double_click->();
        }
    } elsif ($e->LeftDown || $e->RightDown) {
        # If user pressed left or right button we first check whether this happened
        # on a volume or not.
        my $volume_idx = $self->_hover_volume_idx // -1;
        $self->_layer_height_edited(0);
        if ($object_idx_selected != -1 && $self->_variable_layer_thickness_bar_rect_mouse_inside($e)) {
            # A volume is selected and the mouse is hovering over a layer thickness bar.
            # Start editing the layer height.
            $self->_layer_height_edited(1);
            $self->_variable_layer_thickness_action($e);
        } elsif ($object_idx_selected != -1 && $self->_variable_layer_thickness_reset_rect_mouse_inside($e)) {
            $self->{print}->get_object($self->{layer_height_edit_last_object_id})->reset_layer_height_profile;
            # Index 2 means no editing, just wait for mouse up event.
            $self->_layer_height_edited(2);
            $self->Refresh;
        } else {
            # Select volume in this 3D canvas.
            # Don't deselect a volume if layer editing is enabled. We want the object to stay selected
            # during the scene manipulation.
            if ($self->enable_picking && ($volume_idx != -1 || ! $self->layer_editing_enabled)) {
                $self->deselect_volumes;
                $self->select_volume($volume_idx);
                
                if ($volume_idx != -1) {
                    my $group_id = $self->volumes->[$volume_idx]->select_group_id;
                    my @volumes;
                    if ($group_id != -1) {
                        $self->select_volume($_)
                            for grep $self->volumes->[$_]->select_group_id == $group_id,
                            0..$#{$self->volumes};
                    }
                }
                
                $self->Refresh;
            }
            
            # propagate event through callback
            $self->on_select->($volume_idx)
                if $self->on_select;
            
            if ($volume_idx != -1) {
                if ($e->LeftDown && $self->enable_moving) {
                    $self->_drag_volume_idx($volume_idx);
                    $self->_drag_start_pos($self->mouse_to_3d(@$pos));
                } elsif ($e->RightDown) {
                    # if right clicking on volume, propagate event through callback
                    $self->on_right_click->($e->GetPosition)
                        if $self->on_right_click;
                }
            }
        }
    } elsif ($e->Dragging && $e->LeftIsDown && ! $self->_layer_height_edited && defined($self->_drag_volume_idx)) {
        # get new position at the same Z of the initial click point
        my $mouse_ray = $self->mouse_ray($e->GetX, $e->GetY);
        my $cur_pos = $mouse_ray->intersect_plane($self->_drag_start_pos->z);
        
        # calculate the translation vector
        my $vector = $self->_drag_start_pos->vector_to($cur_pos);
        
        # get volume being dragged
        my $volume = $self->volumes->[$self->_drag_volume_idx];
        
        # get all volumes belonging to the same group, if any
        my @volumes;
        if ($volume->drag_group_id == -1) {
            @volumes = ($volume);
        } else {
            @volumes = grep $_->drag_group_id == $volume->drag_group_id, @{$self->volumes};
        }
        
        # apply new temporary volume origin and ignore Z
        $_->origin->translate($vector->x, $vector->y, 0) for @volumes; #,,
        $self->_drag_start_pos($cur_pos);
        $self->_dragged(1);
        $self->Refresh;
    } elsif ($e->Dragging) {
        if ($self->_layer_height_edited && $object_idx_selected != -1) {
            $self->_variable_layer_thickness_action($e) if ($self->_layer_height_edited == 1);
        } elsif ($e->LeftIsDown) {
            # if dragging over blank area with left button, rotate
            if (defined $self->_drag_start_pos) {
                my $orig = $self->_drag_start_pos;
                if (TURNTABLE_MODE) {
                    $self->_sphi($self->_sphi + ($pos->x - $orig->x) * TRACKBALLSIZE);
                    $self->_stheta($self->_stheta - ($pos->y - $orig->y) * TRACKBALLSIZE);        #-
                    $self->_stheta(GIMBALL_LOCK_THETA_MAX) if $self->_stheta > GIMBALL_LOCK_THETA_MAX;
                    $self->_stheta(0) if $self->_stheta < 0;
                } else {
                    my $size = $self->GetClientSize;
                    my @quat = trackball(
                        $orig->x / ($size->width / 2) - 1,
                        1 - $orig->y / ($size->height / 2),       #/
                        $pos->x / ($size->width / 2) - 1,
                        1 - $pos->y / ($size->height / 2),        #/
                    );
                    $self->_quat(mulquats($self->_quat, \@quat));
                }
                $self->on_viewport_changed->() if $self->on_viewport_changed;
                $self->Refresh;
            }
            $self->_drag_start_pos($pos);
        } elsif ($e->MiddleIsDown || $e->RightIsDown) {
            # if dragging over blank area with right button, translate
            
            if (defined $self->_drag_start_xy) {
                # get point in model space at Z = 0
                my $cur_pos = $self->mouse_ray($e->GetX, $e->GetY)->intersect_plane(0);
                my $orig    = $self->mouse_ray(@{$self->_drag_start_xy})->intersect_plane(0);
                $self->_camera_target->translate(
                    @{$orig->vector_to($cur_pos)->negative},
                );
                $self->on_viewport_changed->() if $self->on_viewport_changed;
                $self->Refresh;
            }
            $self->_drag_start_xy($pos);
        }
    } elsif ($e->LeftUp || $e->MiddleUp || $e->RightUp) {
        if ($self->_layer_height_edited) {
            $self->_layer_height_edited(undef);
            $self->{layer_height_edit_timer}->Stop;
            $self->on_model_update->()
                if ($object_idx_selected != -1 && $self->on_model_update);
        } elsif ($self->on_move && defined($self->_drag_volume_idx) && $self->_dragged) {
            # get all volumes belonging to the same group, if any
            my @volume_idxs;
            my $group_id = $self->volumes->[$self->_drag_volume_idx]->drag_group_id;
            if ($group_id == -1) {
                @volume_idxs = ($self->_drag_volume_idx);
            } else {
                @volume_idxs = grep $self->volumes->[$_]->drag_group_id == $group_id,
                    0..$#{$self->volumes};
            }
            $self->on_move->(@volume_idxs);
        }
        $self->_drag_volume_idx(undef);
        $self->_drag_start_pos(undef);
        $self->_drag_start_xy(undef);
        $self->_dragged(undef);
    } elsif ($e->Moving) {
        $self->_mouse_pos($pos);
        # Only refresh if picking is enabled, in that case the objects may get highlighted if the mouse cursor
        # hovers over.
        $self->Refresh if ($self->enable_picking);
    } else {
        $e->Skip();
    }
}

sub mouse_wheel_event {
    my ($self, $e) = @_;
    
    if ($self->layer_editing_enabled && $self->{print}) {
        my $object_idx_selected = $self->_first_selected_object_id;
        if ($object_idx_selected != -1) {
            # A volume is selected. Test, whether hovering over a layer thickness bar.
            if ($self->_variable_layer_thickness_bar_rect_mouse_inside($e)) {
                # Adjust the width of the selection.
                $self->{layer_height_edit_band_width} = max(min($self->{layer_height_edit_band_width} * (1 + 0.1 * $e->GetWheelRotation() / $e->GetWheelDelta()), 10.), 1.5);
                $self->Refresh;
                return;
            }
        }
    }

    # Calculate the zoom delta and apply it to the current zoom factor
    my $zoom = $e->GetWheelRotation() / $e->GetWheelDelta();
    $zoom = max(min($zoom, 4), -4);
    $zoom /= 10;
    $self->_zoom($self->_zoom / (1-$zoom));
    
    # In order to zoom around the mouse point we need to translate
    # the camera target
    my $size = Slic3r::Pointf->new($self->GetSizeWH);
    my $pos = Slic3r::Pointf->new($e->GetX, $size->y - $e->GetY); #-
    $self->_camera_target->translate(
        # ($pos - $size/2) represents the vector from the viewport center
        # to the mouse point. By multiplying it by $zoom we get the new,
        # transformed, length of such vector.
        # Since we want that point to stay fixed, we move our camera target
        # in the opposite direction by the delta of the length of such vector
        # ($zoom - 1). We then scale everything by 1/$self->_zoom since 
        # $self->_camera_target is expressed in terms of model units.
        -($pos->x - $size->x/2) * ($zoom) / $self->_zoom,
        -($pos->y - $size->y/2) * ($zoom) / $self->_zoom,
        0,
    ) if 0;
    $self->on_viewport_changed->() if $self->on_viewport_changed;
    $self->Resize($self->GetSizeWH) if $self->IsShownOnScreen;
    $self->Refresh;
}

# Reset selection.
sub reset_objects {
    my ($self) = @_;
    
    $self->volumes([]);
    $self->_dirty(1);
}

# Setup camera to view all objects.
sub set_viewport_from_scene {
    my ($self, $scene) = @_;
    
    $self->_sphi($scene->_sphi);
    $self->_stheta($scene->_stheta);
    $self->_camera_target($scene->_camera_target);
    $self->_zoom($scene->_zoom);
    $self->_quat($scene->_quat);
    $self->_dirty(1);
}

# Set the camera to a default orientation,
# zoom to volumes.
sub select_view {
    my ($self, $direction) = @_;
    my $dirvec;
    if (ref($direction)) {
        $dirvec = $direction;
    } else {
        if ($direction eq 'iso') {
            $dirvec = VIEW_DEFAULT;
        } elsif ($direction eq 'left') {
            $dirvec = VIEW_LEFT;
        } elsif ($direction eq 'right') {
            $dirvec = VIEW_RIGHT;
        } elsif ($direction eq 'top') {
            $dirvec = VIEW_TOP;
        } elsif ($direction eq 'bottom') {
            $dirvec = VIEW_BOTTOM;
        } elsif ($direction eq 'front') {
            $dirvec = VIEW_FRONT;
        } elsif ($direction eq 'rear') {
            $dirvec = VIEW_REAR;
        }
    }
    my $bb = $self->volumes_bounding_box;
    if (! $bb->empty) {
        $self->_sphi($dirvec->[0]);
        $self->_stheta($dirvec->[1]);
        # Avoid gimball lock.
        $self->_stheta(GIMBALL_LOCK_THETA_MAX) if $self->_stheta > GIMBALL_LOCK_THETA_MAX;
        $self->_stheta(0) if $self->_stheta < 0;
        # View everything.
        $self->zoom_to_bounding_box($bb);
        $self->on_viewport_changed->() if $self->on_viewport_changed;
        $self->Refresh;
    }
}

sub zoom_to_bounding_box {
    my ($self, $bb) = @_;
    return if ($bb->empty);
    
    # calculate the zoom factor needed to adjust viewport to
    # bounding box
    my $max_size = max(@{$bb->size}) * 2;
    my $min_viewport_size = min($self->GetSizeWH);
    $self->_zoom($min_viewport_size / $max_size);
    
    # center view around bounding box center
    $self->_camera_target($bb->center);
    
    $self->on_viewport_changed->() if $self->on_viewport_changed;
}

sub zoom_to_bed {
    my ($self) = @_;
    
    if ($self->bed_shape) {
        $self->zoom_to_bounding_box($self->bed_bounding_box);
    }
}

sub zoom_to_volume {
    my ($self, $volume_idx) = @_;
    
    my $volume = $self->volumes->[$volume_idx];
    my $bb = $volume->transformed_bounding_box;
    $self->zoom_to_bounding_box($bb);
}

sub zoom_to_volumes {
    my ($self) = @_;
    $self->zoom_to_bounding_box($self->volumes_bounding_box);
}

sub volumes_bounding_box {
    my ($self) = @_;
    
    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
    $bb->merge($_->transformed_bounding_box) for @{$self->volumes};
    return $bb;
}

sub bed_bounding_box {
    my ($self) = @_;
    
    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
    if ($self->bed_shape) {
        $bb->merge_point(Slic3r::Pointf3->new(@$_, 0)) for @{$self->bed_shape};
    }
    return $bb;
}

sub max_bounding_box {
    my ($self) = @_;
    
    my $bb = $self->bed_bounding_box;
    $bb->merge($self->volumes_bounding_box);
    return $bb;
}

sub set_auto_bed_shape {
    my ($self, $bed_shape) = @_;
    
    # draw a default square bed around object center
    my $max_size = max(@{ $self->volumes_bounding_box->size });
    my $center = $self->volumes_bounding_box->center;
    $self->set_bed_shape([
        [ $center->x - $max_size, $center->y - $max_size ],  #--
        [ $center->x + $max_size, $center->y - $max_size ],  #--
        [ $center->x + $max_size, $center->y + $max_size ],  #++
        [ $center->x - $max_size, $center->y + $max_size ],  #++
    ]);
    $self->origin(Slic3r::Pointf->new(@$center[X,Y]));
}

sub set_bed_shape {
    my ($self, $bed_shape) = @_;
    
    $self->bed_shape($bed_shape);
    
    # triangulate bed
    my $expolygon = Slic3r::ExPolygon->new([ map [map scale($_), @$_], @$bed_shape ]);
    my $bed_bb = $expolygon->bounding_box;
    
    {
        my @points = ();
        foreach my $triangle (@{ $expolygon->triangulate }) {
            push @points, map {+ unscale($_->x), unscale($_->y), GROUND_Z } @$triangle;  #))
        }
        $self->bed_triangles(OpenGL::Array->new_list(GL_FLOAT, @points));
    }
    
    {
        my @polylines = ();
        for (my $x = $bed_bb->x_min; $x <= $bed_bb->x_max; $x += scale 10) {
            push @polylines, Slic3r::Polyline->new([$x,$bed_bb->y_min], [$x,$bed_bb->y_max]);
        }
        for (my $y = $bed_bb->y_min; $y <= $bed_bb->y_max; $y += scale 10) {
            push @polylines, Slic3r::Polyline->new([$bed_bb->x_min,$y], [$bed_bb->x_max,$y]);
        }
        # clip with a slightly grown expolygon because our lines lay on the contours and
        # may get erroneously clipped
        my @lines = map Slic3r::Line->new(@$_[0,-1]),
            @{intersection_pl(\@polylines, [ @{$expolygon->offset(+scaled_epsilon)} ])};
        
        # append bed contours
        push @lines, map @{$_->lines}, @$expolygon;
        
        my @points = ();
        foreach my $line (@lines) {
            push @points, map {+ unscale($_->x), unscale($_->y), GROUND_Z } @$line;  #))
        }
        $self->bed_grid_lines(OpenGL::Array->new_list(GL_FLOAT, @points));
    }
    
    $self->origin(Slic3r::Pointf->new(0,0));
}

sub deselect_volumes {
    my ($self) = @_;
    $_->selected(0) for @{$self->volumes};
}

sub select_volume {
    my ($self, $volume_idx) = @_;
    
    $self->volumes->[$volume_idx]->selected(1)
        if $volume_idx != -1;
}

sub SetCuttingPlane {
    my ($self, $z, $expolygons) = @_;
    
    $self->cutting_plane_z($z);
    
    # grow slices in order to display them better
    $expolygons = offset_ex([ map @$_, @$expolygons ], scale 0.1);
    
    my @verts = ();
    foreach my $line (map @{$_->lines}, map @$_, @$expolygons) {
        push @verts, (
            unscale($line->a->x), unscale($line->a->y), $z,  #))
            unscale($line->b->x), unscale($line->b->y), $z,  #))
        );
    }
    $self->cut_lines_vertices(OpenGL::Array->new_list(GL_FLOAT, @verts));
}

# Given an axis and angle, compute quaternion.
sub axis_to_quat {
    my ($ax, $phi) = @_;
    
    my $lena = sqrt(reduce { $a + $b } (map { $_ * $_ } @$ax));
    my @q = map { $_ * (1 / $lena) } @$ax;
    @q = map { $_ * sin($phi / 2.0) } @q;
    $q[$#q + 1] = cos($phi / 2.0);
    return @q;
}

# Project a point on the virtual trackball. 
# If it is inside the sphere, map it to the sphere, if it outside map it
# to a hyperbola.
sub project_to_sphere {
    my ($r, $x, $y) = @_;
    
    my $d = sqrt($x * $x + $y * $y);
    if ($d < $r * 0.70710678118654752440) {     # Inside sphere
        return sqrt($r * $r - $d * $d);
    } else {                                    # On hyperbola
        my $t = $r / 1.41421356237309504880;
        return $t * $t / $d;
    }
}

sub cross {
    my ($v1, $v2) = @_;
  
    return (@$v1[1] * @$v2[2] - @$v1[2] * @$v2[1],
            @$v1[2] * @$v2[0] - @$v1[0] * @$v2[2],
            @$v1[0] * @$v2[1] - @$v1[1] * @$v2[0]);
}

# Simulate a track-ball. Project the points onto the virtual trackball, 
# then figure out the axis of rotation, which is the cross product of 
# P1 P2 and O P1 (O is the center of the ball, 0,0,0) Note: This is a 
# deformed trackball-- is a trackball in the center, but is deformed 
# into a hyperbolic sheet of rotation away from the center. 
# It is assumed that the arguments to this routine are in the range 
# (-1.0 ... 1.0).
sub trackball {
    my ($p1x, $p1y, $p2x, $p2y) = @_;
    
    if ($p1x == $p2x && $p1y == $p2y) {
        # zero rotation
        return (0.0, 0.0, 0.0, 1.0);
    }
    
    # First, figure out z-coordinates for projection of P1 and P2 to
    # deformed sphere
    my @p1 = ($p1x, $p1y, project_to_sphere(TRACKBALLSIZE, $p1x, $p1y));
    my @p2 = ($p2x, $p2y, project_to_sphere(TRACKBALLSIZE, $p2x, $p2y));
    
    # axis of rotation (cross product of P1 and P2)
    my @a = cross(\@p2, \@p1);

    # Figure out how much to rotate around that axis.
    my @d = map { $_ * $_ } (map { $p1[$_] - $p2[$_] } 0 .. $#p1);
    my $t = sqrt(reduce { $a + $b } @d) / (2.0 * TRACKBALLSIZE);
    
    # Avoid problems with out-of-control values...
    $t = 1.0 if ($t > 1.0);
    $t = -1.0 if ($t < -1.0);
    my $phi = 2.0 * asin($t);

    return axis_to_quat(\@a, $phi);
}

# Build a rotation matrix, given a quaternion rotation.
sub quat_to_rotmatrix {
    my ($q) = @_;
  
    my @m = ();
  
    $m[0] = 1.0 - 2.0 * (@$q[1] * @$q[1] + @$q[2] * @$q[2]);
    $m[1] = 2.0 * (@$q[0] * @$q[1] - @$q[2] * @$q[3]);
    $m[2] = 2.0 * (@$q[2] * @$q[0] + @$q[1] * @$q[3]);
    $m[3] = 0.0;

    $m[4] = 2.0 * (@$q[0] * @$q[1] + @$q[2] * @$q[3]);
    $m[5] = 1.0 - 2.0 * (@$q[2] * @$q[2] + @$q[0] * @$q[0]);
    $m[6] = 2.0 * (@$q[1] * @$q[2] - @$q[0] * @$q[3]);
    $m[7] = 0.0;

    $m[8] = 2.0 * (@$q[2] * @$q[0] - @$q[1] * @$q[3]);
    $m[9] = 2.0 * (@$q[1] * @$q[2] + @$q[0] * @$q[3]);
    $m[10] = 1.0 - 2.0 * (@$q[1] * @$q[1] + @$q[0] * @$q[0]);
    $m[11] = 0.0;

    $m[12] = 0.0;
    $m[13] = 0.0;
    $m[14] = 0.0;
    $m[15] = 1.0;
  
    return @m;
}

sub mulquats {
    my ($q1, $rq) = @_;
  
    return (@$q1[3] * @$rq[0] + @$q1[0] * @$rq[3] + @$q1[1] * @$rq[2] - @$q1[2] * @$rq[1],
            @$q1[3] * @$rq[1] + @$q1[1] * @$rq[3] + @$q1[2] * @$rq[0] - @$q1[0] * @$rq[2],
            @$q1[3] * @$rq[2] + @$q1[2] * @$rq[3] + @$q1[0] * @$rq[1] - @$q1[1] * @$rq[0],
            @$q1[3] * @$rq[3] - @$q1[0] * @$rq[0] - @$q1[1] * @$rq[1] - @$q1[2] * @$rq[2])
}

sub mouse_to_3d {
    my ($self, $x, $y, $z) = @_;

    my @viewport    = glGetIntegerv_p(GL_VIEWPORT);             # 4 items
    my @mview       = glGetDoublev_p(GL_MODELVIEW_MATRIX);      # 16 items
    my @proj        = glGetDoublev_p(GL_PROJECTION_MATRIX);     # 16 items
    
    $y = $viewport[3] - $y;
    $z //= glReadPixels_p($x, $y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT);
    my @projected = gluUnProject_p($x, $y, $z, @mview, @proj, @viewport);
    return Slic3r::Pointf3->new(@projected);
}

sub mouse_ray {
    my ($self, $x, $y) = @_;
    
    return Slic3r::Linef3->new(
        $self->mouse_to_3d($x, $y, 0),
        $self->mouse_to_3d($x, $y, 1),
    );
}

sub GetContext {
    my ($self) = @_;
    
    if (Wx::wxVERSION >= 2.009) {
        return $self->{context} ||= Wx::GLContext->new($self);
    } else {
        return $self->SUPER::GetContext;
    }
}
 
sub SetCurrent {
    my ($self, $context) = @_;
    
    if (Wx::wxVERSION >= 2.009) {
        return $self->SUPER::SetCurrent($context);
    } else {
        return $self->SUPER::SetCurrent;
    }
}

sub Resize {
    my ($self, $x, $y) = @_;
 
    return unless $self->GetContext;
    $self->_dirty(0);
    
    $self->SetCurrent($self->GetContext);
    glViewport(0, 0, $x, $y);
 
    $x /= $self->_zoom;
    $y /= $self->_zoom;
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if ($self->_camera_type eq 'ortho') {
        #FIXME setting the size of the box 10x larger than necessary
        # is only a workaround for an incorrectly set camera.
        # This workaround harms Z-buffer accuracy!
#        my $depth = 1.05 * $self->max_bounding_box->radius(); 
       my $depth = 10.0 * $self->max_bounding_box->radius();
        glOrtho(
            -$x/2, $x/2, -$y/2, $y/2,
            -$depth, $depth,
        );
    } else {
        die "Invalid camera type: ", $self->_camera_type, "\n" if ($self->_camera_type ne 'perspective');
        my $bbox_r = $self->max_bounding_box->radius();
        my $fov = PI * 45. / 180.;
        my $fov_tan = tan(0.5 * $fov);
        my $cam_distance = 0.5 * $bbox_r / $fov_tan;
        $self->_camera_distance($cam_distance);
        my $nr = $cam_distance - $bbox_r * 1.1;
        my $fr = $cam_distance + $bbox_r * 1.1;
        $nr = 1 if ($nr < 1);
        $fr = $nr + 1 if ($fr < $nr + 1);
        my $h2 = $fov_tan * $nr;
        my $w2 = $h2 * $x / $y;
        glFrustum(-$w2, $w2, -$h2, $h2, $nr, $fr);        
    }

    glMatrixMode(GL_MODELVIEW);
}

sub InitGL {
    my $self = shift;
 
    return if $self->init;
    return unless $self->GetContext;
    $self->init(1);

    glClearColor(0, 0, 0, 1);
    glColor3f(1, 0, 0);
    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    # Set antialiasing/multisampling
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POLYGON_SMOOTH);
    glEnable(GL_MULTISAMPLE);
#    glHint(GL_MULTISAMPLE_FILTER_HINT_NV, GL_NICEST);
    
    # ambient lighting
    glLightModelfv_p(GL_LIGHT_MODEL_AMBIENT, 0.3, 0.3, 0.3, 1);
    
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    
    # light from camera
    glLightfv_p(GL_LIGHT1, GL_POSITION, 1, 0, 1, 0);
    glLightfv_p(GL_LIGHT1, GL_SPECULAR, 0.3, 0.3, 0.3, 1);
    glLightfv_p(GL_LIGHT1, GL_DIFFUSE,  0.2, 0.2, 0.2, 1);
    
    # Enables Smooth Color Shading; try GL_FLAT for (lack of) fun.
    glShadeModel(GL_SMOOTH);
    
    glMaterialfv_p(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, 0.5, 0.3, 0.3, 1);
    glMaterialfv_p(GL_FRONT_AND_BACK, GL_SPECULAR, 1, 1, 1, 1);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 50);
    glMaterialfv_p(GL_FRONT_AND_BACK, GL_EMISSION, 0.1, 0, 0, 0.9);
    
    # A handy trick -- have surface material mirror the color.
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_MULTISAMPLE);
}

sub DestroyGL {
    my $self = shift;
    if ($self->init && $self->GetContext) {
        delete $self->{shader};
    }
}

sub Render {
    my ($self, $dc) = @_;
    
    # prevent calling SetCurrent() when window is not shown yet
    return unless $self->IsShownOnScreen;
    return unless my $context = $self->GetContext;
    $self->SetCurrent($context);
    $self->InitGL;
    
    glClearColor(1, 1, 1, 1);
    glClearDepth(1);
    glDepthFunc(GL_LESS);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    {
        # Shift the perspective camera.
        my $camera_pos = Slic3r::Pointf3->new(0,0,-$self->_camera_distance);
        glTranslatef(@$camera_pos);
    }
    
    if (TURNTABLE_MODE) {
        glRotatef(-$self->_stheta, 1, 0, 0); # pitch
        glRotatef($self->_sphi, 0, 0, 1);    # yaw
    } else {
        my @rotmat = quat_to_rotmatrix($self->quat);
        glMultMatrixd_p(@rotmat[0..15]);
    }
    glTranslatef(@{ $self->_camera_target->negative });
    
    # light from above
    glLightfv_p(GL_LIGHT0, GL_POSITION, -0.5, -0.5, 1, 0);
    glLightfv_p(GL_LIGHT0, GL_SPECULAR, 0.2, 0.2, 0.2, 1);
    glLightfv_p(GL_LIGHT0, GL_DIFFUSE,  0.5, 0.5, 0.5, 1);

    # Head light
    glLightfv_p(GL_LIGHT1, GL_POSITION, 1, 0, 1, 0);
    
    if ($self->enable_picking) {
        # Render the object for picking.
        # FIXME This cannot possibly work in a multi-sampled context as the color gets mangled by the anti-aliasing.
        # Better to use software ray-casting on a bounding-box hierarchy.
        glDisable(GL_MULTISAMPLE);
        glDisable(GL_LIGHTING);
        $self->draw_volumes(1);
        glFlush();
        glFinish();
        
        if (my $pos = $self->_mouse_pos) {
            my $col = [ glReadPixels_p($pos->x, $self->GetSize->GetHeight - $pos->y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE) ];
            my $volume_idx = $col->[0] + $col->[1]*256 + $col->[2]*256*256;
            $self->_hover_volume_idx(undef);
            $_->hover(0) for @{$self->volumes};
            if ($volume_idx <= $#{$self->volumes}) {
                $self->_hover_volume_idx($volume_idx);
                
                $self->volumes->[$volume_idx]->hover(1);
                my $group_id = $self->volumes->[$volume_idx]->select_group_id;
                if ($group_id != -1) {
                    $_->hover(1) for grep { $_->select_group_id == $group_id } @{$self->volumes};
                }
                
                $self->on_hover->($volume_idx) if $self->on_hover;
            }
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glFlush();
        glFinish();
        glEnable(GL_LIGHTING);
        glEnable(GL_MULTISAMPLE);
    }
    
    # draw fixed background
    if ($self->background) {
        glDisable(GL_LIGHTING);
        glPushMatrix();
        glLoadIdentity();
        
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        
        # Draws a bluish bottom to top gradient over the complete screen.
        glDisable(GL_DEPTH_TEST);
        glBegin(GL_QUADS);
        glColor3f(0.0,0.0,0.0);
        glVertex3f(-1.0,-1.0, 1.0);
        glVertex3f( 1.0,-1.0, 1.0);
        glColor3f(10/255,98/255,144/255);
        glVertex3f( 1.0, 1.0, 1.0);
        glVertex3f(-1.0, 1.0, 1.0);
        glEnd();
        glPopMatrix();
        glEnable(GL_DEPTH_TEST);
        
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glEnable(GL_LIGHTING);
    }
    
    # draw ground and axes
    glDisable(GL_LIGHTING);
    
    # draw ground
    my $ground_z = GROUND_Z;
    if ($self->bed_triangles) {
        glDisable(GL_DEPTH_TEST);
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        glEnableClientState(GL_VERTEX_ARRAY);
        glColor4f(0.8, 0.6, 0.5, 0.4);
        glNormal3d(0,0,1);
        glVertexPointer_c(3, GL_FLOAT, 0, $self->bed_triangles->ptr());
        glDrawArrays(GL_TRIANGLES, 0, $self->bed_triangles->elements / 3);
        glDisableClientState(GL_VERTEX_ARRAY);
        
        # we need depth test for grid, otherwise it would disappear when looking
        # the object from below
        glEnable(GL_DEPTH_TEST);
    
        # draw grid
        glLineWidth(3);
        glColor4f(0.2, 0.2, 0.2, 0.4);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer_c(3, GL_FLOAT, 0, $self->bed_grid_lines->ptr());
        glDrawArrays(GL_LINES, 0, $self->bed_grid_lines->elements / 3);
        glDisableClientState(GL_VERTEX_ARRAY);
        
        glDisable(GL_BLEND);
    }
    
    my $volumes_bb = $self->volumes_bounding_box;
    
    {
        # draw axes
        # disable depth testing so that axes are not covered by ground
        glDisable(GL_DEPTH_TEST);
        my $origin = $self->origin;
        my $axis_len = max(
            0.3 * max(@{ $self->bed_bounding_box->size }),
              2 * max(@{ $volumes_bb->size }),
        );
        glLineWidth(2);
        glBegin(GL_LINES);
        # draw line for x axis
        glColor3f(1, 0, 0);
        glVertex3f(@$origin, $ground_z);
        glVertex3f($origin->x + $axis_len, $origin->y, $ground_z);  #,,
        # draw line for y axis
        glColor3f(0, 1, 0);
        glVertex3f(@$origin, $ground_z);
        glVertex3f($origin->x, $origin->y + $axis_len, $ground_z);  #++
        glEnd();
        # draw line for Z axis
        # (re-enable depth test so that axis is correctly shown when objects are behind it)
        glEnable(GL_DEPTH_TEST);
        glBegin(GL_LINES);
        glColor3f(0, 0, 1);
        glVertex3f(@$origin, $ground_z);
        glVertex3f(@$origin, $ground_z+$axis_len);
        glEnd();
    }
    
    glEnable(GL_LIGHTING);
    
    # draw objects
    $self->draw_volumes;

    # draw cutting plane
    if (defined $self->cutting_plane_z) {
        my $plane_z = $self->cutting_plane_z;
        my $bb = $volumes_bb;
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
        glColor4f(0.8, 0.8, 0.8, 0.5);
        glVertex3f($bb->x_min-20, $bb->y_min-20, $plane_z);
        glVertex3f($bb->x_max+20, $bb->y_min-20, $plane_z);
        glVertex3f($bb->x_max+20, $bb->y_max+20, $plane_z);
        glVertex3f($bb->x_min-20, $bb->y_max+20, $plane_z);
        glEnd();
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }

    $self->draw_active_object_annotations;
    
    $self->SwapBuffers();

    # Calling glFinish has a performance penalty, but it seems to fix some OpenGL driver hang-up with extremely large scenes.
    glFinish();
}

sub draw_volumes {
    # $fakecolor is a boolean indicating, that the objects shall be rendered in a color coding the object index for picking.
    my ($self, $fakecolor) = @_;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    
    my $z_cursor_relative = $self->_variable_layer_thickness_bar_mouse_cursor_z_relative;
    foreach my $volume_idx (0..$#{$self->volumes}) {
        my $volume = $self->volumes->[$volume_idx];

        my $shader_active = 0;
        if ($self->layer_editing_enabled && ! $fakecolor && $volume->selected && $self->{shader} && $volume->{layer_height_texture_data}) {
            my $print_object = $self->{print}->get_object(int($volume->select_group_id / 1000000));
            {
                # Update the height texture if the ModelObject::layer_height_texture is invalid.
                my $ncells = $print_object->generate_layer_height_texture(
                    $volume->{layer_height_texture_data}->ptr,
                    $self->{layer_preview_z_texture_height},
                    $self->{layer_preview_z_texture_width},
                    !defined($volume->{layer_height_texture_cells}));
                $volume->{layer_height_texture_cells} = $ncells if $ncells > 0;
            }
            $self->{shader}->Enable;
            my $z_to_texture_row_id             = $self->{shader}->Map('z_to_texture_row');
            my $z_texture_row_to_normalized_id  = $self->{shader}->Map('z_texture_row_to_normalized');
            my $z_cursor_id                     = $self->{shader}->Map('z_cursor');
            my $z_cursor_band_width_id          = $self->{shader}->Map('z_cursor_band_width');
            die if ! defined($z_to_texture_row_id);
            die if ! defined($z_texture_row_to_normalized_id);
            die if ! defined($z_cursor_id);
            die if ! defined($z_cursor_band_width_id);
            my $ncells = $volume->{layer_height_texture_cells};
            my $z_max = $volume->{bounding_box}->z_max;
            glUniform1fARB($z_to_texture_row_id, ($ncells - 1) / ($self->{layer_preview_z_texture_width} * $z_max));
            glUniform1fARB($z_texture_row_to_normalized_id, 1. / $self->{layer_preview_z_texture_height});
            glUniform1fARB($z_cursor_id, $z_max * $z_cursor_relative);
            glUniform1fARB($z_cursor_band_width_id, $self->{layer_height_edit_band_width});
            glBindTexture(GL_TEXTURE_2D, $self->{layer_preview_z_texture_id});
#            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_LEVEL, 0);
#            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
            if (1) {
                glTexImage2D_c(GL_TEXTURE_2D, 0, GL_RGBA8, $self->{layer_preview_z_texture_width}, $self->{layer_preview_z_texture_height}, 
                    0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
                glTexImage2D_c(GL_TEXTURE_2D, 1, GL_RGBA8, $self->{layer_preview_z_texture_width} / 2, $self->{layer_preview_z_texture_height} / 2,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#                glPixelStorei(GL_UNPACK_ROW_LENGTH, $self->{layer_preview_z_texture_width});
                glTexSubImage2D_c(GL_TEXTURE_2D, 0, 0, 0, $self->{layer_preview_z_texture_width}, $self->{layer_preview_z_texture_height},
                    GL_RGBA, GL_UNSIGNED_BYTE, $volume->{layer_height_texture_data}->ptr);
                glTexSubImage2D_c(GL_TEXTURE_2D, 1, 0, 0, $self->{layer_preview_z_texture_width} / 2, $self->{layer_preview_z_texture_height} / 2,
                    GL_RGBA, GL_UNSIGNED_BYTE, $volume->{layer_height_texture_data}->offset($self->{layer_preview_z_texture_width} * $self->{layer_preview_z_texture_height} * 4));
            } else {
                glTexImage2D_c(GL_TEXTURE_2D, 0, GL_RGBA8, $self->{layer_preview_z_texture_width}, $self->{layer_preview_z_texture_height}, 
                    0, GL_RGBA, GL_UNSIGNED_BYTE, $volume->{layer_height_texture_data}->ptr);
                glTexImage2D_c(GL_TEXTURE_2D, 1, GL_RGBA8, $self->{layer_preview_z_texture_width}/2, $self->{layer_preview_z_texture_height}/2, 
                    0, GL_RGBA, GL_UNSIGNED_BYTE, $volume->{layer_height_texture_data}->ptr + $self->{layer_preview_z_texture_width} * $self->{layer_preview_z_texture_height} * 4);
            }

#            my $nlines = ceil($ncells / ($self->{layer_preview_z_texture_width} - 1));

            $shader_active = 1;
        } elsif ($fakecolor) {
            # Object picking mode. Render the object with a color encoding the object index.
            my $r = ($volume_idx & 0x000000FF) >>  0;
            my $g = ($volume_idx & 0x0000FF00) >>  8;
            my $b = ($volume_idx & 0x00FF0000) >> 16;
            glColor4f($r/255.0, $g/255.0, $b/255.0, 1);
        } elsif ($volume->selected) {
            glColor4f(@{ &SELECTED_COLOR });
        } elsif ($volume->hover) {
            glColor4f(@{ &HOVER_COLOR });
        } else {
            glColor4f(@{ $volume->color });
        }

        my $qverts_begin = 0;
        my $qverts_end   = defined($volume->qverts) ? $volume->qverts->size() : 0;
        my $tverts_begin = 0;
        my $tverts_end   = defined($volume->tverts) ? $volume->tverts->size() : 0;
        my $n_offsets    = ($volume->range && $volume->offsets) ? scalar(@{$volume->offsets}) : 0;
        if ($n_offsets) {
            # The Z layer range is specified.
            # First test whether the Z span of this object is not out of ($min_z, $max_z) completely.
            my ($min_z, $max_z) = @{$volume->range};
            next if ($volume->offsets->[0] > $max_z || $volume->offsets->[-3] < $min_z);
            # Then find the lowest layer to be displayed.
            my $i = 0;
            while ($i < $n_offsets && $volume->offsets->[$i] < $min_z) {
                $i += 3;
            }
            # This shall not happen.
            next if ($i == $n_offsets);
            # Remember start of the layer.
            $qverts_begin = $volume->offsets->[$i+1];
            $tverts_begin = $volume->offsets->[$i+2];
            # Some layers are above $min_z. Which?
            while ($i < $n_offsets && $volume->offsets->[$i] <= $max_z) {
                $i += 3;
            }
            if ($i < $n_offsets) {
                $qverts_end = $volume->offsets->[$i+1];
                $tverts_end = $volume->offsets->[$i+2];
            }
        }
        
        glPushMatrix();
        glTranslatef(@{$volume->origin});

        glCullFace(GL_BACK);
        if ($qverts_begin < $qverts_end) {
            glVertexPointer_c(3, GL_FLOAT, 0, $volume->qverts->verts_ptr);
            glNormalPointer_c(GL_FLOAT, 0, $volume->qverts->norms_ptr);
            $qverts_begin /= 3;
            $qverts_end /= 3;
            my $nvertices = $qverts_end-$qverts_begin;
            while ($nvertices > 0) {
                my $nvertices_this = ($nvertices > 4096) ? 4096 : $nvertices;
                glDrawArrays(GL_QUADS, $qverts_begin, $nvertices_this);
                $qverts_begin += $nvertices_this;
                $nvertices -= $nvertices_this;
            }
        }
        
        if ($tverts_begin < $tverts_end) {
            glVertexPointer_c(3, GL_FLOAT, 0, $volume->tverts->verts_ptr);
            glNormalPointer_c(GL_FLOAT, 0, $volume->tverts->norms_ptr);
            $tverts_begin /= 3;
            $tverts_end /= 3;
            my $nvertices = $tverts_end-$tverts_begin;
            while ($nvertices > 0) {
                my $nvertices_this = ($nvertices > 4095) ? 4095 : $nvertices;
                glDrawArrays(GL_TRIANGLES, $tverts_begin, $nvertices_this);
                $tverts_begin += $nvertices_this;
                $nvertices -= $nvertices_this;
            }
        }

        glVertexPointer_c(3, GL_FLOAT, 0, 0);
        glNormalPointer_c(GL_FLOAT, 0, 0);
        glPopMatrix();

        if ($shader_active) {
            glBindTexture(GL_TEXTURE_2D, 0);
            $self->{shader}->Disable;
        }
    }
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisable(GL_BLEND);
    
    if (defined $self->cutting_plane_z) {
        glLineWidth(2);
        glColor3f(0, 0, 0);
        glVertexPointer_c(3, GL_FLOAT, 0, $self->cut_lines_vertices->ptr());
        glDrawArrays(GL_LINES, 0, $self->cut_lines_vertices->elements / 3);
        glVertexPointer_c(3, GL_FLOAT, 0, 0);
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}

sub _load_image_set_texture {
    my ($self, $file_name) = @_;
    # Load a PNG with an alpha channel.
    my $img = Wx::Image->new;
    $img->LoadFile($Slic3r::var->($file_name), wxBITMAP_TYPE_PNG);
    # Get RGB & alpha raw data from wxImage, interleave them into a Perl array.
    my @rgb = unpack 'C*', $img->GetData();
    my @alpha = $img->HasAlpha ? unpack 'C*', $img->GetAlpha() : (255) x (int(@rgb) / 3);
#    my @alpha = unpack 'C*', $img->GetAlpha();
    my $n_pixels = int(@alpha);
    my @data = (0)x($n_pixels * 4);
    for (my $i = 0; $i < $n_pixels; $i += 1) {
        $data[$i*4  ] = $rgb[$i*3];
        $data[$i*4+1] = $rgb[$i*3+1];
        $data[$i*4+2] = $rgb[$i*3+2];
        $data[$i*4+3] = $alpha[$i];
    }
    # Initialize a raw bitmap data.
    my $params = {
        loaded => 1,
        valid  => $n_pixels > 0,
        width  => $img->GetWidth, 
        height => $img->GetHeight,
        data   => OpenGL::Array->new_list(GL_UNSIGNED_BYTE, @data),
        texture_id => glGenTextures_p(1)
    };
    # Create and initialize a texture with the raw data.
    glBindTexture(GL_TEXTURE_2D, $params->{texture_id});
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    glTexImage2D_c(GL_TEXTURE_2D, 0, GL_RGBA8, $params->{width}, $params->{height}, 0, GL_RGBA, GL_UNSIGNED_BYTE, $params->{data}->ptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return $params;
}

sub _variable_layer_thickness_load_overlay_image {
    my ($self) = @_;
    $self->{layer_preview_annotation} = $self->_load_image_set_texture('variable_layer_height_tooltip.png')
        if (! $self->{layer_preview_annotation}->{loaded});
    return $self->{layer_preview_annotation}->{valid};
}

sub _variable_layer_thickness_load_reset_image {
    my ($self) = @_;
    $self->{layer_preview_reset_image} = $self->_load_image_set_texture('variable_layer_height_reset.png')
        if (! $self->{layer_preview_reset_image}->{loaded});
    return $self->{layer_preview_reset_image}->{valid};
}

# Paint the tooltip.
sub _render_image {
    my ($self, $image, $l, $r, $b, $t) = @_;
    glColor4f(1.,1.,1.,1.);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, $image->{texture_id});
    glBegin(GL_QUADS);
    glTexCoord2d(0.,1.); glVertex3f($l, $b, 0);
    glTexCoord2d(1.,1.); glVertex3f($r, $b, 0);
    glTexCoord2d(1.,0.); glVertex3f($r, $t, 0);
    glTexCoord2d(0.,0.); glVertex3f($l, $t, 0);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

sub draw_active_object_annotations {
    # $fakecolor is a boolean indicating, that the objects shall be rendered in a color coding the object index for picking.
    my ($self) = @_;

    return if (! $self->{shader} || ! $self->layer_editing_enabled);

    my $volume;
    foreach my $volume_idx (0..$#{$self->volumes}) {
        my $v = $self->volumes->[$volume_idx];
        if ($v->selected && $v->{layer_height_texture_data} && $v->{layer_height_texture_cells}) {
            $volume = $v;
            last;
        }
    }
    return if (! $volume);
    
    # The viewport and camera are set to complete view and glOrtho(-$x/2, $x/2, -$y/2, $y/2, -$depth, $depth), 
    # where x, y is the window size divided by $self->_zoom.
    my ($bar_left, $bar_bottom, $bar_right, $bar_top) = $self->_variable_layer_thickness_bar_rect_viewport;
    my ($reset_left, $reset_bottom, $reset_right, $reset_top) = $self->_variable_layer_thickness_reset_rect_viewport;
    my $z_cursor_relative = $self->_variable_layer_thickness_bar_mouse_cursor_z_relative;

    $self->{shader}->Enable;
    my $z_to_texture_row_id             = $self->{shader}->Map('z_to_texture_row');
    my $z_texture_row_to_normalized_id  = $self->{shader}->Map('z_texture_row_to_normalized');
    my $z_cursor_id                     = $self->{shader}->Map('z_cursor');
    my $ncells                          = $volume->{layer_height_texture_cells};
    my $z_max                           = $volume->{bounding_box}->z_max;
    glUniform1fARB($z_to_texture_row_id, ($ncells - 1) / ($self->{layer_preview_z_texture_width} * $z_max));
    glUniform1fARB($z_texture_row_to_normalized_id, 1. / $self->{layer_preview_z_texture_height});
    glUniform1fARB($z_cursor_id, $z_max * $z_cursor_relative);
    glBindTexture(GL_TEXTURE_2D, $self->{layer_preview_z_texture_id});
    glTexImage2D_c(GL_TEXTURE_2D, 0, GL_RGBA8, $self->{layer_preview_z_texture_width}, $self->{layer_preview_z_texture_height}, 
        0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexImage2D_c(GL_TEXTURE_2D, 1, GL_RGBA8, $self->{layer_preview_z_texture_width} / 2, $self->{layer_preview_z_texture_height} / 2,
        0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexSubImage2D_c(GL_TEXTURE_2D, 0, 0, 0, $self->{layer_preview_z_texture_width}, $self->{layer_preview_z_texture_height},
        GL_RGBA, GL_UNSIGNED_BYTE, $volume->{layer_height_texture_data}->ptr);
    glTexSubImage2D_c(GL_TEXTURE_2D, 1, 0, 0, $self->{layer_preview_z_texture_width} / 2, $self->{layer_preview_z_texture_height} / 2,
        GL_RGBA, GL_UNSIGNED_BYTE, $volume->{layer_height_texture_data}->offset($self->{layer_preview_z_texture_width} * $self->{layer_preview_z_texture_height} * 4));
    
    # Render the color bar.
    glDisable(GL_DEPTH_TEST);
    # The viewport and camera are set to complete view and glOrtho(-$x/2, $x/2, -$y/2, $y/2, -$depth, $depth), 
    # where x, y is the window size divided by $self->_zoom.
    glPushMatrix();
    glLoadIdentity();
    # Paint the overlay.
    glBegin(GL_QUADS);
    glVertex3f($bar_left,  $bar_bottom, 0);
    glVertex3f($bar_right, $bar_bottom, 0);
    glVertex3f($bar_right, $bar_top, $volume->{bounding_box}->z_max);
    glVertex3f($bar_left,  $bar_top, $volume->{bounding_box}->z_max);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    $self->{shader}->Disable;

    # Paint the tooltip.
    if ($self->_variable_layer_thickness_load_overlay_image) {
        my $gap = 10/$self->_zoom;
        my ($l, $r, $b, $t) = ($bar_left - $self->{layer_preview_annotation}->{width}/$self->_zoom - $gap, $bar_left - $gap, $reset_bottom + $self->{layer_preview_annotation}->{height}/$self->_zoom + $gap, $reset_bottom + $gap);
        $self->_render_image($self->{layer_preview_annotation}, $l, $r, $t, $b);
    }

    # Paint the reset button.
    if ($self->_variable_layer_thickness_load_reset_image) {
        $self->_render_image($self->{layer_preview_reset_image}, $reset_left, $reset_right, $reset_bottom, $reset_top);
    }

    # Paint the graph.
    #FIXME use the min / maximum layer height
    #FIXME show some kind of legend.
    my $object_idx = int($volume->select_group_id / 1000000);
    my $print_object = $self->{print}->get_object($object_idx);
    my $max_z = unscale($print_object->size->z);
    my $profile = $print_object->model_object->layer_height_profile;
    my $layer_height = $print_object->config->get('layer_height');
    # Baseline
    glColor3f(0., 0., 0.);
    glBegin(GL_LINE_STRIP);
    glVertex2f($bar_left + $layer_height * ($bar_right - $bar_left) / 0.45,  $bar_bottom);
    glVertex2f($bar_left + $layer_height * ($bar_right - $bar_left) / 0.45,  $bar_top);
    glEnd();
    # Curve
    glColor3f(0., 0., 1.);
    glBegin(GL_LINE_STRIP);
    for (my $i = 0; $i < int(@{$profile}); $i += 2) {
        my $z = $profile->[$i];
        my $h = $profile->[$i+1];
        glVertex3f($bar_left + $h * ($bar_right - $bar_left) / 0.45,  $bar_bottom + $z * ($bar_top - $bar_bottom) / $max_z, $z);
    }
    glEnd();
    # Revert the matrices.
    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
}

sub opengl_info
{
    my ($self, %params) = @_;
    my %tag = Slic3r::tags($params{format});

    my $gl_version       = glGetString(GL_VERSION);
    my $gl_vendor        = glGetString(GL_VENDOR);
    my $gl_renderer      = glGetString(GL_RENDERER);
    my $glsl_version_ARB = glGetString(GL_SHADING_LANGUAGE_VERSION_ARB) // '';
    my $glsl_version     = glGetString(GL_SHADING_LANGUAGE_VERSION) // $glsl_version_ARB;
    $glsl_version .= 'ARB(' . $glsl_version_ARB . ')' if ($glsl_version_ARB ne '' && $glsl_version ne $glsl_version_ARB);

    my $out = '';
    $out .= "$tag{h2start}OpenGL installation$tag{h2end}$tag{eol}";
    $out .= "  $tag{bstart}Using POGL$tag{bend} v$OpenGL::BUILD_VERSION$tag{eol}";
    $out .= "  $tag{bstart}GL version:   $tag{bend}${gl_version}$tag{eol}";
    $out .= "  $tag{bstart}vendor:       $tag{bend}${gl_vendor}$tag{eol}";
    $out .= "  $tag{bstart}renderer:     $tag{bend}${gl_renderer}$tag{eol}";
    $out .= "  $tag{bstart}GLSL version: $tag{bend}${glsl_version}$tag{eol}";

    # Check for required OpenGL extensions
    $out .= "$tag{h2start}Required extensions (* implemented):$tag{h2end}$tag{eol}";
    my @extensions_required = qw(GL_ARB_shader_objects GL_ARB_fragment_shader GL_ARB_vertex_shader GL_ARB_shading_language_100);
    foreach my $ext (sort @extensions_required) {
        my $stat = glpCheckExtension($ext);
        $out .= sprintf("%s ${ext}$tag{eol}", $stat?' ':'*');
        $out .= sprintf("    ${stat}$tag{eol}") if ($stat && $stat !~ m|^$ext |);
    }
    # Check for other OpenGL extensions
    $out .= "$tag{h2start}Installed extensions (* implemented in the module):$tag{h2end}$tag{eol}";
    my $extensions = glGetString(GL_EXTENSIONS);
    my @extensions = split(' ',$extensions);
    foreach my $ext (sort @extensions) {
        if(! grep(/^$extensions$/, @extensions_required)) {
            my $stat = glpCheckExtension($ext);
            $out .= sprintf("%s ${ext}$tag{eol}", $stat?' ':'*');
            $out .= sprintf("    ${stat}$tag{eol}") if ($stat && $stat !~ m|^$ext |);
        }
    }

    return $out;
}

sub _report_opengl_state
{
    my ($self, $comment) = @_;
    my $err = glGetError();
    return 0 if ($err == 0);
 
    # gluErrorString() hangs. Don't use it.
#    my $errorstr = gluErrorString();
    my $errorstr = '';
    if ($err == 0x0500) {
        $errorstr = 'GL_INVALID_ENUM';
    } elsif ($err == GL_INVALID_VALUE) {
        $errorstr = 'GL_INVALID_VALUE';
    } elsif ($err == GL_INVALID_OPERATION) {
        $errorstr = 'GL_INVALID_OPERATION';
    } elsif ($err == GL_STACK_OVERFLOW) {
        $errorstr = 'GL_STACK_OVERFLOW';
    } elsif ($err == GL_OUT_OF_MEMORY) {
        $errorstr = 'GL_OUT_OF_MEMORY';
    } else {        
        $errorstr = 'unknown';
    }
    if (defined($comment)) {
        printf("OpenGL error at %s, nr %d (0x%x): %s\n", $comment, $err, $err, $errorstr);
    } else {
        printf("OpenGL error nr %d (0x%x): %s\n", $err, $err, $errorstr);
    }
}

sub _vertex_shader {
    return <<'VERTEX';
#version 110

#define LIGHT_TOP_DIR        0., 1., 0.
#define LIGHT_TOP_DIFFUSE    0.2
#define LIGHT_TOP_SPECULAR   0.3

#define LIGHT_FRONT_DIR      0., 0., 1.
#define LIGHT_FRONT_DIFFUSE  0.5
#define LIGHT_FRONT_SPECULAR 0.3

#define INTENSITY_AMBIENT    0.1

uniform float z_to_texture_row;
varying float intensity_specular;
varying float intensity_tainted;
varying float object_z;

void main()
{
    vec3 eye, normal, lightDir, viewVector, halfVector;
    float NdotL, NdotHV;

//    eye = gl_ModelViewMatrixInverse[3].xyz;
    eye = vec3(0., 0., 1.);

    // First transform the normal into eye space and normalize the result.
    normal = normalize(gl_NormalMatrix * gl_Normal);
    
    // Now normalize the light's direction. Note that according to the OpenGL specification, the light is stored in eye space. 
    // Also since we're talking about a directional light, the position field is actually direction.
    lightDir = vec3(LIGHT_TOP_DIR);
    halfVector = normalize(lightDir + eye);
    
    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
    NdotL = max(dot(normal, lightDir), 0.0);

    intensity_tainted = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    intensity_specular = 0.;

//    if (NdotL > 0.0)
//        intensity_specular = LIGHT_TOP_SPECULAR * pow(max(dot(normal, halfVector), 0.0), gl_FrontMaterial.shininess);

    // Perform the same lighting calculation for the 2nd light source.
    lightDir = vec3(LIGHT_FRONT_DIR);
    halfVector = normalize(lightDir + eye);
    NdotL = max(dot(normal, lightDir), 0.0);
    intensity_tainted += NdotL * LIGHT_FRONT_DIFFUSE;
    
    // compute the specular term if NdotL is larger than zero
    if (NdotL > 0.0)
        intensity_specular += LIGHT_FRONT_SPECULAR * pow(max(dot(normal, halfVector), 0.0), gl_FrontMaterial.shininess);

    // Scaled to widths of the Z texture.
    object_z = gl_Vertex.z / gl_Vertex.w;

    gl_Position = ftransform();
} 

VERTEX
}

sub _fragment_shader {
    return <<'FRAGMENT';
#version 110

#define M_PI 3.1415926535897932384626433832795

// 2D texture (1D texture split by the rows) of color along the object Z axis.
uniform sampler2D z_texture;
// Scaling from the Z texture rows coordinate to the normalized texture row coordinate.
uniform float z_to_texture_row;
uniform float z_texture_row_to_normalized;

varying float intensity_specular;
varying float intensity_tainted;
varying float object_z;
uniform float z_cursor;
uniform float z_cursor_band_width;

void main()
{
    float object_z_row = z_to_texture_row * object_z;
    // Index of the row in the texture.
    float z_texture_row = floor(object_z_row);
    // Normalized coordinate from 0. to 1.
    float z_texture_col = object_z_row - z_texture_row;
    float z_blend = 0.25 * cos(min(M_PI, abs(M_PI * (object_z - z_cursor) * 1.8 / z_cursor_band_width))) + 0.25;
    // Calculate level of detail from the object Z coordinate.
    // This makes the slowly sloping surfaces to be show with high detail (with stripes),
    // and the vertical surfaces to be shown with low detail (no stripes)
    float z_in_cells    = object_z_row * 190.;
    // Gradient of Z projected on the screen.
    float dx_vtc        = dFdx(z_in_cells);
    float dy_vtc        = dFdy(z_in_cells);
    float lod           = clamp(0.5 * log2(max(dx_vtc*dx_vtc, dy_vtc*dy_vtc)), 0., 1.);
    // Sample the Z texture. Texture coordinates are normalized to <0, 1>.
    vec4 color       =
        (1. - lod) * texture2DLod(z_texture, vec2(z_texture_col, z_texture_row_to_normalized * (z_texture_row + 0.5    )), 0.) +
        lod        * texture2DLod(z_texture, vec2(z_texture_col, z_texture_row_to_normalized * (z_texture_row * 2. + 1.)), 1.);
    // Mix the final color.
    gl_FragColor = 
        vec4(intensity_specular, intensity_specular, intensity_specular, 1.) + 
        (1. - z_blend) * intensity_tainted * color + 
        z_blend * vec4(1., 1., 0., 0.);
    // and reset the transparency.
    gl_FragColor.a = 1.;
}

FRAGMENT
}

# Container for object geometry and selection status.
package Slic3r::GUI::3DScene::Volume;
use Moo;

has 'bounding_box'      => (is => 'ro', required => 1);
has 'origin'            => (is => 'rw', default => sub { Slic3r::Pointf3->new(0,0,0) });
has 'color'             => (is => 'ro', required => 1);
# An ID containing the object ID, volume ID and instance ID.
has 'composite_id'      => (is => 'rw', default => sub { -1 });
# An ID for group selection. It may be the same for all meshes of all object instances, or for just a single object instance.
has 'select_group_id'   => (is => 'rw', default => sub { -1 });
# An ID for group dragging. It may be the same for all meshes of all object instances, or for just a single object instance.
has 'drag_group_id'     => (is => 'rw', default => sub { -1 });
# Boolean: Is this object selected?
has 'selected'          => (is => 'rw', default => sub { 0 });
# Boolean: Is mouse over this object?
has 'hover'             => (is => 'rw', default => sub { 0 });
# Vector of two values: a span in the Z axis. Meaningful for a display of layers.
has 'range'             => (is => 'rw');

# Geometric data.
# Quads: GLVertexArray object: C++ class maintaining an std::vector<float> for coords and normals.
has 'qverts'            => (is => 'rw');  
# Triangles: GLVertexArray object
has 'tverts'            => (is => 'rw');
# If the qverts or tverts contain thick extrusions, then offsets keeps pointers of the starts
# of the extrusions per layer.
# The offsets stores tripples of (z_top, qverts_idx, tverts_idx) in a linear array.
has 'offsets'           => (is => 'rw');

# RGBA texture along the Z axis of an object, to visualize layers by stripes colored by their height.
has 'layer_height_texture_data'   => (is => 'rw');
# Number of texture cells.
has 'layer_height_texture_cells'  => (is => 'rw');

sub object_idx {
    my ($self) = @_;
    return int($self->composite_id / 1000000);
}

sub volume_idx {
    my ($self) = @_;
    return ($self->composite_id / 1000) % 1000;
}

sub instance_idx {
    my ($self) = @_;
    return $self->composite_id % 1000;
}

sub transformed_bounding_box {
    my ($self) = @_;
    
    my $bb = $self->bounding_box;
    $bb->translate(@{$self->origin});
    return $bb;
}


# The 3D canvas to display objects and tool paths.
package Slic3r::GUI::3DScene;
use base qw(Slic3r::GUI::3DScene::Base);

use OpenGL qw(:glconstants :gluconstants :glufunctions);
use List::Util qw(first min max);
use Slic3r::Geometry qw(scale unscale epsilon);
use Slic3r::Print::State ':steps';

# Perimeter: yellow, Infill: redish, Suport: greenish, last: blueish, 
use constant COLORS => [ [1,1,0,1], [1,0.5,0.5,1], [0.5,1,0.5,1], [0.5,0.5,1,1] ];

__PACKAGE__->mk_accessors(qw(
    color_by
    select_by
    drag_by
));

sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new(@_);
    $self->color_by('volume');      # object | volume
    $self->select_by('object');     # object | volume | instance
    $self->drag_by('instance');     # object | instance
    
    return $self;
}

sub load_object {
    my ($self, $model, $print, $obj_idx, $instance_idxs) = @_;
    
    my $model_object;
    if ($model->isa('Slic3r::Model::Object')) {
        $model_object = $model;
        $model = $model_object->model;
        $obj_idx = 0;
    } else {
        $model_object = $model->get_object($obj_idx);
    }
    
    $instance_idxs ||= [0..$#{$model_object->instances}];

    # Object will have a single common layer height texture for all volumes.
    my $layer_height_texture_data;
    my $layer_height_texture_cells;
    if ($print && $obj_idx < $print->object_count) {
        # Generate the layer height texture. Allocate data for the 0th and 1st mipmap levels.
        $layer_height_texture_data = OpenGL::Array->new($self->{layer_preview_z_texture_width}*$self->{layer_preview_z_texture_height}*5, GL_UNSIGNED_BYTE);
    }
    
    my @volumes_idx = ();
    foreach my $volume_idx (0..$#{$model_object->volumes}) {
        my $volume = $model_object->volumes->[$volume_idx];
        foreach my $instance_idx (@$instance_idxs) {
            my $instance = $model_object->instances->[$instance_idx];
            my $mesh = $volume->mesh->clone;
            $instance->transform_mesh($mesh);
            
            my $color_idx;
            if ($self->color_by eq 'volume') {
                $color_idx = $volume_idx;
            } elsif ($self->color_by eq 'object') {
                $color_idx = $obj_idx;
            }
        
            # Using the colors 'yellowish', 'greenish', 'blueish' for both the extrusion paths
            # and the volumes of a single multi-color object.
            #FIXME so for 4 or more color print, there will be only 3 colors displayed, which will
            # not correspond to the color of the filament.
            my $color = [ @{COLORS->[ $color_idx % scalar(@{&COLORS}) ]} ];
            $color->[3] = $volume->modifier ? 0.5 : 1;
            push @{$self->volumes}, my $v = Slic3r::GUI::3DScene::Volume->new(
                bounding_box    => $mesh->bounding_box,
                color           => $color,
            );
            $v->composite_id($obj_idx*1000000 + $volume_idx*1000 + $instance_idx);
            if ($self->select_by eq 'object') {
                $v->select_group_id($obj_idx*1000000);
            } elsif ($self->select_by eq 'volume') {
                $v->select_group_id($obj_idx*1000000 + $volume_idx*1000);
            } elsif ($self->select_by eq 'instance') {
                $v->select_group_id($v->composite_id);
            }
            if ($self->drag_by eq 'object') {
                $v->drag_group_id($obj_idx*1000);
            } elsif ($self->drag_by eq 'instance') {
                $v->drag_group_id($obj_idx*1000 + $instance_idx);
            }
            push @volumes_idx, my $scene_volume_idx = $#{$self->volumes};
            
            my $verts = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $verts->load_mesh($mesh);
            $v->tverts($verts);

            if (! $volume->modifier) {
                $v->layer_height_texture_data($layer_height_texture_data);
                $v->layer_height_texture_cells($layer_height_texture_cells);
            }
        }
    }
    
    return @volumes_idx;
}

# Called possibly by utils/view-toolpaths.pl, likely broken.
sub load_print_object_slices {
    my ($self, $object) = @_;
    
    my @verts = ();
    my @norms = ();
    my @quad_verts = ();
    my @quad_norms = ();
    foreach my $layer (@{$object->layers}) {
        my $gap = 0;
        my $top_z = $layer->print_z;
        my $bottom_z = $layer->print_z - $layer->height + $gap;
    
        foreach my $copy (@{ $object->_shifted_copies }) {
            {
                my @expolygons = map $_->clone, @{$layer->slices};
                $_->translate(@$copy) for @expolygons;
                $self->_expolygons_to_verts(\@expolygons, $layer->print_z, \@verts, \@norms);
            }
            foreach my $slice (@{$layer->slices}) {
                foreach my $polygon (@$slice) {
                    foreach my $line (@{$polygon->lines}) {
                        $line->translate(@$copy);
                        
                        push @quad_norms, (0,0,-1), (0,0,-1);
                        push @quad_verts, (map unscale($_), @{$line->a}), $bottom_z;
                        push @quad_verts, (map unscale($_), @{$line->b}), $bottom_z;
                        push @quad_norms, (0,0,1), (0,0,1);
                        push @quad_verts, (map unscale($_), @{$line->b}), $top_z;
                        push @quad_verts, (map unscale($_), @{$line->a}), $top_z;
                        
                        # We'll use this for the middle normal when using 4 quads:
                        #my $xy_normal = $line->normal;
                        #$_xynormal->scale(1/$line->length);
                    }
                }
            }
        }
    }
    
    my $obb = $object->bounding_box;
    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
    $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$obb->min_point}, 0));
    $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$obb->max_point}, $object->size->z));
    
    push @{$self->volumes}, my $v = Slic3r::GUI::3DScene::Volume->new(
        bounding_box    => $bb,
        color           => COLORS->[0],
        verts           => OpenGL::Array->new_list(GL_FLOAT, @verts),
        norms           => OpenGL::Array->new_list(GL_FLOAT, @norms),
        quad_verts      => OpenGL::Array->new_list(GL_FLOAT, @quad_verts),
        quad_norms      => OpenGL::Array->new_list(GL_FLOAT, @quad_norms),
    );
}

# Create 3D thick extrusion lines for a skirt and brim.
# Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes.
sub load_print_toolpaths {
    my ($self, $print) = @_;
    
    return if !$print->step_done(STEP_SKIRT);
    return if !$print->step_done(STEP_BRIM);
    return if !$print->has_skirt && $print->config->brim_width == 0;
    
    my $qverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $tverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $offsets = [];  # triples stored in a linear array, sorted by print_z: print_z, qverts, tverts
    
    my $skirt_height = 0;  # number of layers
    if ($print->has_infinite_skirt) {
        $skirt_height = $print->total_layer_count;
    } else {
        $skirt_height = min($print->config->skirt_height, $print->total_layer_count);
    }
    $skirt_height ||= 1 if $print->config->brim_width > 0;
    
    # get first $skirt_height layers (maybe this should be moved to a PrintObject method?)
    my $object0 = $print->get_object(0);
    my @layers = ();
    push @layers, map $object0->get_layer($_-1), 1..min($skirt_height, $object0->layer_count);
    push @layers, map $object0->get_support_layer($_-1), 1..min($skirt_height, $object0->support_layer_count);
    @layers = sort { $a->print_z <=> $b->print_z } @layers;
    @layers = @layers[0..($skirt_height-1)];
    
    foreach my $i (0..($skirt_height-1)) {
        my $top_z = $layers[$i]->print_z;
        push @$offsets, ($top_z, $qverts->size, $tverts->size);
        
        if ($i == 0) {
            $self->_extrusionentity_to_verts($print->brim, $top_z, Slic3r::Point->new(0,0), $qverts, $tverts);
        }
        
        $self->_extrusionentity_to_verts($print->skirt, $top_z, Slic3r::Point->new(0,0), $qverts, $tverts);
    }

    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
    {
        my $pbb = $print->bounding_box;
        $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$pbb->min_point}));
        $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$pbb->max_point}));
    }
    push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
        bounding_box    => $bb,
        color           => COLORS->[2],
        qverts          => $qverts,
        tverts          => $tverts,
        offsets         => $offsets,
    );
}

# Create 3D thick extrusion lines for object forming extrusions.
# Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes,
# one for perimeters, one for infill and one for supports.
sub load_print_object_toolpaths {
    my ($self, $object) = @_;
    
    my $perim_qverts    = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $perim_tverts    = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $infill_qverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $infill_tverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $support_qverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $support_tverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    
    my $perim_offsets   = [];  # triples of (print_z, qverts, tverts), stored linearly, sorted by print_z
    my $infill_offsets  = [];
    my $support_offsets = [];
    
    # order layers by print_z
    my @layers = sort { $a->print_z <=> $b->print_z }
        @{$object->layers}, @{$object->support_layers};
    
    # Bounding box of the object and its copies.
    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
    {
        my $obb = $object->bounding_box;
        foreach my $copy (@{ $object->_shifted_copies }) {
            my $cbb = $obb->clone;
            $cbb->translate(@$copy);
            $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$cbb->min_point}, 0));
            $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$cbb->max_point}, $object->size->z));
        }
    }

    # Maximum size of an allocation block: 32MB / sizeof(float)
    my $alloc_size_max = 32 * 1048576 / 4;
    
    foreach my $layer (@layers) {
        my $top_z = $layer->print_z;
        
        push @$perim_offsets, ($top_z, $perim_qverts->size, $perim_tverts->size)
            if (!@$perim_offsets || $perim_offsets->[-3] != $top_z);
        push @$infill_offsets, ($top_z, $infill_qverts->size, $infill_tverts->size)
            if (!@$infill_offsets || $infill_offsets->[-3] != $top_z);
        push @$support_offsets, ($top_z, $support_qverts->size, $support_tverts->size)
            if (!@$support_offsets || $support_offsets->[-3] != $top_z);

        foreach my $copy (@{ $object->_shifted_copies }) {
            foreach my $layerm (@{$layer->regions}) {
                if ($object->step_done(STEP_PERIMETERS)) {
                    $self->_extrusionentity_to_verts($layerm->perimeters, $top_z, $copy,
                        $perim_qverts, $perim_tverts);
                }
                
                if ($object->step_done(STEP_INFILL)) {
                    $self->_extrusionentity_to_verts($layerm->fills, $top_z, $copy,
                        $infill_qverts, $infill_tverts);
                }
            }
            
            if ($layer->isa('Slic3r::Layer::Support') && $object->step_done(STEP_SUPPORTMATERIAL)) {
                $self->_extrusionentity_to_verts($layer->support_fills, $top_z, $copy,
                    $support_qverts, $support_tverts);
                
                $self->_extrusionentity_to_verts($layer->support_interface_fills, $top_z, $copy,
                    $support_qverts, $support_tverts);
            }
        }

        if ($perim_qverts->size() > $alloc_size_max || $perim_tverts->size() > $alloc_size_max) {
            # Store the vertex arrays and restart their containers.
            push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
                bounding_box    => $bb,
                color           => COLORS->[0],
                qverts          => $perim_qverts,
                tverts          => $perim_tverts,
                offsets         => $perim_offsets,
            );
            $perim_qverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $perim_tverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $perim_offsets  = [];
        }

        if ($infill_qverts->size() > $alloc_size_max || $infill_tverts->size() > $alloc_size_max) {
            # Store the vertex arrays and restart their containers.
            push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
                bounding_box    => $bb,
                color           => COLORS->[1],
                qverts          => $infill_qverts,
                tverts          => $infill_tverts,
                offsets         => $infill_offsets,
            );
            $infill_qverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $infill_tverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $infill_offsets  = [];
        }

        if ($support_qverts->size() > $alloc_size_max || $support_tverts->size() > $alloc_size_max) {
            # Store the vertex arrays and restart their containers.
            push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
                bounding_box    => $bb,
                color           => COLORS->[2],
                qverts          => $support_qverts,
                tverts          => $support_tverts,
                offsets         => $support_offsets,
            );
            $support_qverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $support_tverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $support_offsets  = [];
        }
    }

    if ($perim_qverts->size() > 0 || $perim_tverts->size() > 0) {
        push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
            bounding_box    => $bb,
            color           => COLORS->[0],
            qverts          => $perim_qverts,
            tverts          => $perim_tverts,
            offsets         => $perim_offsets,
        );
    }
    
    if ($infill_qverts->size() > 0 || $infill_tverts->size() > 0) {
        push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
            bounding_box    => $bb,
            color           => COLORS->[1],
            qverts          => $infill_qverts,
            tverts          => $infill_tverts,
            offsets         => $infill_offsets,
        );
    }
    
    if ($support_qverts->size() > 0 || $support_tverts->size() > 0) {
        push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
            bounding_box    => $bb,
            color           => COLORS->[2],
            qverts          => $support_qverts,
            tverts          => $support_tverts,
            offsets         => $support_offsets,
        );
    }
}

sub set_toolpaths_range {
    my ($self, $min_z, $max_z) = @_;
    
    foreach my $volume (@{$self->volumes}) {
        $volume->range([ $min_z, $max_z ]);
    }
}

# called by load_print_object_slices, probably not used.
sub _expolygons_to_verts {
    my ($self, $expolygons, $z, $verts, $norms) = @_;
    
    my $tess = gluNewTess();
    gluTessCallback($tess, GLU_TESS_BEGIN,     'DEFAULT');
    gluTessCallback($tess, GLU_TESS_END,       'DEFAULT');
    gluTessCallback($tess, GLU_TESS_VERTEX, sub {
        my ($x, $y, $z) = @_;
        push @$verts, $x, $y, $z;
        push @$norms, (0,0,1), (0,0,1), (0,0,1);
    });
    gluTessCallback($tess, GLU_TESS_COMBINE,   'DEFAULT');
    gluTessCallback($tess, GLU_TESS_ERROR,     'DEFAULT');
    gluTessCallback($tess, GLU_TESS_EDGE_FLAG, 'DEFAULT');
    
    foreach my $expolygon (@$expolygons) {
        gluTessBeginPolygon($tess);
        foreach my $polygon (@$expolygon) {
            gluTessBeginContour($tess);
            gluTessVertex_p($tess, (map unscale($_), @$_), $z) for @$polygon;
            gluTessEndContour($tess);
        }
        gluTessEndPolygon($tess);
    }
    
    gluDeleteTess($tess);
}

# Fill in the $qverts and $tverts with quads and triangles
# for the extrusion $entity.
sub _extrusionentity_to_verts {
    my ($self, $entity, $top_z, $copy, $qverts, $tverts) = @_;
    
    my ($lines, $widths, $heights, $closed);
    if ($entity->isa('Slic3r::ExtrusionPath::Collection')) {
        $self->_extrusionentity_to_verts($_, $top_z, $copy, $qverts, $tverts)
            for @$entity;
        return;
    } elsif ($entity->isa('Slic3r::ExtrusionPath')) {
        my $polyline = $entity->polyline->clone;
        $polyline->remove_duplicate_points;
        $polyline->translate(@$copy);
        $lines = $polyline->lines;
        $widths = [ map $entity->width, 0..$#$lines ];
        $heights = [ map $entity->height, 0..$#$lines ];
        $closed = 0;
    } else {
        $lines   = [];
        $widths  = [];
        $heights = [];
        # $entity is either of type Slic3r::ExtrusionLoop or Slic3r::ExtrusionMultiPath.
        $closed  = $entity->isa('Slic3r::ExtrusionLoop') ? 1 : 0;
        foreach my $path (@$entity) {
            my $polyline = $path->polyline->clone;
            $polyline->remove_duplicate_points;
            $polyline->translate(@$copy);
            my $path_lines = $polyline->lines;
            push @$lines, @$path_lines;
            push @$widths, map $path->width, 0..$#$path_lines;
            push @$heights, map $path->height, 0..$#$path_lines;
        }
    }
    # Calling the C++ implementation Slic3r::_3DScene::_extrusionentity_to_verts_do()
    # This adds new vertices to the $qverts and $tverts.
    Slic3r::GUI::_3DScene::_extrusionentity_to_verts_do($lines, $widths, $heights,
        $closed, 
        # Top height of the extrusion.
        $top_z, 
        # $copy is not used here.
        $copy,
        # GLVertexArray object: C++ class maintaining an std::vector<float> for coords and normals.
        $qverts,
        $tverts);
}

1;
