# Implements pure perl packages
#
# Slic3r::GUI::3DScene::Base;
# Slic3r::GUI::3DScene;
#
# Slic3r::GUI::Plater::3D derives from Slic3r::GUI::3DScene,
# Slic3r::GUI::Plater::3DPreview,
# Slic3r::GUI::Plater::ObjectCutDialog and Slic3r::GUI::Plater::ObjectPartsPanel
# own $self->{canvas} of the Slic3r::GUI::3DScene type.
#
# Therefore the 3DScene supports renderng of STLs, extrusions and cutting planes,
# and camera manipulation.

package Slic3r::GUI::3DScene::Base;
use strict;
use warnings;

use Wx qw(wxTheApp :timer :bitmap :icon :dialog);
#==============================================================================================================================
#use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS EVT_CHAR EVT_TIMER);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions :gluconstants);
use base qw(Wx::GLCanvas Class::Accessor);
#==============================================================================================================================
#use Math::Trig qw(asin tan);
#use List::Util qw(reduce min max first);
#use Slic3r::Geometry qw(X Y normalize scale unscale scaled_epsilon);
#use Slic3r::Geometry::Clipper qw(offset_ex intersection_pl JT_ROUND);
#==============================================================================================================================
use Wx::GLCanvas qw(:all);
#==============================================================================================================================
#use Slic3r::Geometry qw(PI);
#==============================================================================================================================

# volumes: reference to vector of Slic3r::GUI::3DScene::Volume.
#==============================================================================================================================
#__PACKAGE__->mk_accessors( qw(_quat _dirty init
#                              enable_picking
#                              enable_moving
#                              use_plain_shader
#                              on_viewport_changed
#                              on_hover
#                              on_select
#                              on_double_click
#                              on_right_click
#                              on_move
#                              on_model_update
#                              volumes
#                              _sphi _stheta
#                              cutting_plane_z
#                              cut_lines_vertices
#                              bed_shape
#                              bed_triangles
#                              bed_grid_lines
#                              bed_polygon
#                              background
#                              origin
#                              _mouse_pos
#                              _hover_volume_idx
#
#                              _drag_volume_idx
#                              _drag_start_pos
#                              _drag_volume_center_offset
#                              _drag_start_xy
#                              _dragged
#
#                              _layer_height_edited
#
#                              _camera_type
#                              _camera_target
#                              _camera_distance
#                              _zoom
#                              
#                              _legend_enabled
#                              _warning_enabled
#                              _apply_zoom_to_volumes_filter
#                              _mouse_dragging
#                                                            
#                              ) );
#
#use constant TRACKBALLSIZE  => 0.8;
#use constant TURNTABLE_MODE => 1;
#use constant GROUND_Z       => -0.02;
## For mesh selection: Not selected - bright yellow.
#use constant DEFAULT_COLOR  => [1,1,0];
## For mesh selection: Selected - bright green.
#use constant SELECTED_COLOR => [0,1,0,1];
## For mesh selection: Mouse hovers over the object, but object not selected yet - dark green.
#use constant HOVER_COLOR    => [0.4,0.9,0,1];
#
## phi / theta angles to orient the camera.
#use constant VIEW_DEFAULT    => [45.0,45.0];
#use constant VIEW_LEFT       => [90.0,90.0];
#use constant VIEW_RIGHT      => [-90.0,90.0];
#use constant VIEW_TOP        => [0.0,0.0];
#use constant VIEW_BOTTOM     => [0.0,180.0];
#use constant VIEW_FRONT      => [0.0,90.0];
#use constant VIEW_REAR       => [180.0,90.0];
#
#use constant MANIPULATION_IDLE          => 0;
#use constant MANIPULATION_DRAGGING      => 1;
#use constant MANIPULATION_LAYER_HEIGHT  => 2;
#
#use constant GIMBALL_LOCK_THETA_MAX => 180;
#
#use constant VARIABLE_LAYER_THICKNESS_BAR_WIDTH => 70;
#use constant VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT => 22;
#
## make OpenGL::Array thread-safe
#{
#    no warnings 'redefine';
#    *OpenGL::Array::CLONE_SKIP = sub { 1 };
#}
#==============================================================================================================================

sub new {
    my ($class, $parent) = @_;
    
    # We can only enable multi sample anti aliasing wih wxWidgets 3.0.3 and with a hacked Wx::GLCanvas,
    # which exports some new WX_GL_XXX constants, namely WX_GL_SAMPLE_BUFFERS and WX_GL_SAMPLES.
    my $can_multisample =
        ! wxTheApp->{app_config}->get('use_legacy_opengl') &&
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
#==============================================================================================================================
#    if (Wx::wxVERSION >= 3.000003) {
#        # Wx 3.0.3 contains an ugly hack to support some advanced OpenGL attributes through the attribute list.
#        # The attribute list is transferred between the wxGLCanvas and wxGLContext constructors using a single static array s_wglContextAttribs.
#        # Immediatelly force creation of the OpenGL context to consume the static variable s_wglContextAttribs.
#        $self->GetContext();
#    }
#==============================================================================================================================

#==============================================================================================================================
    Slic3r::GUI::_3DScene::add_canvas($self);
    Slic3r::GUI::_3DScene::allow_multisample($self, $can_multisample);
#    my $context = $self->GetContext;
#    $self->SetCurrent($context);
#    Slic3r::GUI::_3DScene::add_canvas($self, $context);
#    
#    $self->{can_multisample} = $can_multisample;
#    $self->background(1);
#    $self->_quat((0, 0, 0, 1));
#    $self->_stheta(45);
#    $self->_sphi(45);
#    $self->_zoom(1);
#    $self->_legend_enabled(0);
#    $self->_warning_enabled(0);
#    $self->use_plain_shader(0);
#    $self->_apply_zoom_to_volumes_filter(0);
#    $self->_mouse_dragging(0);
#
#    # Collection of GLVolume objects
#    $self->volumes(Slic3r::GUI::_3DScene::GLVolume::Collection->new);
#
#    # 3D point in model space
#    $self->_camera_type('ortho');
##    $self->_camera_type('perspective');
#    $self->_camera_target(Slic3r::Pointf3->new(0,0,0));
#    $self->_camera_distance(0.);
#    $self->layer_editing_enabled(0);
#    $self->{layer_height_edit_band_width} = 2.;
#    $self->{layer_height_edit_strength} = 0.005;
#    $self->{layer_height_edit_last_object_id} = -1;
#    $self->{layer_height_edit_last_z} = 0.;
#    $self->{layer_height_edit_last_action} = 0;
#
#    $self->reset_objects;
#    
#    EVT_PAINT($self, sub {
#        my $dc = Wx::PaintDC->new($self);
#        $self->Render($dc);
#    });
#    EVT_SIZE($self, sub { $self->_dirty(1) });
#    EVT_IDLE($self, sub {
#        return unless $self->_dirty;
#        return if !$self->IsShownOnScreen;
#        $self->Resize( $self->GetSizeWH );
#        $self->Refresh;
#    });
#    EVT_MOUSEWHEEL($self, \&mouse_wheel_event);
#    EVT_MOUSE_EVENTS($self, \&mouse_event);
##    EVT_KEY_DOWN($self, sub {
#    EVT_CHAR($self, sub {
#        my ($s, $event) = @_;
#        if ($event->HasModifiers) {
#            $event->Skip;
#        } else {
#            my $key = $event->GetKeyCode;
#            if ($key == ord('0')) {
#                $self->select_view('iso');
#            } elsif ($key == ord('1')) {
#                $self->select_view('top');
#            } elsif ($key == ord('2')) {
#                $self->select_view('bottom');
#            } elsif ($key == ord('3')) {
#                $self->select_view('front');
#            } elsif ($key == ord('4')) {
#                $self->select_view('rear');
#            } elsif ($key == ord('5')) {
#                $self->select_view('left');
#            } elsif ($key == ord('6')) {
#                $self->select_view('right');
#            } elsif ($key == ord('z')) {
#                $self->zoom_to_volumes;
#            } elsif ($key == ord('b')) {
#                $self->zoom_to_bed;
#            } else {
#                $event->Skip;
#            }
#        }
#    });
#    
#    $self->{layer_height_edit_timer_id} = &Wx::NewId();
#    $self->{layer_height_edit_timer} = Wx::Timer->new($self, $self->{layer_height_edit_timer_id});
#    EVT_TIMER($self, $self->{layer_height_edit_timer_id}, sub {
#        my ($self, $event) = @_;
#        return if $self->_layer_height_edited != 1;
#        $self->_variable_layer_thickness_action(undef);
#    });
#==============================================================================================================================
    
    return $self;
}

#==============================================================================================================================
#sub set_legend_enabled {
#    my ($self, $value) = @_;
#    $self->_legend_enabled($value);
#}
#
#sub set_warning_enabled {
#    my ($self, $value) = @_;
#    $self->_warning_enabled($value);
#}
#==============================================================================================================================

sub Destroy {
    my ($self) = @_;
#==============================================================================================================================
    Slic3r::GUI::_3DScene::remove_canvas($self);    
#    $self->{layer_height_edit_timer}->Stop;
#    $self->DestroyGL;
#==============================================================================================================================
    return $self->SUPER::Destroy;
}

#==============================================================================================================================
#sub layer_editing_enabled {
#    my ($self, $value) = @_;
#    if (@_ == 2) {
#        $self->{layer_editing_enabled} = $value;
#        if ($value) {
#            if (! $self->{layer_editing_initialized}) {
#                # Enabling the layer editing for the first time. This triggers compilation of the necessary OpenGL shaders.
#                # If compilation fails, a message box is shown with the error codes.
#                $self->SetCurrent($self->GetContext);
#                my $shader = new Slic3r::GUI::_3DScene::GLShader;
#                my $error_message;
#                if (! $shader->load_from_text($self->_fragment_shader_variable_layer_height, $self->_vertex_shader_variable_layer_height)) {
#                    # Compilation or linking of the shaders failed.
#                    $error_message = "Cannot compile an OpenGL Shader, therefore the Variable Layer Editing will be disabled.\n\n" 
#                        . $shader->last_error;
#                    $shader = undef;
#                } else {
#                    $self->{layer_height_edit_shader} = $shader;
#                    ($self->{layer_preview_z_texture_id}) = glGenTextures_p(1);
#                    glBindTexture(GL_TEXTURE_2D, $self->{layer_preview_z_texture_id});
#                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
#                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
#                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
#                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
#                    glBindTexture(GL_TEXTURE_2D, 0);
#                }
#                if (defined($error_message)) {
#                    # Don't enable the layer editing tool.
#                    $self->{layer_editing_enabled} = 0;
#                    # 2 means failed
#                    $self->{layer_editing_initialized} = 2;
#                    # Show the error message.
#                    Wx::MessageBox($error_message, "Slic3r Error", wxOK | wxICON_EXCLAMATION, $self);
#                } else {
#                    $self->{layer_editing_initialized} = 1;
#                }
#            } elsif ($self->{layer_editing_initialized} == 2) {
#                # Initilization failed before. Don't try to initialize and disable layer editing.
#                $self->{layer_editing_enabled} = 0;
#            }
#        }
#    }
#    return $self->{layer_editing_enabled};
#}
#
#sub layer_editing_allowed {
#    my ($self) = @_;
#    # Allow layer editing if either the shaders were not initialized yet and we don't know
#    # whether it will be possible to initialize them, 
#    # or if the initialization was done already and it failed.
#    return ! (defined($self->{layer_editing_initialized}) && $self->{layer_editing_initialized} == 2);
#}
#
#sub _first_selected_object_id_for_variable_layer_height_editing {
#    my ($self) = @_;
#    for my $i (0..$#{$self->volumes}) {
#        if ($self->volumes->[$i]->selected) {
#            my $object_id = int($self->volumes->[$i]->select_group_id / 1000000);
#            # Objects with object_id >= 1000 have a specific meaning, for example the wipe tower proxy.
#            return ($object_id >= $self->{print}->object_count) ? -1 : $object_id
#                if $object_id < 10000;
#        }
#    }
#    return -1;
#}
#
## Returns an array with (left, top, right, bottom) of the variable layer thickness bar on the screen.
#sub _variable_layer_thickness_bar_rect_screen {
#    my ($self) = @_;
#    my ($cw, $ch) = $self->GetSizeWH;
#    return ($cw - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, 0, $cw, $ch - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT);
#}
#
#sub _variable_layer_thickness_bar_rect_viewport {
#    my ($self) = @_;
#    my ($cw, $ch) = $self->GetSizeWH;
#    return ((0.5*$cw-VARIABLE_LAYER_THICKNESS_BAR_WIDTH)/$self->_zoom, (-0.5*$ch+VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT)/$self->_zoom, $cw/(2*$self->_zoom), $ch/(2*$self->_zoom));
#}
#
## Returns an array with (left, top, right, bottom) of the variable layer thickness bar on the screen.
#sub _variable_layer_thickness_reset_rect_screen {
#    my ($self) = @_;
#    my ($cw, $ch) = $self->GetSizeWH;
#    return ($cw - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, $ch - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT, $cw, $ch);
#}
#
#sub _variable_layer_thickness_reset_rect_viewport {
#    my ($self) = @_;
#    my ($cw, $ch) = $self->GetSizeWH;
#    return ((0.5*$cw-VARIABLE_LAYER_THICKNESS_BAR_WIDTH)/$self->_zoom, -$ch/(2*$self->_zoom), $cw/(2*$self->_zoom), (-0.5*$ch+VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT)/$self->_zoom);
#}
#
#sub _variable_layer_thickness_bar_rect_mouse_inside {
#   my ($self, $mouse_evt) = @_;
#   my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_bar_rect_screen;
#   return $mouse_evt->GetX >= $bar_left && $mouse_evt->GetX <= $bar_right && $mouse_evt->GetY >= $bar_top && $mouse_evt->GetY <= $bar_bottom;
#}
#
#sub _variable_layer_thickness_reset_rect_mouse_inside {
#   my ($self, $mouse_evt) = @_;
#   my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_reset_rect_screen;
#   return $mouse_evt->GetX >= $bar_left && $mouse_evt->GetX <= $bar_right && $mouse_evt->GetY >= $bar_top && $mouse_evt->GetY <= $bar_bottom;
#}
#
#sub _variable_layer_thickness_bar_mouse_cursor_z_relative {
#   my ($self) = @_;
#   my $mouse_pos = $self->ScreenToClientPoint(Wx::GetMousePosition());
#   my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_bar_rect_screen;
#   return ($mouse_pos->x >= $bar_left && $mouse_pos->x <= $bar_right && $mouse_pos->y >= $bar_top && $mouse_pos->y <= $bar_bottom) ?
#        # Inside the bar.
#        ($bar_bottom - $mouse_pos->y - 1.) / ($bar_bottom - $bar_top - 1) :
#        # Outside the bar.
#        -1000.;
#}
#
#sub _variable_layer_thickness_action {
#    my ($self, $mouse_event, $do_modification) = @_;
#    # A volume is selected. Test, whether hovering over a layer thickness bar.
#    return if $self->{layer_height_edit_last_object_id} == -1;
#    if (defined($mouse_event)) {
#        my ($bar_left, $bar_top, $bar_right, $bar_bottom) = $self->_variable_layer_thickness_bar_rect_screen;
#        $self->{layer_height_edit_last_z} = unscale($self->{print}->get_object($self->{layer_height_edit_last_object_id})->size->z)
#            * ($bar_bottom - $mouse_event->GetY - 1.) / ($bar_bottom - $bar_top);
#        $self->{layer_height_edit_last_action} = $mouse_event->ShiftDown ? ($mouse_event->RightIsDown ? 3 : 2) : ($mouse_event->RightIsDown ? 0 : 1);
#    }
#    # Mark the volume as modified, so Print will pick its layer height profile? Where to mark it?
#    # Start a timer to refresh the print? schedule_background_process() ?
#    # The PrintObject::adjust_layer_height_profile() call adjusts the profile of its associated ModelObject, it does not modify the profile of the PrintObject itself.
#    $self->{print}->get_object($self->{layer_height_edit_last_object_id})->adjust_layer_height_profile(
#        $self->{layer_height_edit_last_z},
#        $self->{layer_height_edit_strength},
#        $self->{layer_height_edit_band_width}, 
#        $self->{layer_height_edit_last_action});
#             
#    $self->volumes->[$self->{layer_height_edit_last_object_id}]->generate_layer_height_texture(
#        $self->{print}->get_object($self->{layer_height_edit_last_object_id}), 1);
#    $self->Refresh;
#    # Automatic action on mouse down with the same coordinate.
#    $self->{layer_height_edit_timer}->Start(100, wxTIMER_CONTINUOUS);
#}
#
#sub mouse_event {
#    my ($self, $e) = @_;
#    
#    my $pos = Slic3r::Pointf->new($e->GetPositionXY);
#    my $object_idx_selected = $self->{layer_height_edit_last_object_id} = ($self->layer_editing_enabled && $self->{print}) ? $self->_first_selected_object_id_for_variable_layer_height_editing : -1;
#
#    $self->_mouse_dragging($e->Dragging);
#    
#    if ($e->Entering && (&Wx::wxMSW || $^O eq 'linux')) {
#        # wxMSW needs focus in order to catch mouse wheel events
#        $self->SetFocus;
#        $self->_drag_start_xy(undef);        
#    } elsif ($e->LeftDClick) {
#        if ($object_idx_selected != -1 && $self->_variable_layer_thickness_bar_rect_mouse_inside($e)) {
#        } elsif ($self->on_double_click) {
#            $self->on_double_click->();
#        }
#    } elsif ($e->LeftDown || $e->RightDown) {
#        # If user pressed left or right button we first check whether this happened
#        # on a volume or not.
#        my $volume_idx = $self->_hover_volume_idx // -1;
#        $self->_layer_height_edited(0);
#        if ($object_idx_selected != -1 && $self->_variable_layer_thickness_bar_rect_mouse_inside($e)) {
#            # A volume is selected and the mouse is hovering over a layer thickness bar.
#            # Start editing the layer height.
#            $self->_layer_height_edited(1);
#            $self->_variable_layer_thickness_action($e);
#        } elsif ($object_idx_selected != -1 && $self->_variable_layer_thickness_reset_rect_mouse_inside($e)) {
#            $self->{print}->get_object($object_idx_selected)->reset_layer_height_profile;
#            # Index 2 means no editing, just wait for mouse up event.
#            $self->_layer_height_edited(2);
#            $self->Refresh;
#            $self->Update;
#        } else {
#            # The mouse_to_3d gets the Z coordinate from the Z buffer at the screen coordinate $pos->x,y,
#            # an converts the screen space coordinate to unscaled object space.
#            my $pos3d = ($volume_idx == -1) ? undef : $self->mouse_to_3d(@$pos);
#
#            # Select volume in this 3D canvas.
#            # Don't deselect a volume if layer editing is enabled. We want the object to stay selected
#            # during the scene manipulation.
#
#            if ($self->enable_picking && ($volume_idx != -1 || ! $self->layer_editing_enabled)) {
#                $self->deselect_volumes;
#                $self->select_volume($volume_idx);
#                
#                if ($volume_idx != -1) {
#                    my $group_id = $self->volumes->[$volume_idx]->select_group_id;
#                    my @volumes;
#                    if ($group_id != -1) {
#                        $self->select_volume($_)
#                            for grep $self->volumes->[$_]->select_group_id == $group_id,
#                            0..$#{$self->volumes};
#                    }
#                }
#                
#                $self->Refresh;
#                $self->Update;
#            }
#            
#            # propagate event through callback
#            $self->on_select->($volume_idx)
#                if $self->on_select;
#            
#            if ($volume_idx != -1) {
#                if ($e->LeftDown && $self->enable_moving) {
#                    # Only accept the initial position, if it is inside the volume bounding box.
#                    my $volume_bbox = $self->volumes->[$volume_idx]->transformed_bounding_box;
#                    $volume_bbox->offset(1.);
#                    if ($volume_bbox->contains_point($pos3d)) {
#                        # The dragging operation is initiated.
#                        $self->_drag_volume_idx($volume_idx);
#                        $self->_drag_start_pos($pos3d);
#                        # Remember the shift to to the object center. The object center will later be used
#                        # to limit the object placement close to the bed.
#                        $self->_drag_volume_center_offset($pos3d->vector_to($volume_bbox->center));
#                    }
#                } elsif ($e->RightDown) {
#                    # if right clicking on volume, propagate event through callback
#                    $self->on_right_click->($e->GetPosition)
#                        if $self->on_right_click;
#                }
#            }
#        }
#    } elsif ($e->Dragging && $e->LeftIsDown && ! $self->_layer_height_edited && defined($self->_drag_volume_idx)) {
#        # Get new position at the same Z of the initial click point.
#        my $cur_pos = Slic3r::Linef3->new(
#                $self->mouse_to_3d($e->GetX, $e->GetY, 0),
#                $self->mouse_to_3d($e->GetX, $e->GetY, 1))
#            ->intersect_plane($self->_drag_start_pos->z);
# 
#        # Clip the new position, so the object center remains close to the bed.
#        {
#            $cur_pos->translate(@{$self->_drag_volume_center_offset});
#            my $cur_pos2 = Slic3r::Point->new(scale($cur_pos->x), scale($cur_pos->y));
#            if (! $self->bed_polygon->contains_point($cur_pos2)) {
#                my $ip = $self->bed_polygon->point_projection($cur_pos2);
#                $cur_pos->set_x(unscale($ip->x));
#                $cur_pos->set_y(unscale($ip->y));
#            }
#            $cur_pos->translate(@{$self->_drag_volume_center_offset->negative});
#        }
#        # Calculate the translation vector.
#        my $vector = $self->_drag_start_pos->vector_to($cur_pos);
#        # Get the volume being dragged.
#        my $volume = $self->volumes->[$self->_drag_volume_idx];
#        # Get all volumes belonging to the same group, if any.
#        my @volumes = ($volume->drag_group_id == -1) ?
#            ($volume) :
#            grep $_->drag_group_id == $volume->drag_group_id, @{$self->volumes};
#        # Apply new temporary volume origin and ignore Z.
#        $_->translate($vector->x, $vector->y, 0) for @volumes;
#        $self->_drag_start_pos($cur_pos);
#        $self->_dragged(1);
#        $self->Refresh;
#        $self->Update;
#    } elsif ($e->Dragging) {
#        if ($self->_layer_height_edited && $object_idx_selected != -1) {
#            $self->_variable_layer_thickness_action($e) if ($self->_layer_height_edited == 1);
#        } elsif ($e->LeftIsDown) {
#            # if dragging over blank area with left button, rotate
#            if (defined $self->_drag_start_pos) {
#                my $orig = $self->_drag_start_pos;
#                if (TURNTABLE_MODE) {
#                    # Turntable mode is enabled by default.
#                    $self->_sphi($self->_sphi + ($pos->x - $orig->x) * TRACKBALLSIZE);
#                    $self->_stheta($self->_stheta - ($pos->y - $orig->y) * TRACKBALLSIZE);        #-
#                    $self->_stheta(GIMBALL_LOCK_THETA_MAX) if $self->_stheta > GIMBALL_LOCK_THETA_MAX;
#                    $self->_stheta(0) if $self->_stheta < 0;
#                } else {
#                    my $size = $self->GetClientSize;
#                    my @quat = trackball(
#                        $orig->x / ($size->width / 2) - 1,
#                        1 - $orig->y / ($size->height / 2),       #/
#                        $pos->x / ($size->width / 2) - 1,
#                        1 - $pos->y / ($size->height / 2),        #/
#                    );
#                    $self->_quat(mulquats($self->_quat, \@quat));
#                }
#                $self->on_viewport_changed->() if $self->on_viewport_changed;
#                $self->Refresh;
#                $self->Update;
#            }
#            $self->_drag_start_pos($pos);
#        } elsif ($e->MiddleIsDown || $e->RightIsDown) {
#            # If dragging over blank area with right button, pan.
#            if (defined $self->_drag_start_xy) {
#                # get point in model space at Z = 0
#                my $cur_pos = $self->mouse_to_3d($e->GetX, $e->GetY, 0);
#                my $orig    = $self->mouse_to_3d($self->_drag_start_xy->x, $self->_drag_start_xy->y, 0);
#                $self->_camera_target->translate(@{$orig->vector_to($cur_pos)->negative});
#                $self->on_viewport_changed->() if $self->on_viewport_changed;
#                $self->Refresh;
#                $self->Update;
#            }
#            $self->_drag_start_xy($pos);
#        }
#    } elsif ($e->LeftUp || $e->MiddleUp || $e->RightUp) {
#        if ($self->_layer_height_edited) {
#            $self->_layer_height_edited(undef);
#            $self->{layer_height_edit_timer}->Stop;
#            $self->on_model_update->()
#                if ($object_idx_selected != -1 && $self->on_model_update);
#        } elsif ($self->on_move && defined($self->_drag_volume_idx) && $self->_dragged) {
#            # get all volumes belonging to the same group, if any
#            my @volume_idxs;
#            my $group_id = $self->volumes->[$self->_drag_volume_idx]->drag_group_id;
#            if ($group_id == -1) {
#                @volume_idxs = ($self->_drag_volume_idx);
#            } else {
#                @volume_idxs = grep $self->volumes->[$_]->drag_group_id == $group_id,
#                    0..$#{$self->volumes};
#            }
#            $self->on_move->(@volume_idxs);
#        }
#        $self->_drag_volume_idx(undef);
#        $self->_drag_start_pos(undef);
#        $self->_drag_start_xy(undef);
#        $self->_dragged(undef);
#    } elsif ($e->Moving) {
#        $self->_mouse_pos($pos);
#        # Only refresh if picking is enabled, in that case the objects may get highlighted if the mouse cursor
#        # hovers over.
#        if ($self->enable_picking) {
#            $self->Update;
#            $self->Refresh;
#        }
#    } else {
#        $e->Skip();
#    }
#}
#
#sub mouse_wheel_event {
#    my ($self, $e) = @_;
#
#    if ($e->MiddleIsDown) {
#        # Ignore the wheel events if the middle button is pressed.
#        return;
#    }
#    if ($self->layer_editing_enabled && $self->{print}) {
#        my $object_idx_selected = $self->_first_selected_object_id_for_variable_layer_height_editing;
#        if ($object_idx_selected != -1) {
#            # A volume is selected. Test, whether hovering over a layer thickness bar.
#            if ($self->_variable_layer_thickness_bar_rect_mouse_inside($e)) {
#                # Adjust the width of the selection.
#                $self->{layer_height_edit_band_width} = max(min($self->{layer_height_edit_band_width} * (1 + 0.1 * $e->GetWheelRotation() / $e->GetWheelDelta()), 10.), 1.5);
#                $self->Refresh;
#                return;
#            }
#        }
#    }
#
#    # Calculate the zoom delta and apply it to the current zoom factor
#    my $zoom = $e->GetWheelRotation() / $e->GetWheelDelta();
#    $zoom = max(min($zoom, 4), -4);
#    $zoom /= 10;
#    $zoom = $self->_zoom / (1-$zoom);
#    # Don't allow to zoom too far outside the scene.
#    my $zoom_min = $self->get_zoom_to_bounding_box_factor($self->max_bounding_box);
#    $zoom_min *= 0.4 if defined $zoom_min;
#    $zoom = $zoom_min if defined $zoom_min && $zoom < $zoom_min;
#    $self->_zoom($zoom);
#    
##    # In order to zoom around the mouse point we need to translate
##    # the camera target
##    my $size = Slic3r::Pointf->new($self->GetSizeWH);
##    my $pos = Slic3r::Pointf->new($e->GetX, $size->y - $e->GetY); #-
##    $self->_camera_target->translate(
##        # ($pos - $size/2) represents the vector from the viewport center
##        # to the mouse point. By multiplying it by $zoom we get the new,
##        # transformed, length of such vector.
##        # Since we want that point to stay fixed, we move our camera target
##        # in the opposite direction by the delta of the length of such vector
##        # ($zoom - 1). We then scale everything by 1/$self->_zoom since 
##        # $self->_camera_target is expressed in terms of model units.
##        -($pos->x - $size->x/2) * ($zoom) / $self->_zoom,
##        -($pos->y - $size->y/2) * ($zoom) / $self->_zoom,
##        0,
##    ) if 0;
#
#    $self->on_viewport_changed->() if $self->on_viewport_changed;
#    $self->Resize($self->GetSizeWH) if $self->IsShownOnScreen;
#    $self->Refresh;
#}
#
## Reset selection.
#sub reset_objects {
#    my ($self) = @_;
#    if ($self->GetContext) {
#        $self->SetCurrent($self->GetContext);
#        $self->volumes->release_geometry;
#    }
#    $self->volumes->erase;
#    $self->_dirty(1);
#}
#
## Setup camera to view all objects.
#sub set_viewport_from_scene {
#    my ($self, $scene) = @_;
#    
#    $self->_sphi($scene->_sphi);
#    $self->_stheta($scene->_stheta);
#    $self->_camera_target($scene->_camera_target);
#    $self->_zoom($scene->_zoom);
#    $self->_quat($scene->_quat);
#    $self->_dirty(1);
#}
#
## Set the camera to a default orientation,
## zoom to volumes.
#sub select_view {
#    my ($self, $direction) = @_;
#
#    my $dirvec;
#    if (ref($direction)) {
#        $dirvec = $direction;
#    } else {
#        if ($direction eq 'iso') {
#            $dirvec = VIEW_DEFAULT;
#        } elsif ($direction eq 'left') {
#            $dirvec = VIEW_LEFT;
#        } elsif ($direction eq 'right') {
#            $dirvec = VIEW_RIGHT;
#        } elsif ($direction eq 'top') {
#            $dirvec = VIEW_TOP;
#        } elsif ($direction eq 'bottom') {
#            $dirvec = VIEW_BOTTOM;
#        } elsif ($direction eq 'front') {
#            $dirvec = VIEW_FRONT;
#        } elsif ($direction eq 'rear') {
#            $dirvec = VIEW_REAR;
#        }
#    }
#    my $bb = $self->volumes_bounding_box;
#    if (! $bb->empty) {
#        $self->_sphi($dirvec->[0]);
#        $self->_stheta($dirvec->[1]);
#        # Avoid gimball lock.
#        $self->_stheta(GIMBALL_LOCK_THETA_MAX) if $self->_stheta > GIMBALL_LOCK_THETA_MAX;
#        $self->_stheta(0) if $self->_stheta < 0;
#        $self->on_viewport_changed->() if $self->on_viewport_changed;
#        $self->Refresh;
#    }
#}
#
#sub get_zoom_to_bounding_box_factor {
#    my ($self, $bb) = @_;    
#    my $max_bb_size = max(@{ $bb->size });
#    return undef if ($max_bb_size == 0);
#        
#    # project the bbox vertices on a plane perpendicular to the camera forward axis
#    # then calculates the vertices coordinate on this plane along the camera xy axes
#    
#    # we need the view matrix, we let opengl calculate it (same as done in render sub)
#    glMatrixMode(GL_MODELVIEW);
#    glLoadIdentity();
#
#    if (!TURNTABLE_MODE) {
#        # Shift the perspective camera.
#        my $camera_pos = Slic3r::Pointf3->new(0,0,-$self->_camera_distance);
#        glTranslatef(@$camera_pos);
#    }
#    
#    if (TURNTABLE_MODE) {
#        # Turntable mode is enabled by default.
#        glRotatef(-$self->_stheta, 1, 0, 0); # pitch
#        glRotatef($self->_sphi, 0, 0, 1);    # yaw
#    } else {
#        # Shift the perspective camera.
#        my $camera_pos = Slic3r::Pointf3->new(0,0,-$self->_camera_distance);
#        glTranslatef(@$camera_pos);
#        my @rotmat = quat_to_rotmatrix($self->quat);
#        glMultMatrixd_p(@rotmat[0..15]);
#    }    
#    glTranslatef(@{ $self->_camera_target->negative });
#    
#    # get the view matrix back from opengl
#    my @matrix = glGetFloatv_p(GL_MODELVIEW_MATRIX);
#
#    # camera axes
#    my $right = Slic3r::Pointf3->new($matrix[0], $matrix[4], $matrix[8]);
#    my $up = Slic3r::Pointf3->new($matrix[1], $matrix[5], $matrix[9]);
#    my $forward = Slic3r::Pointf3->new($matrix[2], $matrix[6], $matrix[10]);
#    
#    my $bb_min = $bb->min_point();
#    my $bb_max = $bb->max_point();
#    my $bb_center = $bb->center();
#    
#    # bbox vertices in world space
#    my @vertices = ();    
#    push(@vertices, $bb_min);
#    push(@vertices, Slic3r::Pointf3->new($bb_max->x(), $bb_min->y(), $bb_min->z()));
#    push(@vertices, Slic3r::Pointf3->new($bb_max->x(), $bb_max->y(), $bb_min->z()));
#    push(@vertices, Slic3r::Pointf3->new($bb_min->x(), $bb_max->y(), $bb_min->z()));
#    push(@vertices, Slic3r::Pointf3->new($bb_min->x(), $bb_min->y(), $bb_max->z()));
#    push(@vertices, Slic3r::Pointf3->new($bb_max->x(), $bb_min->y(), $bb_max->z()));
#    push(@vertices, $bb_max);
#    push(@vertices, Slic3r::Pointf3->new($bb_min->x(), $bb_max->y(), $bb_max->z()));
#    
#    my $max_x = 0.0;
#    my $max_y = 0.0;
#
#    # margin factor to give some empty space around the bbox
#    my $margin_factor = 1.25;
#    
#    foreach my $v (@vertices) {
#        # project vertex on the plane perpendicular to camera forward axis
#        my $pos = Slic3r::Pointf3->new($v->x() - $bb_center->x(), $v->y() - $bb_center->y(), $v->z() - $bb_center->z());
#        my $proj_on_normal = $pos->x() * $forward->x() + $pos->y() * $forward->y() + $pos->z() * $forward->z();
#        my $proj_on_plane = Slic3r::Pointf3->new($pos->x() - $proj_on_normal * $forward->x(), $pos->y() - $proj_on_normal * $forward->y(), $pos->z() - $proj_on_normal * $forward->z());
#        
#        # calculates vertex coordinate along camera xy axes
#        my $x_on_plane = $proj_on_plane->x() * $right->x() + $proj_on_plane->y() * $right->y() + $proj_on_plane->z() * $right->z();
#        my $y_on_plane = $proj_on_plane->x() * $up->x() + $proj_on_plane->y() * $up->y() + $proj_on_plane->z() * $up->z();
#    
#        $max_x = max($max_x, $margin_factor * 2 * abs($x_on_plane));
#        $max_y = max($max_y, $margin_factor * 2 * abs($y_on_plane));
#    }
#    
#    return undef if (($max_x == 0) || ($max_y == 0));
#    
#    my ($cw, $ch) = $self->GetSizeWH;
#    my $min_ratio = min($cw / $max_x, $ch / $max_y);
#
#    return $min_ratio;
#}
#
#sub zoom_to_bounding_box {
#    my ($self, $bb) = @_;
#    # Calculate the zoom factor needed to adjust viewport to bounding box.
#    my $zoom = $self->get_zoom_to_bounding_box_factor($bb);
#    if (defined $zoom) {
#        $self->_zoom($zoom);
#        # center view around bounding box center
#        $self->_camera_target($bb->center);
#        $self->on_viewport_changed->() if $self->on_viewport_changed;
#        $self->Resize($self->GetSizeWH) if $self->IsShownOnScreen;
#        $self->Refresh;
#    }
#}
#
#sub zoom_to_bed {
#    my ($self) = @_;
#    
#    if ($self->bed_shape) {
#        $self->zoom_to_bounding_box($self->bed_bounding_box);
#    }
#}
#
#sub zoom_to_volume {
#    my ($self, $volume_idx) = @_;
#    
#    my $volume = $self->volumes->[$volume_idx];
#    my $bb = $volume->transformed_bounding_box;
#    $self->zoom_to_bounding_box($bb);
#}
#
#sub zoom_to_volumes {
#    my ($self) = @_;
#
#    $self->_apply_zoom_to_volumes_filter(1);
#    $self->zoom_to_bounding_box($self->volumes_bounding_box);
#    $self->_apply_zoom_to_volumes_filter(0);
#}
#
#sub volumes_bounding_box {
#    my ($self) = @_;
#    
#    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
#    foreach my $v (@{$self->volumes}) {
#        $bb->merge($v->transformed_bounding_box) if (! $self->_apply_zoom_to_volumes_filter || $v->zoom_to_volumes);
#    }
#    return $bb;
#}
#
#sub bed_bounding_box {
#    my ($self) = @_;
#        
#    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
#    if ($self->bed_shape) {
#        $bb->merge_point(Slic3r::Pointf3->new(@$_, 0)) for @{$self->bed_shape};
#    }
#    return $bb;
#}
#
#sub max_bounding_box {
#    my ($self) = @_;
#        
#    my $bb = $self->bed_bounding_box;
#    $bb->merge($self->volumes_bounding_box);
#    return $bb;
#}
#
## Used by ObjectCutDialog and ObjectPartsPanel to generate a rectangular ground plane
## to support the scene objects.
#sub set_auto_bed_shape {
#    my ($self, $bed_shape) = @_;
#    
#    # draw a default square bed around object center
#    my $max_size = max(@{ $self->volumes_bounding_box->size });
#    my $center = $self->volumes_bounding_box->center;
#    $self->set_bed_shape([
#        [ $center->x - $max_size, $center->y - $max_size ],  #--
#        [ $center->x + $max_size, $center->y - $max_size ],  #--
#        [ $center->x + $max_size, $center->y + $max_size ],  #++
#        [ $center->x - $max_size, $center->y + $max_size ],  #++
#    ]);
#    # Set the origin for painting of the coordinate system axes.
#    $self->origin(Slic3r::Pointf->new(@$center[X,Y]));
#}
#
## Set the bed shape to a single closed 2D polygon (array of two element arrays),
## triangulate the bed and store the triangles into $self->bed_triangles,
## fills the $self->bed_grid_lines and sets $self->origin.
## Sets $self->bed_polygon to limit the object placement.
#sub set_bed_shape {
#    my ($self, $bed_shape) = @_;
#    
#    $self->bed_shape($bed_shape);
#    
#    # triangulate bed
#    my $expolygon = Slic3r::ExPolygon->new([ map [map scale($_), @$_], @$bed_shape ]);
#    my $bed_bb = $expolygon->bounding_box;
#    
#    {
#        my @points = ();
#        foreach my $triangle (@{ $expolygon->triangulate }) {
#            push @points, map {+ unscale($_->x), unscale($_->y), GROUND_Z } @$triangle;
#        }
#        $self->bed_triangles(OpenGL::Array->new_list(GL_FLOAT, @points));
#    }
#    
#    {
#        my @polylines = ();
#        for (my $x = $bed_bb->x_min; $x <= $bed_bb->x_max; $x += scale 10) {
#            push @polylines, Slic3r::Polyline->new([$x,$bed_bb->y_min], [$x,$bed_bb->y_max]);
#        }
#        for (my $y = $bed_bb->y_min; $y <= $bed_bb->y_max; $y += scale 10) {
#            push @polylines, Slic3r::Polyline->new([$bed_bb->x_min,$y], [$bed_bb->x_max,$y]);
#        }
#        # clip with a slightly grown expolygon because our lines lay on the contours and
#        # may get erroneously clipped
#        my @lines = map Slic3r::Line->new(@$_[0,-1]),
#            @{intersection_pl(\@polylines, [ @{$expolygon->offset(+scaled_epsilon)} ])};
#        
#        # append bed contours
#        push @lines, map @{$_->lines}, @$expolygon;
#        
#        my @points = ();
#        foreach my $line (@lines) {
#            push @points, map {+ unscale($_->x), unscale($_->y), GROUND_Z } @$line;  #))
#        }
#        $self->bed_grid_lines(OpenGL::Array->new_list(GL_FLOAT, @points));
#    }
#    
#    # Set the origin for painting of the coordinate system axes.
#    $self->origin(Slic3r::Pointf->new(0,0));
#
#    $self->bed_polygon(offset_ex([$expolygon->contour], $bed_bb->radius * 1.7, JT_ROUND, scale(0.5))->[0]->contour->clone);
#}
#
#sub deselect_volumes {
#    my ($self) = @_;
#    $_->set_selected(0) for @{$self->volumes};
#}
#
#sub select_volume {
#    my ($self, $volume_idx) = @_;
#
#    return if ($volume_idx >= scalar(@{$self->volumes}));
#
#    $self->volumes->[$volume_idx]->set_selected(1)
#        if $volume_idx != -1;
#}
#
#sub SetCuttingPlane {
#    my ($self, $z, $expolygons) = @_;
#    
#    $self->cutting_plane_z($z);
#    
#    # grow slices in order to display them better
#    $expolygons = offset_ex([ map @$_, @$expolygons ], scale 0.1);
#    
#    my @verts = ();
#    foreach my $line (map @{$_->lines}, map @$_, @$expolygons) {
#        push @verts, (
#            unscale($line->a->x), unscale($line->a->y), $z,  #))
#            unscale($line->b->x), unscale($line->b->y), $z,  #))
#        );
#    }
#    $self->cut_lines_vertices(OpenGL::Array->new_list(GL_FLOAT, @verts));
#}
#
## Given an axis and angle, compute quaternion.
#sub axis_to_quat {
#    my ($ax, $phi) = @_;
#    
#    my $lena = sqrt(reduce { $a + $b } (map { $_ * $_ } @$ax));
#    my @q = map { $_ * (1 / $lena) } @$ax;
#    @q = map { $_ * sin($phi / 2.0) } @q;
#    $q[$#q + 1] = cos($phi / 2.0);
#    return @q;
#}
#
## Project a point on the virtual trackball. 
## If it is inside the sphere, map it to the sphere, if it outside map it
## to a hyperbola.
#sub project_to_sphere {
#    my ($r, $x, $y) = @_;
#    
#    my $d = sqrt($x * $x + $y * $y);
#    if ($d < $r * 0.70710678118654752440) {     # Inside sphere
#        return sqrt($r * $r - $d * $d);
#    } else {                                    # On hyperbola
#        my $t = $r / 1.41421356237309504880;
#        return $t * $t / $d;
#    }
#}
#
#sub cross {
#    my ($v1, $v2) = @_;
#  
#    return (@$v1[1] * @$v2[2] - @$v1[2] * @$v2[1],
#            @$v1[2] * @$v2[0] - @$v1[0] * @$v2[2],
#            @$v1[0] * @$v2[1] - @$v1[1] * @$v2[0]);
#}
#
## Simulate a track-ball. Project the points onto the virtual trackball, 
## then figure out the axis of rotation, which is the cross product of 
## P1 P2 and O P1 (O is the center of the ball, 0,0,0) Note: This is a 
## deformed trackball-- is a trackball in the center, but is deformed 
## into a hyperbolic sheet of rotation away from the center. 
## It is assumed that the arguments to this routine are in the range 
## (-1.0 ... 1.0).
#sub trackball {
#    my ($p1x, $p1y, $p2x, $p2y) = @_;
#    
#    if ($p1x == $p2x && $p1y == $p2y) {
#        # zero rotation
#        return (0.0, 0.0, 0.0, 1.0);
#    }
#    
#    # First, figure out z-coordinates for projection of P1 and P2 to
#    # deformed sphere
#    my @p1 = ($p1x, $p1y, project_to_sphere(TRACKBALLSIZE, $p1x, $p1y));
#    my @p2 = ($p2x, $p2y, project_to_sphere(TRACKBALLSIZE, $p2x, $p2y));
#    
#    # axis of rotation (cross product of P1 and P2)
#    my @a = cross(\@p2, \@p1);
#
#    # Figure out how much to rotate around that axis.
#    my @d = map { $_ * $_ } (map { $p1[$_] - $p2[$_] } 0 .. $#p1);
#    my $t = sqrt(reduce { $a + $b } @d) / (2.0 * TRACKBALLSIZE);
#    
#    # Avoid problems with out-of-control values...
#    $t = 1.0 if ($t > 1.0);
#    $t = -1.0 if ($t < -1.0);
#    my $phi = 2.0 * asin($t);
#
#    return axis_to_quat(\@a, $phi);
#}
#
## Build a rotation matrix, given a quaternion rotation.
#sub quat_to_rotmatrix {
#    my ($q) = @_;
#  
#    my @m = ();
#  
#    $m[0] = 1.0 - 2.0 * (@$q[1] * @$q[1] + @$q[2] * @$q[2]);
#    $m[1] = 2.0 * (@$q[0] * @$q[1] - @$q[2] * @$q[3]);
#    $m[2] = 2.0 * (@$q[2] * @$q[0] + @$q[1] * @$q[3]);
#    $m[3] = 0.0;
#
#    $m[4] = 2.0 * (@$q[0] * @$q[1] + @$q[2] * @$q[3]);
#    $m[5] = 1.0 - 2.0 * (@$q[2] * @$q[2] + @$q[0] * @$q[0]);
#    $m[6] = 2.0 * (@$q[1] * @$q[2] - @$q[0] * @$q[3]);
#    $m[7] = 0.0;
#
#    $m[8] = 2.0 * (@$q[2] * @$q[0] - @$q[1] * @$q[3]);
#    $m[9] = 2.0 * (@$q[1] * @$q[2] + @$q[0] * @$q[3]);
#    $m[10] = 1.0 - 2.0 * (@$q[1] * @$q[1] + @$q[0] * @$q[0]);
#    $m[11] = 0.0;
#
#    $m[12] = 0.0;
#    $m[13] = 0.0;
#    $m[14] = 0.0;
#    $m[15] = 1.0;
#  
#    return @m;
#}
#
#sub mulquats {
#    my ($q1, $rq) = @_;
#  
#    return (@$q1[3] * @$rq[0] + @$q1[0] * @$rq[3] + @$q1[1] * @$rq[2] - @$q1[2] * @$rq[1],
#            @$q1[3] * @$rq[1] + @$q1[1] * @$rq[3] + @$q1[2] * @$rq[0] - @$q1[0] * @$rq[2],
#            @$q1[3] * @$rq[2] + @$q1[2] * @$rq[3] + @$q1[0] * @$rq[1] - @$q1[1] * @$rq[0],
#            @$q1[3] * @$rq[3] - @$q1[0] * @$rq[0] - @$q1[1] * @$rq[1] - @$q1[2] * @$rq[2])
#}
#
## Convert the screen space coordinate to an object space coordinate.
## If the Z screen space coordinate is not provided, a depth buffer value is substituted.
#sub mouse_to_3d {
#    my ($self, $x, $y, $z) = @_;
#
#    return unless $self->GetContext;
#    $self->SetCurrent($self->GetContext);
#
#    my @viewport    = glGetIntegerv_p(GL_VIEWPORT);             # 4 items
#    my @mview       = glGetDoublev_p(GL_MODELVIEW_MATRIX);      # 16 items
#    my @proj        = glGetDoublev_p(GL_PROJECTION_MATRIX);     # 16 items
#    
#    $y = $viewport[3] - $y;
#    $z //= glReadPixels_p($x, $y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT);
#    my @projected = gluUnProject_p($x, $y, $z, @mview, @proj, @viewport);
#    return Slic3r::Pointf3->new(@projected);
#}
#
#sub GetContext {
#    my ($self) = @_;
#    return $self->{context} ||= Wx::GLContext->new($self);
#}
# 
#sub SetCurrent {
#    my ($self, $context) = @_;
#    return $self->SUPER::SetCurrent($context);
#}
#
#sub UseVBOs {
#    my ($self) = @_;
#        
#    if (! defined ($self->{use_VBOs})) {
#        my $use_legacy = wxTheApp->{app_config}->get('use_legacy_opengl');
#        if ($use_legacy eq '1') {
#            # Disable OpenGL 2.0 rendering.
#            $self->{use_VBOs} = 0;
#            # Don't enable the layer editing tool.
#            $self->{layer_editing_enabled} = 0;
#            # 2 means failed
#            $self->{layer_editing_initialized} = 2;
#            return 0;
#        }
#        # This is a special path for wxWidgets on GTK, where an OpenGL context is initialized
#        # first when an OpenGL widget is shown for the first time. How ugly.
#        return 0 if (! $self->init && $^O eq 'linux');
#        # Don't use VBOs if anything fails.
#        $self->{use_VBOs} = 0;
#        if ($self->GetContext) {
#            $self->SetCurrent($self->GetContext);
#            Slic3r::GUI::_3DScene::_glew_init;
#            my @gl_version = split(/\./, glGetString(GL_VERSION));
#            $self->{use_VBOs} = int($gl_version[0]) >= 2;
#            # print "UseVBOs $self OpenGL major: $gl_version[0], minor: $gl_version[1]. Use VBOs: ", $self->{use_VBOs}, "\n";
#        }
#    }
#    return $self->{use_VBOs};
#}
#
#sub Resize {
#    my ($self, $x, $y) = @_;
#
#    return unless $self->GetContext;
#    $self->_dirty(0);
#    
#    $self->SetCurrent($self->GetContext);
#    glViewport(0, 0, $x, $y);
# 
#    $x /= $self->_zoom;
#    $y /= $self->_zoom;
#    
#    glMatrixMode(GL_PROJECTION);
#    glLoadIdentity();
#    if ($self->_camera_type eq 'ortho') {
#        #FIXME setting the size of the box 10x larger than necessary
#        # is only a workaround for an incorrectly set camera.
#        # This workaround harms Z-buffer accuracy!
##        my $depth = 1.05 * $self->max_bounding_box->radius();
#       my $depth = 5.0 * max(@{ $self->max_bounding_box->size });
#        glOrtho(
#            -$x/2, $x/2, -$y/2, $y/2,
#            -$depth, $depth,
#        );
#    } else {
#        die "Invalid camera type: ", $self->_camera_type, "\n" if ($self->_camera_type ne 'perspective');
#        my $bbox_r = $self->max_bounding_box->radius();
#        my $fov = PI * 45. / 180.;
#        my $fov_tan = tan(0.5 * $fov);
#        my $cam_distance = 0.5 * $bbox_r / $fov_tan;
#        $self->_camera_distance($cam_distance);
#        my $nr = $cam_distance - $bbox_r * 1.1;
#        my $fr = $cam_distance + $bbox_r * 1.1;
#        $nr = 1 if ($nr < 1);
#        $fr = $nr + 1 if ($fr < $nr + 1);
#        my $h2 = $fov_tan * $nr;
#        my $w2 = $h2 * $x / $y;
#        glFrustum(-$w2, $w2, -$h2, $h2, $nr, $fr);        
#    }
#    glMatrixMode(GL_MODELVIEW);
#}
#
#sub InitGL {
#    my $self = shift;
# 
#    return if $self->init;
#    return unless $self->GetContext;
#    $self->init(1);
#    
##    # This is a special path for wxWidgets on GTK, where an OpenGL context is initialized
##    # first when an OpenGL widget is shown for the first time. How ugly.
##    # In that case the volumes are wainting to be moved to Vertex Buffer Objects
##    # after the OpenGL context is being initialized.
##    $self->volumes->finalize_geometry(1) 
##        if ($^O eq 'linux' && $self->UseVBOs);
# 
#    $self->zoom_to_bed;
#    
#    glClearColor(0, 0, 0, 1);
#    glColor3f(1, 0, 0);
#    glEnable(GL_DEPTH_TEST);
#    glClearDepth(1.0);
#    glDepthFunc(GL_LEQUAL);
#    glEnable(GL_CULL_FACE);
#    glEnable(GL_BLEND);
#    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#    
#    # Set antialiasing/multisampling
#    glDisable(GL_LINE_SMOOTH);
#    glDisable(GL_POLYGON_SMOOTH);
#
#    # See "GL_MULTISAMPLE and GL_ARRAY_BUFFER_ARB messages on failed launch"
#    # https://github.com/alexrj/Slic3r/issues/4085
#    eval {
#        # Disable the multi sampling by default, so the picking by color will work correctly.
#        glDisable(GL_MULTISAMPLE);
#    };
#    # Disable multi sampling if the eval failed.
#    $self->{can_multisample} = 0 if $@;
#    
#    # ambient lighting
#    glLightModelfv_p(GL_LIGHT_MODEL_AMBIENT, 0.3, 0.3, 0.3, 1);
#    
#    glEnable(GL_LIGHTING);
#    glEnable(GL_LIGHT0);
#    glEnable(GL_LIGHT1);
#    
#    # light from camera
#    glLightfv_p(GL_LIGHT1, GL_POSITION, 1, 0, 1, 0);
#    glLightfv_p(GL_LIGHT1, GL_SPECULAR, 0.3, 0.3, 0.3, 1);
#    glLightfv_p(GL_LIGHT1, GL_DIFFUSE,  0.2, 0.2, 0.2, 1);
#    
#    # Enables Smooth Color Shading; try GL_FLAT for (lack of) fun.
#    glShadeModel(GL_SMOOTH);
#    
##    glMaterialfv_p(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, 0.5, 0.3, 0.3, 1);
##    glMaterialfv_p(GL_FRONT_AND_BACK, GL_SPECULAR, 1, 1, 1, 1);
##    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 50);
##    glMaterialfv_p(GL_FRONT_AND_BACK, GL_EMISSION, 0.1, 0, 0, 0.9);
#    
#    # A handy trick -- have surface material mirror the color.
#    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
#    glEnable(GL_COLOR_MATERIAL);
#    glEnable(GL_MULTISAMPLE) if ($self->{can_multisample});
#
#    if ($self->UseVBOs) {
#        my $shader = new Slic3r::GUI::_3DScene::GLShader;
##        if (! $shader->load($self->_fragment_shader_Phong, $self->_vertex_shader_Phong)) {
#            print "Compilaton of path shader failed: \n" . $shader->last_error . "\n";
#            $shader = undef;
#        } else {
#            $self->{plain_shader} = $shader;
#        }
#    }
#}
#
#sub DestroyGL {
#    my $self = shift;
#    if ($self->GetContext) {
#        $self->SetCurrent($self->GetContext);
#        if ($self->{plain_shader}) {
#            $self->{plain_shader}->release;
#            delete $self->{plain_shader};
#        }
#        if ($self->{layer_height_edit_shader}) {
#            $self->{layer_height_edit_shader}->release;
#            delete $self->{layer_height_edit_shader};
#        }
#        $self->volumes->release_geometry;
#    }
#}
#
#sub Render {
#    my ($self, $dc) = @_;
#    
#    # prevent calling SetCurrent() when window is not shown yet
#    return unless $self->IsShownOnScreen;
#    return unless my $context = $self->GetContext;
#    $self->SetCurrent($context);
#    $self->InitGL;
#        
#    glClearColor(1, 1, 1, 1);
#    glClearDepth(1);
#    glDepthFunc(GL_LESS);
#    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#    
#    glMatrixMode(GL_MODELVIEW);
#    glLoadIdentity();
#
#    if (!TURNTABLE_MODE) {
#        # Shift the perspective camera.
#        my $camera_pos = Slic3r::Pointf3->new(0,0,-$self->_camera_distance);
#        glTranslatef(@$camera_pos);
#    }
#    
#    if (TURNTABLE_MODE) {
#        # Turntable mode is enabled by default.
#        glRotatef(-$self->_stheta, 1, 0, 0); # pitch
#        glRotatef($self->_sphi, 0, 0, 1);    # yaw
#    } else {
#        my @rotmat = quat_to_rotmatrix($self->quat);
#        glMultMatrixd_p(@rotmat[0..15]);
#    }
#    
#    glTranslatef(@{ $self->_camera_target->negative });
#    
#    # light from above
#    glLightfv_p(GL_LIGHT0, GL_POSITION, -0.5, -0.5, 1, 0);
#    glLightfv_p(GL_LIGHT0, GL_SPECULAR, 0.2, 0.2, 0.2, 1);
#    glLightfv_p(GL_LIGHT0, GL_DIFFUSE,  0.5, 0.5, 0.5, 1);
#
#    # Head light
#    glLightfv_p(GL_LIGHT1, GL_POSITION, 1, 0, 1, 0);
#    
#    if ($self->enable_picking && !$self->_mouse_dragging) {
#        if (my $pos = $self->_mouse_pos) {
#            # Render the object for picking.
#            # FIXME This cannot possibly work in a multi-sampled context as the color gets mangled by the anti-aliasing.
#            # Better to use software ray-casting on a bounding-box hierarchy.
#            glPushAttrib(GL_ENABLE_BIT);
#            glDisable(GL_MULTISAMPLE) if ($self->{can_multisample});
#            glDisable(GL_LIGHTING);
#            glDisable(GL_BLEND);
#            $self->draw_volumes(1);
#            glPopAttrib();
#            glFlush();
#            my $col = [ glReadPixels_p($pos->x, $self->GetSize->GetHeight - $pos->y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE) ];
#            my $volume_idx = $col->[0] + $col->[1]*256 + $col->[2]*256*256;
#            $self->_hover_volume_idx(undef);
#            $_->set_hover(0) for @{$self->volumes};
#            if ($volume_idx <= $#{$self->volumes}) {
#                $self->_hover_volume_idx($volume_idx);
#                
#                $self->volumes->[$volume_idx]->set_hover(1);
#                my $group_id = $self->volumes->[$volume_idx]->select_group_id;
#                if ($group_id != -1) {
#                    $_->set_hover(1) for grep { $_->select_group_id == $group_id } @{$self->volumes};
#                }
#                
#                $self->on_hover->($volume_idx) if $self->on_hover;
#            }
#            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#        }
#    }
#    
#    # draw fixed background
#    if ($self->background) {
#        glDisable(GL_LIGHTING);
#        glPushMatrix();
#        glLoadIdentity();
#        
#        glMatrixMode(GL_PROJECTION);
#        glPushMatrix();
#        glLoadIdentity();
#        
#        # Draws a bluish bottom to top gradient over the complete screen.
#        glDisable(GL_DEPTH_TEST);
#        glBegin(GL_QUADS);
#        glColor3f(0.0,0.0,0.0);
#        glVertex3f(-1.0,-1.0, 1.0);
#        glVertex3f( 1.0,-1.0, 1.0);
#        glColor3f(10/255,98/255,144/255);
#        glVertex3f( 1.0, 1.0, 1.0);
#        glVertex3f(-1.0, 1.0, 1.0);
#        glEnd();
#        glPopMatrix();
#        glEnable(GL_DEPTH_TEST);
#        
#        glMatrixMode(GL_MODELVIEW);
#        glPopMatrix();
#        glEnable(GL_LIGHTING);
#    }
#    
#    # draw ground and axes
#    glDisable(GL_LIGHTING);
#    
#    # draw ground
#    my $ground_z = GROUND_Z;
#    
#    if ($self->bed_triangles) {
#        glDisable(GL_DEPTH_TEST);
#        
#        glEnable(GL_BLEND);
#        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#        
#        glEnableClientState(GL_VERTEX_ARRAY);
#        glColor4f(0.8, 0.6, 0.5, 0.4);
#        glNormal3d(0,0,1);
#        glVertexPointer_c(3, GL_FLOAT, 0, $self->bed_triangles->ptr());
#        glDrawArrays(GL_TRIANGLES, 0, $self->bed_triangles->elements / 3);
#        glDisableClientState(GL_VERTEX_ARRAY);
#        
#        # we need depth test for grid, otherwise it would disappear when looking
#        # the object from below
#        glEnable(GL_DEPTH_TEST);
#    
#        # draw grid
#        glLineWidth(3);
#        glColor4f(0.2, 0.2, 0.2, 0.4);
#        glEnableClientState(GL_VERTEX_ARRAY);
#        glVertexPointer_c(3, GL_FLOAT, 0, $self->bed_grid_lines->ptr());
#        glDrawArrays(GL_LINES, 0, $self->bed_grid_lines->elements / 3);
#        glDisableClientState(GL_VERTEX_ARRAY);
#        
#        glDisable(GL_BLEND);
#    }
#    
#    my $volumes_bb = $self->volumes_bounding_box;
#    
#    {
#       # draw axes
#        # disable depth testing so that axes are not covered by ground
#        glDisable(GL_DEPTH_TEST);
#        my $origin = $self->origin;
#        my $axis_len = $self->use_plain_shader ? 0.3 * max(@{ $self->bed_bounding_box->size }) : 2 * max(@{ $volumes_bb->size });
#        glLineWidth(2);
#        glBegin(GL_LINES);
#        # draw line for x axis
#        glColor3f(1, 0, 0);
#        glVertex3f(@$origin, $ground_z);
#        glVertex3f($origin->x + $axis_len, $origin->y, $ground_z);  #,,
#        # draw line for y axis
#        glColor3f(0, 1, 0);
#        glVertex3f(@$origin, $ground_z);
#        glVertex3f($origin->x, $origin->y + $axis_len, $ground_z);  #++
#        glEnd();
#        # draw line for Z axis
#        # (re-enable depth test so that axis is correctly shown when objects are behind it)
#        glEnable(GL_DEPTH_TEST);
#        glBegin(GL_LINES);
#        glColor3f(0, 0, 1);
#        glVertex3f(@$origin, $ground_z);
#        glVertex3f(@$origin, $ground_z+$axis_len);
#        glEnd();
#    }
#    
#    glEnable(GL_LIGHTING);
#    
#    # draw objects
#    if (! $self->use_plain_shader) {
#        $self->draw_volumes;
#    } elsif ($self->UseVBOs) {
#        if ($self->enable_picking) {
#            $self->mark_volumes_for_layer_height;
#            $self->volumes->set_print_box($self->bed_bounding_box->x_min, $self->bed_bounding_box->y_min, 0.0, $self->bed_bounding_box->x_max, $self->bed_bounding_box->y_max, $self->{config}->get('max_print_height'));
#            $self->volumes->check_outside_state($self->{config});
#            # do not cull backfaces to show broken geometry, if any
#            glDisable(GL_CULL_FACE);
#        }
#        $self->{plain_shader}->enable if $self->{plain_shader};
#        $self->volumes->render_VBOs;
#        $self->{plain_shader}->disable;
#        glEnable(GL_CULL_FACE) if ($self->enable_picking);
#    } else {
#        # do not cull backfaces to show broken geometry, if any
#        glDisable(GL_CULL_FACE) if ($self->enable_picking);
#        $self->volumes->render_legacy;
#        glEnable(GL_CULL_FACE) if ($self->enable_picking);
#    }
#
#    if (defined $self->cutting_plane_z) {
#        # draw cutting plane
#        my $plane_z = $self->cutting_plane_z;
#        my $bb = $volumes_bb;
#        glDisable(GL_CULL_FACE);
#        glDisable(GL_LIGHTING);
#        glEnable(GL_BLEND);
#        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#        glBegin(GL_QUADS);
#        glColor4f(0.8, 0.8, 0.8, 0.5);
#        glVertex3f($bb->x_min-20, $bb->y_min-20, $plane_z);
#        glVertex3f($bb->x_max+20, $bb->y_min-20, $plane_z);
#        glVertex3f($bb->x_max+20, $bb->y_max+20, $plane_z);
#        glVertex3f($bb->x_min-20, $bb->y_max+20, $plane_z);
#        glEnd();
#        glEnable(GL_CULL_FACE);
#        glDisable(GL_BLEND);
#        
#        # draw cutting contours
#        glEnableClientState(GL_VERTEX_ARRAY);
#        glLineWidth(2);
#        glColor3f(0, 0, 0);
#        glVertexPointer_c(3, GL_FLOAT, 0, $self->cut_lines_vertices->ptr());
#        glDrawArrays(GL_LINES, 0, $self->cut_lines_vertices->elements / 3);
#        glVertexPointer_c(3, GL_FLOAT, 0, 0);
#        glDisableClientState(GL_VERTEX_ARRAY);
#    }
#
#    # draw warning message
#    $self->draw_warning;
#    
#    # draw gcode preview legend
#    $self->draw_legend;
#    
#    $self->draw_active_object_annotations;
#    
#    $self->SwapBuffers();
#}
#
#sub draw_volumes {
#    # $fakecolor is a boolean indicating, that the objects shall be rendered in a color coding the object index for picking.
#    my ($self, $fakecolor) = @_;
#    
#    # do not cull backfaces to show broken geometry, if any
#    glDisable(GL_CULL_FACE);
#    
#    glEnable(GL_BLEND);
#    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#    
#    glEnableClientState(GL_VERTEX_ARRAY);
#    glEnableClientState(GL_NORMAL_ARRAY);
#    
#    foreach my $volume_idx (0..$#{$self->volumes}) {
#        my $volume = $self->volumes->[$volume_idx];
#
#        if ($fakecolor) {
#            # Object picking mode. Render the object with a color encoding the object index.
#            my $r = ($volume_idx & 0x000000FF) >>  0;
#            my $g = ($volume_idx & 0x0000FF00) >>  8;
#            my $b = ($volume_idx & 0x00FF0000) >> 16;
#            glColor4f($r/255.0, $g/255.0, $b/255.0, 1);
#        } elsif ($volume->selected) {
#            glColor4f(@{ &SELECTED_COLOR });
#        } elsif ($volume->hover) {
#            glColor4f(@{ &HOVER_COLOR });
#        } else {
#            glColor4f(@{ $volume->color });
#        }
#
#        $volume->render;
#    }
#    glDisableClientState(GL_NORMAL_ARRAY);
#    glDisableClientState(GL_VERTEX_ARRAY);
#    
#    glDisable(GL_BLEND);
#    glEnable(GL_CULL_FACE);    
#}
#
#sub mark_volumes_for_layer_height {
#    my ($self) = @_;
#    
#    foreach my $volume_idx (0..$#{$self->volumes}) {
#        my $volume = $self->volumes->[$volume_idx];
#        my $object_id = int($volume->select_group_id / 1000000);
#        if ($self->layer_editing_enabled && $volume->selected && $self->{layer_height_edit_shader} && 
#            $volume->has_layer_height_texture && $object_id < $self->{print}->object_count) {
#            $volume->set_layer_height_texture_data($self->{layer_preview_z_texture_id}, $self->{layer_height_edit_shader}->shader_program_id,
#                $self->{print}->get_object($object_id), $self->_variable_layer_thickness_bar_mouse_cursor_z_relative, $self->{layer_height_edit_band_width});
#        } else {
#            $volume->reset_layer_height_texture_data();
#        }
#    }
#}
#
#sub _load_image_set_texture {
#    my ($self, $file_name) = @_;
#    # Load a PNG with an alpha channel.
#    my $img = Wx::Image->new;
#    $img->LoadFile(Slic3r::var($file_name), wxBITMAP_TYPE_PNG);
#    # Get RGB & alpha raw data from wxImage, interleave them into a Perl array.
#    my @rgb = unpack 'C*', $img->GetData();
#    my @alpha = $img->HasAlpha ? unpack 'C*', $img->GetAlpha() : (255) x (int(@rgb) / 3);
#    my $n_pixels = int(@alpha);
#    my @data = (0)x($n_pixels * 4);
#    for (my $i = 0; $i < $n_pixels; $i += 1) {
#        $data[$i*4  ] = $rgb[$i*3];
#        $data[$i*4+1] = $rgb[$i*3+1];
#        $data[$i*4+2] = $rgb[$i*3+2];
#        $data[$i*4+3] = $alpha[$i];
#    }
#    # Initialize a raw bitmap data.
#    my $params = {
#        loaded => 1,
#        valid  => $n_pixels > 0,
#        width  => $img->GetWidth, 
#        height => $img->GetHeight,
#        data   => OpenGL::Array->new_list(GL_UNSIGNED_BYTE, @data),
#        texture_id => glGenTextures_p(1)
#    };
#    # Create and initialize a texture with the raw data.
#    glBindTexture(GL_TEXTURE_2D, $params->{texture_id});
#    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
#    glTexImage2D_c(GL_TEXTURE_2D, 0, GL_RGBA8, $params->{width}, $params->{height}, 0, GL_RGBA, GL_UNSIGNED_BYTE, $params->{data}->ptr);
#    glBindTexture(GL_TEXTURE_2D, 0);
#    return $params;
#}
#
#sub _variable_layer_thickness_load_overlay_image {
#    my ($self) = @_;
#    $self->{layer_preview_annotation} = $self->_load_image_set_texture('variable_layer_height_tooltip.png')
#        if (! $self->{layer_preview_annotation}->{loaded});
#    return $self->{layer_preview_annotation}->{valid};
#}
#
#sub _variable_layer_thickness_load_reset_image {
#    my ($self) = @_;
#    $self->{layer_preview_reset_image} = $self->_load_image_set_texture('variable_layer_height_reset.png')
#        if (! $self->{layer_preview_reset_image}->{loaded});
#    return $self->{layer_preview_reset_image}->{valid};
#}
#
## Paint the tooltip.
#sub _render_image {
#    my ($self, $image, $l, $r, $b, $t) = @_;
#    $self->_render_texture($image->{texture_id}, $l, $r, $b, $t);
#}
#
#sub _render_texture {
#    my ($self, $tex_id, $l, $r, $b, $t) = @_;
#    
#    glColor4f(1.,1.,1.,1.);
#    glDisable(GL_LIGHTING);
#    glEnable(GL_BLEND);
#    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#    glEnable(GL_TEXTURE_2D);
#    glBindTexture(GL_TEXTURE_2D, $tex_id);
#    glBegin(GL_QUADS);
#    glTexCoord2d(0.,1.); glVertex3f($l, $b, 0);
#    glTexCoord2d(1.,1.); glVertex3f($r, $b, 0);
#    glTexCoord2d(1.,0.); glVertex3f($r, $t, 0);
#    glTexCoord2d(0.,0.); glVertex3f($l, $t, 0);
#    glEnd();
#    glBindTexture(GL_TEXTURE_2D, 0);
#    glDisable(GL_TEXTURE_2D);
#    glDisable(GL_BLEND);
#    glEnable(GL_LIGHTING);
#}
#
#sub draw_active_object_annotations {
#    # $fakecolor is a boolean indicating, that the objects shall be rendered in a color coding the object index for picking.
#    my ($self) = @_;
#
#    return if (! $self->{layer_height_edit_shader} || ! $self->layer_editing_enabled);
#
#    # Find the selected volume, over which the layer editing is active.
#    my $volume;
#    foreach my $volume_idx (0..$#{$self->volumes}) {
#        my $v = $self->volumes->[$volume_idx];
#        if ($v->selected && $v->has_layer_height_texture) {
#            $volume = $v;
#            last;
#        }
#    }
#    return if (! $volume);
#    
#    # If the active object was not allocated at the Print, go away. This should only be a momentary case between an object addition / deletion
#    # and an update by Platter::async_apply_config.
#    my $object_idx = int($volume->select_group_id / 1000000);
#    return if $object_idx >= $self->{print}->object_count;
#
#    # The viewport and camera are set to complete view and glOrtho(-$x/2, $x/2, -$y/2, $y/2, -$depth, $depth), 
#    # where x, y is the window size divided by $self->_zoom.
#    my ($bar_left, $bar_bottom, $bar_right, $bar_top) = $self->_variable_layer_thickness_bar_rect_viewport;
#    my ($reset_left, $reset_bottom, $reset_right, $reset_top) = $self->_variable_layer_thickness_reset_rect_viewport;
#    my $z_cursor_relative = $self->_variable_layer_thickness_bar_mouse_cursor_z_relative;
#
#    my $print_object = $self->{print}->get_object($object_idx);
#    my $z_max = $print_object->model_object->bounding_box->z_max;
#    
#    $self->{layer_height_edit_shader}->enable;
#    $self->{layer_height_edit_shader}->set_uniform('z_to_texture_row',            $volume->layer_height_texture_z_to_row_id);
#    $self->{layer_height_edit_shader}->set_uniform('z_texture_row_to_normalized', 1. / $volume->layer_height_texture_height);
#    $self->{layer_height_edit_shader}->set_uniform('z_cursor',                    $z_max * $z_cursor_relative);
#    $self->{layer_height_edit_shader}->set_uniform('z_cursor_band_width',         $self->{layer_height_edit_band_width});
#    glBindTexture(GL_TEXTURE_2D, $self->{layer_preview_z_texture_id});
#    glTexImage2D_c(GL_TEXTURE_2D, 0, GL_RGBA8, $volume->layer_height_texture_width, $volume->layer_height_texture_height, 
#        0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#    glTexImage2D_c(GL_TEXTURE_2D, 1, GL_RGBA8, $volume->layer_height_texture_width / 2, $volume->layer_height_texture_height / 2,
#        0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#    glTexSubImage2D_c(GL_TEXTURE_2D, 0, 0, 0, $volume->layer_height_texture_width, $volume->layer_height_texture_height,
#        GL_RGBA, GL_UNSIGNED_BYTE, $volume->layer_height_texture_data_ptr_level0);
#    glTexSubImage2D_c(GL_TEXTURE_2D, 1, 0, 0, $volume->layer_height_texture_width / 2, $volume->layer_height_texture_height / 2,
#        GL_RGBA, GL_UNSIGNED_BYTE, $volume->layer_height_texture_data_ptr_level1);
#    
#    # Render the color bar.
#    glDisable(GL_DEPTH_TEST);
#    # The viewport and camera are set to complete view and glOrtho(-$x/2, $x/2, -$y/2, $y/2, -$depth, $depth), 
#    # where x, y is the window size divided by $self->_zoom.
#    glPushMatrix();
#    glLoadIdentity();
#    # Paint the overlay.
#    glBegin(GL_QUADS);
#    glVertex3f($bar_left,  $bar_bottom, 0);
#    glVertex3f($bar_right, $bar_bottom, 0);
#    glVertex3f($bar_right, $bar_top, $z_max);
#    glVertex3f($bar_left,  $bar_top, $z_max);
#    glEnd();
#    glBindTexture(GL_TEXTURE_2D, 0);
#    $self->{layer_height_edit_shader}->disable;
#
#    # Paint the tooltip.
#    if ($self->_variable_layer_thickness_load_overlay_image) 
#        my $gap = 10/$self->_zoom;
#        my ($l, $r, $b, $t) = ($bar_left - $self->{layer_preview_annotation}->{width}/$self->_zoom - $gap, $bar_left - $gap, $reset_bottom + $self->{layer_preview_annotation}->{height}/$self->_zoom + $gap, $reset_bottom + $gap);
#        $self->_render_image($self->{layer_preview_annotation}, $l, $r, $t, $b);
#    }
#
#    # Paint the reset button.
#    if ($self->_variable_layer_thickness_load_reset_image) {
#        $self->_render_image($self->{layer_preview_reset_image}, $reset_left, $reset_right, $reset_bottom, $reset_top);
#    }
#
#    # Paint the graph.
#    #FIXME show some kind of legend.
#    my $max_z = unscale($print_object->size->z);
#    my $profile = $print_object->model_object->layer_height_profile;
#    my $layer_height = $print_object->config->get('layer_height');
#    my $layer_height_max  = 10000000000.;
#    {
#        # Get a maximum layer height value.
#        #FIXME This is a duplicate code of Slicing.cpp.
#        my $nozzle_diameters  = $print_object->print->config->get('nozzle_diameter');
#        my $layer_heights_min = $print_object->print->config->get('min_layer_height');
#        my $layer_heights_max = $print_object->print->config->get('max_layer_height');
#        for (my $i = 0; $i < scalar(@{$nozzle_diameters}); $i += 1) {
#            my $lh_min = ($layer_heights_min->[$i] == 0.) ? 0.07 : max(0.01, $layer_heights_min->[$i]);
#            my $lh_max = ($layer_heights_max->[$i] == 0.) ? (0.75 * $nozzle_diameters->[$i]) : $layer_heights_max->[$i];
#            $layer_height_max = min($layer_height_max, max($lh_min, $lh_max));
#        }
#    }
#    # Make the vertical bar a bit wider so the layer height curve does not touch the edge of the bar region.
#    $layer_height_max *= 1.12;
#    # Baseline
#    glColor3f(0., 0., 0.);
#    glBegin(GL_LINE_STRIP);
#    glVertex2f($bar_left + $layer_height * ($bar_right - $bar_left) / $layer_height_max,  $bar_bottom);
#    glVertex2f($bar_left + $layer_height * ($bar_right - $bar_left) / $layer_height_max,  $bar_top);
#    glEnd();
#    # Curve
#    glColor3f(0., 0., 1.);
#    glBegin(GL_LINE_STRIP);
#    for (my $i = 0; $i < int(@{$profile}); $i += 2) {
#        my $z = $profile->[$i];
#        my $h = $profile->[$i+1];
#        glVertex3f($bar_left + $h * ($bar_right - $bar_left) / $layer_height_max,  $bar_bottom + $z * ($bar_top - $bar_bottom) / $max_z, $z);
#    }
#    glEnd();
#    # Revert the matrices.
#    glPopMatrix();
#    glEnable(GL_DEPTH_TEST);
#}
#
#sub draw_legend {
#    my ($self) = @_;
# 
#    if (!$self->_legend_enabled) {
#        return;
#    }
#
#    # If the legend texture has not been loaded into the GPU, do it now.
#    my $tex_id = Slic3r::GUI::_3DScene::finalize_legend_texture;
#    if ($tex_id > 0)
#    {
#        my $tex_w = Slic3r::GUI::_3DScene::get_legend_texture_width;
#        my $tex_h = Slic3r::GUI::_3DScene::get_legend_texture_height;
#        if (($tex_w > 0) && ($tex_h > 0))
#        {
#            glDisable(GL_DEPTH_TEST);
#            glPushMatrix();
#            glLoadIdentity();
#
#            my ($cw, $ch) = $self->GetSizeWH;
#
#            my $l = (-0.5 * $cw) / $self->_zoom;
#            my $t = (0.5 * $ch) / $self->_zoom;
#            my $r = $l + $tex_w / $self->_zoom;
#            my $b = $t - $tex_h / $self->_zoom;
#            $self->_render_texture($tex_id, $l, $r, $b, $t);
#
#            glPopMatrix();
#            glEnable(GL_DEPTH_TEST);
#        }
#    }
#}
#
#sub draw_warning {
#    my ($self) = @_;
# 
#    if (!$self->_warning_enabled) {
#        return;
#    }
#
#    # If the warning texture has not been loaded into the GPU, do it now.
#    my $tex_id = Slic3r::GUI::_3DScene::finalize_warning_texture;
#    if ($tex_id > 0)
#    {
#        my $tex_w = Slic3r::GUI::_3DScene::get_warning_texture_width;
#        my $tex_h = Slic3r::GUI::_3DScene::get_warning_texture_height;
#        if (($tex_w > 0) && ($tex_h > 0))
#        {
#            glDisable(GL_DEPTH_TEST);
#            glPushMatrix();
#            glLoadIdentity();
#        
#            my ($cw, $ch) = $self->GetSizeWH;
#                
#            my $l = (-0.5 * $tex_w) / $self->_zoom;
#            my $t = (-0.5 * $ch + $tex_h) / $self->_zoom;
#            my $r = $l + $tex_w / $self->_zoom;
#            my $b = $t - $tex_h / $self->_zoom;
#            $self->_render_texture($tex_id, $l, $r, $b, $t);
#
#            glPopMatrix();
#            glEnable(GL_DEPTH_TEST);
#        }
#    }
#}
#
#sub update_volumes_colors_by_extruder {
#    my ($self, $config) = @_;    
#    $self->volumes->update_colors_by_extruder($config);
#}
#
#sub opengl_info
#{
#    my ($self, %params) = @_;
#    my %tag = Slic3r::tags($params{format});
#
#    my $gl_version       = glGetString(GL_VERSION);
#    my $gl_vendor        = glGetString(GL_VENDOR);
#    my $gl_renderer      = glGetString(GL_RENDERER);
#    my $glsl_version     = glGetString(GL_SHADING_LANGUAGE_VERSION);
#
#    my $out = '';
#    $out .= "$tag{h2start}OpenGL installation$tag{h2end}$tag{eol}";
#    $out .= "  $tag{bstart}Using POGL$tag{bend} v$OpenGL::BUILD_VERSION$tag{eol}";
#    $out .= "  $tag{bstart}GL version:   $tag{bend}${gl_version}$tag{eol}";
#    $out .= "  $tag{bstart}vendor:       $tag{bend}${gl_vendor}$tag{eol}";
#    $out .= "  $tag{bstart}renderer:     $tag{bend}${gl_renderer}$tag{eol}";
#    $out .= "  $tag{bstart}GLSL version: $tag{bend}${glsl_version}$tag{eol}";
#
#    # Check for other OpenGL extensions
#    $out .= "$tag{h2start}Installed extensions (* implemented in the module):$tag{h2end}$tag{eol}";
#    my $extensions = glGetString(GL_EXTENSIONS);
#    my @extensions = split(' ',$extensions);
#    foreach my $ext (sort @extensions) {
#        my $stat = glpCheckExtension($ext);
#        $out .= sprintf("%s ${ext}$tag{eol}", $stat?' ':'*');
#        $out .= sprintf("    ${stat}$tag{eol}") if ($stat && $stat !~ m|^$ext |);
#    }
#
#    return $out;
#}
#
#sub _report_opengl_state
#{
#    my ($self, $comment) = @_;
#    my $err = glGetError();
#    return 0 if ($err == 0);
# 
#    # gluErrorString() hangs. Don't use it.
##    my $errorstr = gluErrorString();
#    my $errorstr = '';
#    if ($err == 0x0500) {
#        $errorstr = 'GL_INVALID_ENUM';
#    } elsif ($err == GL_INVALID_VALUE) {
#        $errorstr = 'GL_INVALID_VALUE';
#    } elsif ($err == GL_INVALID_OPERATION) {
#        $errorstr = 'GL_INVALID_OPERATION';
#    } elsif ($err == GL_STACK_OVERFLOW) {
#        $errorstr = 'GL_STACK_OVERFLOW';
#    } elsif ($err == GL_OUT_OF_MEMORY) {
#        $errorstr = 'GL_OUT_OF_MEMORY';
#    } else {        
#        $errorstr = 'unknown';
#    }
#    if (defined($comment)) {
#        printf("OpenGL error at %s, nr %d (0x%x): %s\n", $comment, $err, $err, $errorstr);
#    } else {
#        printf("OpenGL error nr %d (0x%x): %s\n", $err, $err, $errorstr);
#    }
#}
#
#sub _vertex_shader_Gouraud {
#    return <<'VERTEX';
##version 110
#
##define INTENSITY_CORRECTION 0.6
#
#// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
#const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
##define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
##define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
##define LIGHT_TOP_SHININESS  20.0
#
#// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
#const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
##define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
#//#define LIGHT_FRONT_SPECULAR (0.0 * INTENSITY_CORRECTION)
#//#define LIGHT_FRONT_SHININESS 5.0
#
##define INTENSITY_AMBIENT    0.3
#
#const vec3 ZERO = vec3(0.0, 0.0, 0.0);
#
#struct PrintBoxDetection
#{
#    vec3 min;
#    vec3 max;
#    // xyz contains the offset, if w == 1.0 detection needs to be performed
#    vec4 volume_origin;
#};
#
#uniform PrintBoxDetection print_box;
#
#// x = tainted, y = specular;
#varying vec2 intensity;
#
#varying vec3 delta_box_min;
#varying vec3 delta_box_max;
#
#void main()
#{
#    // First transform the normal into camera space and normalize the result.
#    vec3 normal = normalize(gl_NormalMatrix * gl_Normal);
#    
#    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
#    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
#    float NdotL = max(dot(normal, LIGHT_TOP_DIR), 0.0);
#
#    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
#    intensity.y = 0.0;
#
#    if (NdotL > 0.0)
#        intensity.y += LIGHT_TOP_SPECULAR * pow(max(dot(normal, reflect(-LIGHT_TOP_DIR, normal)), 0.0), LIGHT_TOP_SHININESS);
#
#    // Perform the same lighting calculation for the 2nd light source (no specular applied).
#    NdotL = max(dot(normal, LIGHT_FRONT_DIR), 0.0);
#    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;
#
#    // compute deltas for out of print volume detection (world coordinates)
#    if (print_box.volume_origin.w == 1.0)
#    {
#        vec3 v = gl_Vertex.xyz + print_box.volume_origin.xyz;
#        delta_box_min = v - print_box.min;
#        delta_box_max = v - print_box.max;
#    }
#    else
#    {
#        delta_box_min = ZERO;
#        delta_box_max = ZERO;
#    }    
#
#    gl_Position = ftransform();
#} 
#
#VERTEX
#}
#
#sub _fragment_shader_Gouraud {
#    return <<'FRAGMENT';
##version 110
#
#const vec3 ZERO = vec3(0.0, 0.0, 0.0);
#
#// x = tainted, y = specular;
#varying vec2 intensity;
#
#varying vec3 delta_box_min;
#varying vec3 delta_box_max;
#
#uniform vec4 uniform_color;
#
#void main()
#{
#    // if the fragment is outside the print volume -> use darker color
#    vec3 color = (any(lessThan(delta_box_min, ZERO)) || any(greaterThan(delta_box_max, ZERO))) ? mix(uniform_color.rgb, ZERO, 0.3333) : uniform_color.rgb;
#    gl_FragColor = vec4(vec3(intensity.y, intensity.y, intensity.y) + color * intensity.x, uniform_color.a);
#}
#
#FRAGMENT
#}
#
#sub _vertex_shader_Phong {
#    return <<'VERTEX';
##version 110
#
#varying vec3 normal;
#varying vec3 eye;
#void main(void)  
#{
#   eye    = normalize(vec3(gl_ModelViewMatrix * gl_Vertex));
#   normal = normalize(gl_NormalMatrix * gl_Normal);
#   gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
#}
#VERTEX
#}
#
#sub _fragment_shader_Phong {
#    return <<'FRAGMENT';
##version 110
#
##define INTENSITY_CORRECTION 0.7
#
##define LIGHT_TOP_DIR        -0.6/1.31, 0.6/1.31, 1./1.31
##define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
##define LIGHT_TOP_SPECULAR   (0.5 * INTENSITY_CORRECTION)
#//#define LIGHT_TOP_SHININESS  50.
##define LIGHT_TOP_SHININESS  10.
#
##define LIGHT_FRONT_DIR      1./1.43, 0.2/1.43, 1./1.43
##define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
##define LIGHT_FRONT_SPECULAR (0.0 * INTENSITY_CORRECTION)
##define LIGHT_FRONT_SHININESS 50.
#
##define INTENSITY_AMBIENT    0.0
#
#varying vec3 normal;
#varying vec3 eye;
#uniform vec4 uniform_color;
#void main() {
# 
#    float intensity_specular = 0.;
#    float intensity_tainted  = 0.;
#    float intensity = max(dot(normal,vec3(LIGHT_TOP_DIR)), 0.0);
#    // if the vertex is lit compute the specular color
#    if (intensity > 0.0) {
#        intensity_tainted = LIGHT_TOP_DIFFUSE * intensity;
#        // compute the half vector
#        vec3 h = normalize(vec3(LIGHT_TOP_DIR) + eye);  
#        // compute the specular term into spec
#        intensity_specular = LIGHT_TOP_SPECULAR * pow(max(dot(h, normal), 0.0), LIGHT_TOP_SHININESS);
#    }
#    intensity = max(dot(normal,vec3(LIGHT_FRONT_DIR)), 0.0);
#    // if the vertex is lit compute the specular color
#    if (intensity > 0.0) {
#        intensity_tainted += LIGHT_FRONT_DIFFUSE * intensity;
#        // compute the half vector
#//        vec3 h = normalize(vec3(LIGHT_FRONT_DIR) + eye);
#        // compute the specular term into spec
#//        intensity_specular += LIGHT_FRONT_SPECULAR * pow(max(dot(h,normal), 0.0), LIGHT_FRONT_SHININESS);
#    }
#    
#    gl_FragColor = max(
#        vec4(intensity_specular, intensity_specular, intensity_specular, 0.) + uniform_color * intensity_tainted, 
#        INTENSITY_AMBIENT * uniform_color);
#    gl_FragColor.a = uniform_color.a;
#}
#FRAGMENT
#}
#
#sub _vertex_shader_variable_layer_height {
#    return <<'VERTEX';
##version 110
#
##define INTENSITY_CORRECTION 0.6
#
#const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
##define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
##define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
##define LIGHT_TOP_SHININESS  20.0
#
#const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
##define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
#//#define LIGHT_FRONT_SPECULAR (0.0 * INTENSITY_CORRECTION)
#//#define LIGHT_FRONT_SHININESS 5.0
#
##define INTENSITY_AMBIENT    0.3
#
#// x = tainted, y = specular;
#varying vec2 intensity;
#
#varying float object_z;
#
#void main()
#{
#    // First transform the normal into camera space and normalize the result.
#    vec3 normal = normalize(gl_NormalMatrix * gl_Normal);
#    
#    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
#    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
#    float NdotL = max(dot(normal, LIGHT_TOP_DIR), 0.0);
#
#    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
#    intensity.y = 0.0;
#
#    if (NdotL > 0.0)
#        intensity.y += LIGHT_TOP_SPECULAR * pow(max(dot(normal, reflect(-LIGHT_TOP_DIR, normal)), 0.0), LIGHT_TOP_SHININESS);
#
#    // Perform the same lighting calculation for the 2nd light source (no specular)
#    NdotL = max(dot(normal, LIGHT_FRONT_DIR), 0.0);
#    
#    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;
#    
#    // Scaled to widths of the Z texture.
#    object_z = gl_Vertex.z;
#
#    gl_Position = ftransform();
#} 
#
#VERTEX
#}
#
#sub _fragment_shader_variable_layer_height {
#    return <<'FRAGMENT';
##version 110
#
##define M_PI 3.1415926535897932384626433832795
#
#// 2D texture (1D texture split by the rows) of color along the object Z axis.
#uniform sampler2D z_texture;
#// Scaling from the Z texture rows coordinate to the normalized texture row coordinate.
#uniform float z_to_texture_row;
#uniform float z_texture_row_to_normalized;
#uniform float z_cursor;
#uniform float z_cursor_band_width;
#
#// x = tainted, y = specular;
#varying vec2 intensity;
#
#varying float object_z;
#
#void main()
#{
#    float object_z_row = z_to_texture_row * object_z;
#    // Index of the row in the texture.
#    float z_texture_row = floor(object_z_row);
#    // Normalized coordinate from 0. to 1.
#    float z_texture_col = object_z_row - z_texture_row;
#    float z_blend = 0.25 * cos(min(M_PI, abs(M_PI * (object_z - z_cursor) * 1.8 / z_cursor_band_width))) + 0.25;
#    // Calculate level of detail from the object Z coordinate.
#    // This makes the slowly sloping surfaces to be show with high detail (with stripes),
#    // and the vertical surfaces to be shown with low detail (no stripes)
#    float z_in_cells    = object_z_row * 190.;
#    // Gradient of Z projected on the screen.
#    float dx_vtc        = dFdx(z_in_cells);
#    float dy_vtc        = dFdy(z_in_cells);
#    float lod           = clamp(0.5 * log2(max(dx_vtc*dx_vtc, dy_vtc*dy_vtc)), 0., 1.);
#    // Sample the Z texture. Texture coordinates are normalized to <0, 1>.
#    vec4 color       =
#        mix(texture2D(z_texture, vec2(z_texture_col, z_texture_row_to_normalized * (z_texture_row + 0.5    )), -10000.),
#            texture2D(z_texture, vec2(z_texture_col, z_texture_row_to_normalized * (z_texture_row * 2. + 1.)),  10000.), lod);
#            
#    // Mix the final color.
#    gl_FragColor = 
#        vec4(intensity.y, intensity.y, intensity.y, 1.0) +  intensity.x * mix(color, vec4(1.0, 1.0, 0.0, 1.0), z_blend);
#}
#
#FRAGMENT
#}
#===================================================================================================================================        

# The 3D canvas to display objects and tool paths.
package Slic3r::GUI::3DScene;
use base qw(Slic3r::GUI::3DScene::Base);

#===================================================================================================================================        
#use OpenGL qw(:glconstants :gluconstants :glufunctions);
#use List::Util qw(first min max);
#use Slic3r::Geometry qw(scale unscale epsilon);
#use Slic3r::Print::State ':steps';
#===================================================================================================================================        

#===================================================================================================================================        
#__PACKAGE__->mk_accessors(qw(
#    color_by
#    select_by
#    drag_by
#));
#===================================================================================================================================        

sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new(@_);
#===================================================================================================================================        
#    $self->color_by('volume');      # object | volume
#    $self->select_by('object');     # object | volume | instance
#    $self->drag_by('instance');     # object | instance
#===================================================================================================================================        
    
    return $self;
}

#==============================================================================================================================
#sub load_object {
#    my ($self, $model, $print, $obj_idx, $instance_idxs) = @_;
#    
#    $self->SetCurrent($self->GetContext) if $useVBOs;
#
#    my $model_object;
#    if ($model->isa('Slic3r::Model::Object')) {
#        $model_object = $model;
#        $model = $model_object->model;
#        $obj_idx = 0;
#    } else {
#        $model_object = $model->get_object($obj_idx);
#    }
#    
#    $instance_idxs ||= [0..$#{$model_object->instances}];
#    my $volume_indices = $self->volumes->load_object(
#        $model_object, $obj_idx, $instance_idxs, $self->color_by, $self->select_by, $self->drag_by,
#        $self->UseVBOs);
#    return @{$volume_indices};
#}
#
## Create 3D thick extrusion lines for a skirt and brim.
## Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes.
#sub load_print_toolpaths {
#    my ($self, $print, $colors) = @_;
#
#    $self->SetCurrent($self->GetContext) if $self->UseVBOs;
#    Slic3r::GUI::_3DScene::_load_print_toolpaths($print, $self->volumes, $colors, $self->UseVBOs)
#        if ($print->step_done(STEP_SKIRT) && $print->step_done(STEP_BRIM));
#}
#
## Create 3D thick extrusion lines for object forming extrusions.
## Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes,
## one for perimeters, one for infill and one for supports.
#sub load_print_object_toolpaths {
#    my ($self, $object, $colors) = @_;
#
#    $self->SetCurrent($self->GetContext) if $self->UseVBOs;
#    Slic3r::GUI::_3DScene::_load_print_object_toolpaths($object, $self->volumes, $colors, $self->UseVBOs);
#}
#
## Create 3D thick extrusion lines for wipe tower extrusions.
#sub load_wipe_tower_toolpaths {
#    my ($self, $print, $colors) = @_;
#       
#    $self->SetCurrent($self->GetContext) if $self->UseVBOs;
#    Slic3r::GUI::_3DScene::_load_wipe_tower_toolpaths($print, $self->volumes, $colors, $self->UseVBOs)
#        if ($print->step_done(STEP_WIPE_TOWER));
#}
#
#sub load_gcode_preview {
#    my ($self, $print, $gcode_preview_data, $colors) = @_;
#
#    $self->SetCurrent($self->GetContext) if $self->UseVBOs;
#    Slic3r::GUI::_3DScene::load_gcode_preview($print, $gcode_preview_data, $self->volumes, $colors, $self->UseVBOs);
#}
#
#sub set_toolpaths_range {
#    my ($self, $min_z, $max_z) = @_;
#    $self->volumes->set_range($min_z, $max_z);
#}
#
#sub reset_legend_texture {
#    Slic3r::GUI::_3DScene::reset_legend_texture();
#}
#
#sub get_current_print_zs {
#    my ($self, $active_only) = @_;
#    return $self->volumes->get_current_print_zs($active_only);
#}
#==============================================================================================================================

1;
