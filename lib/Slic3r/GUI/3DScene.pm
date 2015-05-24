package Slic3r::GUI::3DScene::Base;
use strict;
use warnings;

use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions :gluconstants);
use base qw(Wx::GLCanvas Class::Accessor);
use Math::Trig qw(asin);
use List::Util qw(reduce min max first);
use Slic3r::Geometry qw(X Y Z MIN MAX triangle_normal normalize deg2rad tan scale unscale scaled_epsilon);
use Slic3r::Geometry::Clipper qw(offset_ex intersection_pl);
use Wx::GLCanvas qw(:all);
 
__PACKAGE__->mk_accessors( qw(_quat _dirty init
                              enable_cutting
                              enable_picking
                              enable_moving
                              on_viewport_changed
                              on_hover
                              on_select
                              on_double_click
                              on_right_click
                              on_move
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
                              _camera_target
                              _zoom
                              ) );

use constant TRACKBALLSIZE  => 0.8;
use constant TURNTABLE_MODE => 1;
use constant GROUND_Z       => -0.02;
use constant DEFAULT_COLOR  => [1,1,0];
use constant SELECTED_COLOR => [0,1,0,1];
use constant HOVER_COLOR    => [0.4,0.9,0,1];

# make OpenGL::Array thread-safe
{
    no warnings 'redefine';
    *OpenGL::Array::CLONE_SKIP = sub { 1 };
}

sub new {
    my ($class, $parent) = @_;
    
    # we request a depth buffer explicitely because it looks like it's not created by 
    # default on Linux, causing transparency issues
    my $self = $class->SUPER::new($parent, -1, Wx::wxDefaultPosition, Wx::wxDefaultSize, 0, "",
        [WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 16, 0]);
   
    $self->background(1);
    $self->_quat((0, 0, 0, 1));
    $self->_stheta(45);
    $self->_sphi(45);
    $self->_zoom(1);
    
    # 3D point in model space
    $self->_camera_target(Slic3r::Pointf3->new(0,0,0));
    
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
    EVT_MOUSEWHEEL($self, sub {
        my ($self, $e) = @_;
        
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
        $self->_dirty(1);
        $self->Refresh;
    });
    EVT_MOUSE_EVENTS($self, \&mouse_event);
    
    return $self;
}

sub mouse_event {
    my ($self, $e) = @_;
    
    my $pos = Slic3r::Pointf->new($e->GetPositionXY);
    if ($e->Entering && &Wx::wxMSW) {
        # wxMSW needs focus in order to catch mouse wheel events
        $self->SetFocus;
    } elsif ($e->LeftDClick) {
        $self->on_double_click->()
            if $self->on_double_click;
    } elsif ($e->LeftDown || $e->RightDown) {
        # If user pressed left or right button we first check whether this happened
        # on a volume or not.
        my $volume_idx = $self->_hover_volume_idx // -1;
        
        # select volume in this 3D canvas
        if ($self->enable_picking) {
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
    } elsif ($e->Dragging && $e->LeftIsDown && defined($self->_drag_volume_idx)) {
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
        if ($e->LeftIsDown) {
            # if dragging over blank area with left button, rotate
            if (defined $self->_drag_start_pos) {
                my $orig = $self->_drag_start_pos;
                if (TURNTABLE_MODE) {
                    $self->_sphi($self->_sphi + ($pos->x - $orig->x) * TRACKBALLSIZE);
                    $self->_stheta($self->_stheta - ($pos->y - $orig->y) * TRACKBALLSIZE);        #-
                    $self->_stheta(150) if $self->_stheta > 150;
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
        if ($self->on_move && defined($self->_drag_volume_idx) && $self->_dragged) {
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
        $self->Refresh;
    } else {
        $e->Skip();
    }
}

sub reset_objects {
    my ($self) = @_;
    
    $self->volumes([]);
    $self->_dirty(1);
}

sub set_viewport_from_scene {
    my ($self, $scene) = @_;
    
    $self->_sphi($scene->_sphi);
    $self->_stheta($scene->_stheta);
    $self->_camera_target($scene->_camera_target);
    $self->_zoom($scene->_zoom);
    $self->_quat($scene->_quat);
    $self->_dirty(1);
}

sub zoom_to_bounding_box {
    my ($self, $bb) = @_;
    
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
    $bb->merge_point(Slic3r::Pointf3->new(@$_, 0)) for @{$self->bed_shape};
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
    my ($self, $z) = @_;
    
    $self->cutting_plane_z($z);
    
    # perform cut and cache section lines
    my @verts = ();
    foreach my $volume (@{$self->volumes}) {
        foreach my $volume (@{$self->volumes}) {
            next if !$volume->mesh;
            my $expolygons = $volume->mesh->slice([ $z - $volume->origin->z ])->[0];
            $expolygons = offset_ex([ map @$_, @$expolygons ], scale 0.1);
            
            foreach my $line (map @{$_->lines}, map @$_, @$expolygons) {
                push @verts, (
                    unscale($line->a->x), unscale($line->a->y), $z,  #))
                    unscale($line->b->x), unscale($line->b->y), $z,  #))
                );
            }
        }
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
    my $depth = 10 * max(@{ $self->max_bounding_box->size });
    glOrtho(
        -$x/2, $x/2, -$y/2, $y/2,
        -$depth, 2*$depth,
    );
    
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
    
    if ($self->enable_picking) {
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
    }
    
    # draw fixed background
    if ($self->background) {
        glDisable(GL_LIGHTING);
        glPushMatrix();
        glLoadIdentity();
        
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        
        glBegin(GL_QUADS);
        glColor3f(0.0,0.0,0.0);
        glVertex2f(-1.0,-1.0);
        glVertex2f(1,-1.0);
        glColor3f(10/255,98/255,144/255);
        glVertex2f(1, 1);
        glVertex2f(-1.0, 1);
        glEnd();
        glPopMatrix();
        
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
        glVertexPointer_p(3, $self->bed_triangles);
        glDrawArrays(GL_TRIANGLES, 0, $self->bed_triangles->elements / 3);
        glDisableClientState(GL_VERTEX_ARRAY);
        
        # we need depth test for grid, otherwise it would disappear when looking
        # the object from below
        glEnable(GL_DEPTH_TEST);
    
        # draw grid
        glLineWidth(3);
        glColor4f(0.2, 0.2, 0.2, 0.4);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer_p(3, $self->bed_grid_lines);
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
    
    glFlush();
 
    $self->SwapBuffers();
}

sub draw_volumes {
    my ($self, $fakecolor) = @_;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    
    foreach my $volume_idx (0..$#{$self->volumes}) {
        my $volume = $self->volumes->[$volume_idx];
        glPushMatrix();
        glTranslatef(@{$volume->origin});
        
        if ($fakecolor) {
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
        
        my @sorted_z = ();
        my ($min_z, $max_z);
        if ($volume->range && $volume->offsets) {
            @sorted_z = sort { $a <=> $b } keys %{$volume->offsets};
            
            ($min_z, $max_z) = @{$volume->range};
            $min_z = first { $_ >= $min_z } @sorted_z;
            $max_z = first { $_ > $max_z }  @sorted_z;
        }
        
        glCullFace(GL_BACK);
        if ($volume->qverts) {
            my ($min_offset, $max_offset);
            if (defined $min_z) {
                $min_offset = $volume->offsets->{$min_z}->[0];
            }
            if (defined $max_z) {
                $max_offset = $volume->offsets->{$max_z}->[0];
            }
            $min_offset //= 0;
            $max_offset //= $volume->qverts->size;
            
            glVertexPointer_c(3, GL_FLOAT, 0, $volume->qverts->verts_ptr);
            glNormalPointer_c(GL_FLOAT, 0, $volume->qverts->norms_ptr);
            glDrawArrays(GL_QUADS, $min_offset / 3, ($max_offset-$min_offset) / 3);
        }
        
        if ($volume->tverts) {
            my ($min_offset, $max_offset);
            if (defined $min_z) {
                $min_offset = $volume->offsets->{$min_z}->[1];
            }
            if (defined $max_z) {
                $max_offset = $volume->offsets->{$max_z}->[1];
            }
            $min_offset //= 0;
            $max_offset //= $volume->tverts->size;
            
            glVertexPointer_c(3, GL_FLOAT, 0, $volume->tverts->verts_ptr);
            glNormalPointer_c(GL_FLOAT, 0, $volume->tverts->norms_ptr);
            glDrawArrays(GL_TRIANGLES, $min_offset / 3, ($max_offset-$min_offset) / 3);
        }
        
        glPopMatrix();
    }
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisable(GL_BLEND);
    
    if (defined $self->cutting_plane_z) {
        glLineWidth(2);
        glColor3f(0, 0, 0);
        glVertexPointer_p(3, $self->cut_lines_vertices);
        glDrawArrays(GL_LINES, 0, $self->cut_lines_vertices->elements / 3);
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}

package Slic3r::GUI::3DScene::Volume;
use Moo;

has 'bounding_box'      => (is => 'ro', required => 1);
has 'origin'            => (is => 'rw', default => sub { Slic3r::Pointf3->new(0,0,0) });
has 'color'             => (is => 'ro', required => 1);
has 'select_group_id'   => (is => 'rw', default => sub { -1 });
has 'drag_group_id'     => (is => 'rw', default => sub { -1 });
has 'selected'          => (is => 'rw', default => sub { 0 });
has 'hover'             => (is => 'rw', default => sub { 0 });
has 'range'             => (is => 'rw');

# geometric data
has 'qverts'            => (is => 'rw');  # GLVertexArray object
has 'tverts'            => (is => 'rw');  # GLVertexArray object
has 'mesh'              => (is => 'rw');  # only required for cut contours
has 'offsets'           => (is => 'rw');  # [ z => [ qverts_idx, tverts_idx ] ]

sub transformed_bounding_box {
    my ($self) = @_;
    
    my $bb = $self->bounding_box;
    $bb->translate(@{$self->origin});
    return $bb;
}

package Slic3r::GUI::3DScene;
use base qw(Slic3r::GUI::3DScene::Base);

use OpenGL qw(:glconstants :gluconstants :glufunctions);
use List::Util qw(first min max);
use Slic3r::Geometry qw(scale unscale epsilon);
use Slic3r::Print::State ':steps';

use constant COLORS => [ [1,1,0,1], [1,0.5,0.5,1], [0.5,1,0.5,1], [0.5,0.5,1,1] ];

__PACKAGE__->mk_accessors(qw(
    color_by
    select_by
    drag_by
    volumes_by_object
    _objects_by_volumes
));

sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new(@_);
    $self->color_by('volume');      # object | volume
    $self->select_by('object');     # object | volume | instance
    $self->drag_by('instance');     # object | instance
    $self->volumes_by_object({});   # obj_idx => [ volume_idx, volume_idx ... ]
    $self->_objects_by_volumes({}); # volume_idx => [ obj_idx, instance_idx ]
    
    return $self;
}

sub load_object {
    my ($self, $model, $obj_idx, $instance_idxs) = @_;
    
    my $model_object;
    if ($model->isa('Slic3r::Model::Object')) {
        $model_object = $model;
        $model = $model_object->model;
        $obj_idx = 0;
    } else {
        $model_object = $model->get_object($obj_idx);
    }
    
    $instance_idxs ||= [0..$#{$model_object->instances}];
    
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
        
            my $color = [ @{COLORS->[ $color_idx % scalar(@{&COLORS}) ]} ];
            $color->[3] = $volume->modifier ? 0.5 : 1;
            push @{$self->volumes}, my $v = Slic3r::GUI::3DScene::Volume->new(
                bounding_box    => $mesh->bounding_box,
                color           => $color,
            );
            $v->mesh($mesh) if $self->enable_cutting;
            if ($self->select_by eq 'object') {
                $v->select_group_id($obj_idx*1000000);
            } elsif ($self->select_by eq 'volume') {
                $v->select_group_id($obj_idx*1000000 + $volume_idx*1000);
            } elsif ($self->select_by eq 'instance') {
                $v->select_group_id($obj_idx*1000000 + $volume_idx*1000 + $instance_idx);
            }
            if ($self->drag_by eq 'object') {
                $v->drag_group_id($obj_idx*1000);
            } elsif ($self->drag_by eq 'instance') {
                $v->drag_group_id($obj_idx*1000 + $instance_idx);
            }
            push @volumes_idx, my $scene_volume_idx = $#{$self->volumes};
            $self->_objects_by_volumes->{$scene_volume_idx} = [ $obj_idx, $volume_idx, $instance_idx ];
            
            my $verts = Slic3r::GUI::_3DScene::GLVertexArray->new;
            $verts->load_mesh($mesh);
            $v->tverts($verts);
        }
    }
    
    $self->volumes_by_object->{$obj_idx} = [@volumes_idx];
    return @volumes_idx;
}

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

sub load_print_toolpaths {
    my ($self, $print) = @_;
    
    return if !$print->step_done(STEP_SKIRT);
    return if !$print->step_done(STEP_BRIM);
    return if !$print->has_skirt && $print->config->brim_width == 0;
    
    my $qverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $tverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my %offsets = ();  # print_z => [ qverts, tverts ]
    
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
        $offsets{$top_z} = [$qverts->size, $tverts->size];
        
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
        offsets         => { %offsets },
    );
}

sub load_print_object_toolpaths {
    my ($self, $object) = @_;
    
    my $perim_qverts    = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $perim_tverts    = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $infill_qverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $infill_tverts   = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $support_qverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    my $support_tverts  = Slic3r::GUI::_3DScene::GLVertexArray->new;
    
    my %perim_offsets   = ();  # print_z => [ qverts, tverts ]
    my %infill_offsets  = ();
    my %support_offsets = ();
    
    # order layers by print_z
    my @layers = sort { $a->print_z <=> $b->print_z }
        @{$object->layers}, @{$object->support_layers};
    
    foreach my $layer (@layers) {
        my $top_z = $layer->print_z;
        
        if (!exists $perim_offsets{$top_z}) {
            $perim_offsets{$top_z} = [
                $perim_qverts->size, $perim_tverts->size,
            ];
            $infill_offsets{$top_z} = [
                $infill_qverts->size, $infill_tverts->size,
            ];
            $support_offsets{$top_z} = [
                $support_qverts->size, $support_tverts->size,
            ];
        }
        
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
    }
    
    my $obb = $object->bounding_box;
    my $bb = Slic3r::Geometry::BoundingBoxf3->new;
    foreach my $copy (@{ $object->_shifted_copies }) {
        my $cbb = $obb->clone;
        $cbb->translate(@$copy);
        $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$cbb->min_point}, 0));
        $bb->merge_point(Slic3r::Pointf3->new_unscale(@{$cbb->max_point}, $object->size->z));
    }
    
    push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
        bounding_box    => $bb,
        color           => COLORS->[0],
        qverts          => $perim_qverts,
        tverts          => $perim_tverts,
        offsets         => { %perim_offsets },
    );
    
    push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
        bounding_box    => $bb,
        color           => COLORS->[1],
        qverts          => $infill_qverts,
        tverts          => $infill_tverts,
        offsets         => { %infill_offsets },
    );
    
    push @{$self->volumes}, Slic3r::GUI::3DScene::Volume->new(
        bounding_box    => $bb,
        color           => COLORS->[2],
        qverts          => $support_qverts,
        tverts          => $support_tverts,
        offsets         => { %support_offsets },
    );
}

sub set_toolpaths_range {
    my ($self, $min_z, $max_z) = @_;
    
    foreach my $volume (@{$self->volumes}) {
        $volume->range([ $min_z, $max_z ]);
    }
}

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
        $closed  = 1;
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
    Slic3r::GUI::_3DScene::_extrusionentity_to_verts_do($lines, $widths, $heights,
        $closed, $top_z, $copy, $qverts, $tverts);
}

sub object_idx {
    my ($self, $volume_idx) = @_;
    return $self->_objects_by_volumes->{$volume_idx}[0];
}

sub volume_idx {
    my ($self, $volume_idx) = @_;
    return $self->_objects_by_volumes->{$volume_idx}[1];
}

sub instance_idx {
    my ($self, $volume_idx) = @_;
    return $self->_objects_by_volumes->{$volume_idx}[2];
}

1;
