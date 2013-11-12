package Slic3r::GUI::PreviewCanvas;
use strict;
use warnings;

use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions);
use base qw(Wx::GLCanvas Class::Accessor);
use Math::Trig qw(asin);
use List::Util qw(reduce min max first);
use Slic3r::Geometry qw(X Y Z MIN MAX triangle_normal normalize deg2rad tan);
use Wx::GLCanvas qw(:all);
 
__PACKAGE__->mk_accessors( qw(quat dirty init mview_init
                              object_center object_size
                              volumes initpos
                              sphi stheta) );

use constant TRACKBALLSIZE => 0.8;
use constant TURNTABLE_MODE => 1;
use constant COLORS => [ [1,1,1], [1,0.5,0.5], [0.5,1,0.5], [0.5,0.5,1] ];

sub new {
    my ($class, $parent, $object) = @_;
    my $self = $class->SUPER::new($parent);
   
    $self->quat((0, 0, 0, 1));
    $self->sphi(45);
    $self->stheta(-45);

    $object->align_to_origin;
    $self->object_center($object->center);
    $self->object_size($object->size);
    
    # group mesh(es) by material
    my @materials = ();
    $self->volumes([]);
    foreach my $volume (@{$object->volumes}) {
        my $mesh = $volume->mesh;
        $mesh->repair;
        
        my $material_id = $volume->material_id // '_';
        my $color_idx = first { $materials[$_] eq $material_id } 0..$#materials;
        if (!defined $color_idx) {
            push @materials, $material_id;
            $color_idx = $#materials;
        }
        push @{$self->volumes}, my $v = {
            color => COLORS->[ $color_idx % scalar(@{&COLORS}) ],
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
    
    EVT_PAINT($self, sub {
        my $dc = Wx::PaintDC->new($self);
        $self->Render($dc);
    });
    EVT_SIZE($self, sub { $self->dirty(1) });
    EVT_IDLE($self, sub {
        return unless $self->dirty;
        return if !$self->IsShownOnScreen;
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
    
    glTranslatef($tox, $toy, 0);
    glMatrixMode(GL_MODELVIEW);
    $self->Zoom($factor);
    glTranslatef(-$tox, -$toy, 0);
}

sub Zoom {
    my ($self, $factor) =  @_;
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
    my $ratio = $factor * min($win_size->width, $win_size->height) / max(@{ $self->object_size });
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
    my $object_size = $self->object_size;
    glOrtho(-$x/2, $x/2, -$y/2, $y/2, 0.5, 2 * max(@$object_size));
 
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
    
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    
    # Settings for our light.
    my @LightPos        = (0, 0, 2, 1.0);
    my @LightAmbient    = (0.1, 0.1, 0.1, 1.0);
    my @LightDiffuse    = (0.7, 0.5, 0.5, 1.0);
    my @LightSpecular   = (0.1, 0.1, 0.1, 0.1);
    
    # Enables Smooth Color Shading; try GL_FLAT for (lack of) fun.
    glShadeModel(GL_SMOOTH);
    
    # Set up a light, turn it on.
    glLightfv_p(GL_LIGHT1, GL_POSITION, @LightPos);
    glLightfv_p(GL_LIGHT1, GL_AMBIENT,  @LightAmbient);
    glLightfv_p(GL_LIGHT1, GL_DIFFUSE,  @LightDiffuse);
    glLightfv_p(GL_LIGHT1, GL_SPECULAR, @LightSpecular);
    glEnable(GL_LIGHT1);
      
    # A handy trick -- have surface material mirror the color.
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
}
 
sub Render {
    my ($self, $dc) = @_;
 
    return unless $self->GetContext;
    $self->SetCurrent($self->GetContext);
    $self->InitGL;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPushMatrix();

    my $object_size = $self->object_size;
    glTranslatef(0, 0, -max(@$object_size[0..1]));
    my @rotmat = quat_to_rotmatrix($self->quat);
    glMultMatrixd_p(@rotmat[0..15]);
    glRotatef($self->stheta, 1, 0, 0);
    glRotatef($self->sphi, 0, 0, 1);
    glTranslatef(map -$_, @{ $self->object_center });

    $self->draw_mesh;
    
    # draw axes
    {
        my $axis_len = 2 * max(@{ $self->object_size });
        glLineWidth(2);
        glBegin(GL_LINES);
        # draw line for x axis
        glColor3f(1, 0, 0);
        glVertex3f(0, 0, 0);
        glVertex3f($axis_len, 0, 0);
        # draw line for y axis
        glColor3f(0, 1, 0);
        glVertex3f(0, 0, 0);
        glVertex3f(0, $axis_len, 0);
        # draw line for Z axis
        glColor3f(0, 0, 1);
        glVertex3f(0, 0, 0);
        glVertex3f(0, 0, $axis_len);
        glEnd();
        
        # draw ground
        my $ground_z = -0.02;
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
	    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
        glColor4f(1, 1, 1, 0.5);
        glVertex3f(-$axis_len, -$axis_len, $ground_z);
        glVertex3f($axis_len, -$axis_len, $ground_z);
        glVertex3f($axis_len, $axis_len, $ground_z);
        glVertex3f(-$axis_len, $axis_len, $ground_z);
        glEnd();
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        
        # draw grid
        glBegin(GL_LINES);
        glColor3f(1, 1, 1);
        for (my $x = -$axis_len; $x <= $axis_len; $x += 10) {
            glVertex3f($x, -$axis_len, $ground_z);
            glVertex3f($x, $axis_len, $ground_z);
        }
        for (my $y = -$axis_len; $y <= $axis_len; $y += 10) {
            glVertex3f(-$axis_len, $y, $ground_z);
            glVertex3f($axis_len, $y, $ground_z);
        }
        glEnd();
    }

    glPopMatrix();
    glFlush();
 
    $self->SwapBuffers();
}

sub draw_mesh {
    my $self = shift;
    
    glEnable(GL_CULL_FACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    
    foreach my $volume (@{$self->volumes}) {
        glVertexPointer_p(3, $volume->{verts});
        
        glCullFace(GL_BACK);
        glNormalPointer_p($volume->{norms});
        glColor3f(@{ $volume->{color} });
        glDrawArrays(GL_TRIANGLES, 0, $volume->{verts}->elements / 3);
    }
    
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

1;
