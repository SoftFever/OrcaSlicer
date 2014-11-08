package Slic3r::GUI::PreviewCanvas;
use strict;
use warnings;

use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions);
use base qw(Wx::GLCanvas Class::Accessor);
use Math::Trig qw(asin);
use List::Util qw(reduce min max first);
use Slic3r::Geometry qw(X Y Z MIN MAX triangle_normal normalize deg2rad tan scale unscale);
use Slic3r::Geometry::Clipper qw(offset_ex intersection_pl);
use Wx::GLCanvas qw(:all);
 
__PACKAGE__->mk_accessors( qw(quat dirty init mview_init
                              object_bounding_box
                              volumes initpos
                              sphi stheta
                              cutting_plane_z
                              cut_lines_vertices
                              bed_triangles
                              bed_grid_lines
                              origin
                              ) );

use constant TRACKBALLSIZE => 0.8;
use constant TURNTABLE_MODE => 1;
use constant GROUND_Z       => 0.02;
use constant SELECTED_COLOR => [0,1,0,1];
use constant COLORS => [ [1,1,0], [1,0.5,0.5], [0.5,1,0.5], [0.5,0.5,1] ];

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
   
    $self->quat((0, 0, 0, 1));
    $self->sphi(45);
    $self->stheta(-45);
    
    $self->reset_objects;
    
    EVT_PAINT($self, sub {
        my $dc = Wx::PaintDC->new($self);
        return if !$self->object_bounding_box;
        $self->Render($dc);
    });
    EVT_SIZE($self, sub { $self->dirty(1) });
    EVT_IDLE($self, sub {
        return unless $self->dirty;
        return if !$self->IsShownOnScreen;
        return if !$self->object_bounding_box;
        $self->Resize( $self->GetSizeWH );
        $self->Refresh;
    });
    EVT_MOUSEWHEEL($self, sub {
        my ($self, $e) = @_;
        
        my $zoom = ($e->GetWheelRotation() / $e->GetWheelDelta() / 10);
        $zoom = $zoom > 0 ?  (1.0 + $zoom) : 1 / (1.0 - $zoom);
        my @pos3d = $self->mouse_to_3d($e->GetX(), $e->GetY());
        $self->ZoomTo($zoom, $pos3d[0], $pos3d[1]);
        
        $self->Refresh;
    });
    EVT_MOUSE_EVENTS($self, sub {
        my ($self, $e) = @_;

        if ($e->Dragging() && $e->LeftIsDown()) {
            $self->handle_rotation($e);
        } elsif ($e->Dragging() && $e->RightIsDown()) {
            $self->handle_translation($e);
        } elsif ($e->LeftUp() || $e->RightUp()) {
            $self->initpos(undef);
        } else {
            $e->Skip();
        }
    });
    
    return $self;
}

sub reset_objects {
    my ($self) = @_;
    
    $self->volumes([]);
    $self->dirty(1);
}

# this method accepts a Slic3r::BoudingBox3f object
sub set_bounding_box {
    my ($self, $bb) = @_;
    
    $self->object_bounding_box($bb);
    $self->dirty(1);
}

sub set_auto_bed_shape {
    my ($self, $bed_shape) = @_;
    
    # draw a default square bed around object center
    my $max_size = max(@{ $self->object_bounding_box->size });
    my $center = $self->object_bounding_box->center;
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
        my @lines = ();
        for (my $x = $bed_bb->x_min; $x <= $bed_bb->x_max; $x += scale 10) {
            push @lines, Slic3r::Polyline->new([$x,$bed_bb->y_min], [$x,$bed_bb->y_max]);
        }
        for (my $y = $bed_bb->y_min; $y <= $bed_bb->y_max; $y += scale 10) {
            push @lines, Slic3r::Polyline->new([$bed_bb->x_min,$y], [$bed_bb->x_max,$y]);
        }
        @lines = @{intersection_pl(\@lines, [ @$expolygon ])};
        my @points = ();
        foreach my $polyline (@lines) {
            push @points, map {+ unscale($_->x), unscale($_->y), GROUND_Z } @$polyline;  #))
        }
        $self->bed_grid_lines(OpenGL::Array->new_list(GL_FLOAT, @points));
    }
    
    $self->origin(Slic3r::Pointf->new(0,0));
}

sub load_object {
    my ($self, $object, $all_instances) = @_;
    
    my $z_min = $object->raw_bounding_box->z_min;
    
    # color mesh(es) by material
    my @materials = ();
    
    # sort volumes: non-modifiers first
    my @volumes = sort { ($a->modifier // 0) <=> ($b->modifier // 0) } @{$object->volumes};
    foreach my $volume (@volumes) {
        my @instances = $all_instances ? @{$object->instances} : $object->instances->[0];
        foreach my $instance (@instances) {
            my $mesh = $volume->mesh->clone;
            $instance->transform_mesh($mesh);
            
            my $material_id = $volume->material_id // '_';
            my $color_idx = first { $materials[$_] eq $material_id } 0..$#materials;
            if (!defined $color_idx) {
                push @materials, $material_id;
                $color_idx = $#materials;
            }
        
            my $color = [ @{COLORS->[ $color_idx % scalar(@{&COLORS}) ]} ];
            push @$color, $volume->modifier ? 0.5 : 1;
            push @{$self->volumes}, my $v = {
                mesh  => $mesh,
                color => $color,
                z_min => $z_min,
            };
        
            {
                my $vertices = $mesh->vertices;
                my @verts = map @{ $vertices->[$_] }, map @$_, @{$mesh->facets};
                $v->{verts} = OpenGL::Array->new_list(GL_FLOAT, @verts);
            }
        
            {
                my @norms = map { @$_, @$_, @$_ } @{$mesh->normals};
                $v->{norms} = OpenGL::Array->new_list(GL_FLOAT, @norms);
            }
        }
    }
}

sub SetCuttingPlane {
    my ($self, $z) = @_;
    
    $self->cutting_plane_z($z);
    
    # perform cut and cache section lines
    my @verts = ();
    foreach my $volume (@{$self->volumes}) {
        foreach my $volume (@{$self->volumes}) {
            my $expolygons = $volume->{mesh}->slice([ $z + $volume->{z_min} ])->[0];
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

sub handle_rotation {
    my ($self, $e) = @_;

    if (not defined $self->initpos) {
        $self->initpos($e->GetPosition());
    } else {
        my $orig = $self->initpos;
        my $new = $e->GetPosition();
        my $size = $self->GetClientSize();
        if (TURNTABLE_MODE) {
            $self->sphi($self->sphi + ($new->x - $orig->x)*TRACKBALLSIZE);
            $self->stheta($self->stheta + ($new->y - $orig->y)*TRACKBALLSIZE);        #-
        } else {
            my @quat = trackball($orig->x / ($size->width / 2) - 1,
                            1 - $orig->y / ($size->height / 2),       #/
                            $new->x / ($size->width / 2) - 1,
                            1 - $new->y / ($size->height / 2),        #/
                            );
            $self->quat(mulquats($self->quat, \@quat));
        }
        $self->initpos($new);
        $self->Refresh;
    }
}

sub handle_translation {
    my ($self, $e) = @_;

    if (not defined $self->initpos) {
        $self->initpos($e->GetPosition());
    } else {
        my $new = $e->GetPosition();
        my $orig = $self->initpos;
        my @orig3d = $self->mouse_to_3d($orig->x, $orig->y);             #)()
        my @new3d = $self->mouse_to_3d($new->x, $new->y);                #)()
        glTranslatef($new3d[0] - $orig3d[0], $new3d[1] - $orig3d[1], 0);
        $self->initpos($new);
        $self->Refresh;
    }
}

sub mouse_to_3d {
    my ($self, $x, $y) = @_;

    my @viewport    = glGetIntegerv_p(GL_VIEWPORT);             # 4 items
    my @mview       = glGetDoublev_p(GL_MODELVIEW_MATRIX);      # 16 items
    my @proj        = glGetDoublev_p(GL_PROJECTION_MATRIX);     # 16 items

    my @projected = gluUnProject_p($x, $viewport[3] - $y, 1.0, @mview, @proj, @viewport);
    return @projected;
}

sub ZoomTo {
    my ($self, $factor, $tox, $toy) =  @_;
    
    return if !$self->init;
    glTranslatef($tox, $toy, 0);
    glMatrixMode(GL_MODELVIEW);
    $self->Zoom($factor);
    glTranslatef(-$tox, -$toy, 0);
}

sub Zoom {
    my ($self, $factor) =  @_;
    
    glMatrixMode(GL_MODELVIEW);
    glScalef($factor, $factor, 1);
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

sub ResetModelView {
    my ($self, $factor) = @_;
    
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    my $win_size = $self->GetClientSize();
    my $ratio = $factor * min($win_size->width, $win_size->height) / (2 * max(@{ $self->object_bounding_box->size }));
    glScalef($ratio, $ratio, 1);
}

sub Resize {
    my ($self, $x, $y) = @_;
 
    return unless $self->GetContext;
    $self->dirty(0);
 
    $self->SetCurrent($self->GetContext);
    glViewport(0, 0, $x, $y);
 
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(
        -$x/2, $x/2, -$y/2, $y/2,
        -200, 10 * max(@{ $self->object_bounding_box->size }),
    );
 
    glMatrixMode(GL_MODELVIEW);
    unless ($self->mview_init) {
        $self->mview_init(1);
        $self->ResetModelView(0.9);
    }
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
    
    # ambient lighting
    glLightModelfv_p(GL_LIGHT_MODEL_AMBIENT, 0.1, 0.1, 0.1, 1);
    
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glLightfv_p(GL_LIGHT0, GL_POSITION, 0.5, 0.5, 1, 0);
    glLightfv_p(GL_LIGHT0, GL_SPECULAR, 0.5, 0.5, 0.5, 1);
    glLightfv_p(GL_LIGHT0, GL_DIFFUSE,  0.8, 0.8, 0.8, 1);
    glLightfv_p(GL_LIGHT1, GL_POSITION, 1, 0, 0.5, 0);
    glLightfv_p(GL_LIGHT1, GL_SPECULAR, 0.5, 0.5, 0.5, 1);
    glLightfv_p(GL_LIGHT1, GL_DIFFUSE,  1, 1, 1, 1);
    
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
    glPushMatrix();
    
    my $bb = $self->object_bounding_box;
    my $object_size = $bb->size;
    glTranslatef(0, 0, -max(@$object_size[0..1]));
    my @rotmat = quat_to_rotmatrix($self->quat);
    glMultMatrixd_p(@rotmat[0..15]);
    glRotatef($self->stheta, 1, 0, 0);
    glRotatef($self->sphi, 0, 0, 1);
    
    # center everything around 0,0 since that's where we're looking at (glOrtho())
    my $center = $bb->center;
    glTranslatef(-$center->x, -$center->y, 0); #,,
    
    # draw objects
    $self->draw_mesh;
    
    # draw ground and axes
    glDisable(GL_LIGHTING);
    my $z0 = 0;
    
    {
        # draw ground
        my $ground_z = GROUND_Z;
        if ($self->bed_triangles) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            
            glEnableClientState(GL_VERTEX_ARRAY);
            glColor4f(0.5, 0.5, 0.5, 0.3);
            glNormal3d(0,0,1);
            glVertexPointer_p(3, $self->bed_triangles);
            glDrawArrays(GL_TRIANGLES, 0, $self->bed_triangles->elements / 3);
            glDisableClientState(GL_VERTEX_ARRAY);
            
            glDisable(GL_BLEND);
        
            # draw grid
            glTranslatef(0, 0, 0.02);
            glLineWidth(3);
            glColor3f(1.0, 1.0, 1.0);
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer_p(3, $self->bed_grid_lines);
            glDrawArrays(GL_LINES, 0, $self->bed_grid_lines->elements / 3);
            glDisableClientState(GL_VERTEX_ARRAY);
        }
        
        {
            # draw axes
            $ground_z += 0.02;
            my $origin = $self->origin;
            my $axis_len = 2 * max(@{ $object_size });
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
            # draw line for Z axis
            glColor3f(0, 0, 1);
            glVertex3f(@$origin, $ground_z);
            glVertex3f(@$origin, $ground_z+$axis_len);
            glEnd();
        }
        
        # draw cutting plane
        if (defined $self->cutting_plane_z) {
            my $plane_z = $z0 + $self->cutting_plane_z;
            glDisable(GL_CULL_FACE);
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
    }
    
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
    glFlush();
 
    $self->SwapBuffers();
}

sub draw_mesh {
    my $self = shift;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    
    foreach my $volume (@{$self->volumes}) {
        glTranslatef(0, 0, -$volume->{z_min});
        
        glVertexPointer_p(3, $volume->{verts});
        
        glCullFace(GL_BACK);
        glNormalPointer_p($volume->{norms});
        if ($volume->{selected}) {
            glColor4f(@{ &SELECTED_COLOR });
        } else {
            glColor4f(@{ $volume->{color} });
        }
        glDrawArrays(GL_TRIANGLES, 0, $volume->{verts}->elements / 3);
        
        glTranslatef(0, 0, +$volume->{z_min});
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

1;
