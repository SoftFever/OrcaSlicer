package Slic3r::GUI::PreviewCanvas;
use strict;
use warnings;

use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions);
use base qw(Wx::GLCanvas Class::Accessor);
use Math::Trig qw(asin);
use List::Util qw(reduce min max);
use Slic3r::Geometry qw(X Y Z MIN MAX triangle_normal normalize deg2rad tan);
use Wx::GLCanvas qw(:all);
 
__PACKAGE__->mk_accessors( qw(quat dirty init mview_init
                              mesh_center mesh_size
                              verts norms initpos) );
 
sub new {
    my ($class, $parent, $mesh) = @_;
    my $self = $class->SUPER::new($parent);
   
    $self->quat((0, 0, 0, 1));

    # prepare mesh
    {
        $self->mesh_center($mesh->center);
        $self->mesh_size($mesh->size);
        
        my @verts = map @{ $mesh->vertices->[$_] }, map @$_, @{$mesh->facets};
        $self->verts(OpenGL::Array->new_list(GL_FLOAT, @verts));
        
        my @norms = map { @$_, @$_, @$_ } map normalize(triangle_normal(map $mesh->vertices->[$_], @$_)), @{$mesh->facets};
        $self->norms(OpenGL::Array->new_list(GL_FLOAT, @norms));
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

sub axis_to_quat {
  my ($ax, $phi) = @_;
  my $lena = sqrt(reduce { $a + $b } (map { $_ * $_ } @$ax));
  my @q = map { $_ * (1 / $lena) } @$ax;
  @q = map { $_ * sin($phi / 2.0) } @q;
  $q[$#q + 1] = cos($phi / 2.0);
  return @q;
}

sub project_to_sphere {
  my ($r, $x, $y) = @_;
  my $d = sqrt($x * $x + $y * $y);
  if ($d < $r * 0.70710678118654752440) {
        return sqrt($r * $r - $d * $d);
  } else {
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

sub trackball {
  my ($p1x, $p1y, $p2x, $p2y, $r) = @_;

    if ($p1x == $p2x && $p1y == $p2y) {
      return (0.0, 0.0, 0.0, 1.0);
    }

    my @p1 = ($p1x, $p1y, project_to_sphere($r, $p1x, $p1y));
    my @p2 = ($p2x, $p2y, project_to_sphere($r, $p2x, $p2y));
    my @a = cross(\@p2, \@p1);

    my @d = map { $_ * $_ } (map { $p1[$_] - $p2[$_] } 0 .. $#p1);
    my $t = sqrt(reduce { $a + $b } @d) / (2.0 * $r);

    $t = 1.0 if ($t > 1.0);
    $t = -1.0 if ($t < -1.0);
    my $phi = 2.0 * asin($t);

    return axis_to_quat(\@a, $phi);
}

sub quat_to_rotmatrix {
  my ($q) = @_;
  my @m;
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
    my ($orig, $new, $size, @quat);
    $orig = $self->initpos;
    $new = $e->GetPosition();
    $size = $self->GetClientSize();
    @quat = trackball($orig->x / ($size->width / 2) - 1,
                      1 - $orig->y / ($size->height / 2),
                      $new->x / ($size->width / 2) - 1,
                      1 - $new->y / ($size->height / 2),
                      0.8);
    $self->quat(mulquats($self->quat, \@quat));
    $self->initpos($new);
    $self->Refresh;
  }
}

sub handle_translation {
  my ($self, $e) = @_;

  if (not defined $self->initpos) {
    $self->initpos($e->GetPosition());
  } else {
    my ($orig, @orig3d, $new, @new3d);
    $new = $e->GetPosition();
    $orig = $self->initpos;
    @orig3d = $self->mouse_to_3d($orig->x, $orig->y);
    @new3d = $self->mouse_to_3d($new->x, $new->y);
    glTranslatef($new3d[0] - $orig3d[0], $new3d[1] - $orig3d[1], 0);
    $self->initpos($new);
    $self->Refresh;
  }
}

sub mouse_to_3d {
  my ($self, $x, $y) = @_;

  my @viewport = glGetIntegerv_p(GL_VIEWPORT);
  my @mview = glGetDoublev_p(GL_MODELVIEW_MATRIX);
  my @proj = glGetDoublev_p(GL_PROJECTION_MATRIX);

  my @projected = gluUnProject_p($x, $viewport[3] - $y, 1.0,
                                 $mview[0], $mview[1], $mview[2], $mview[3],
                                 $mview[4], $mview[5], $mview[6], $mview[7],
                                 $mview[8], $mview[9], $mview[10], $mview[11],
                                 $mview[12], $mview[13], $mview[14], $mview[15],
                                 $proj[0], $proj[1], $proj[2], $proj[3],
                                 $proj[4], $proj[5], $proj[6], $proj[7],
                                 $proj[8], $proj[9], $proj[10], $proj[11],
                                 $proj[12], $proj[13], $proj[14], $proj[15],
                                 $viewport[0], $viewport[1], $viewport[2], $viewport[3]);

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
    my $mesh_size = $self->mesh_size;
    my $win_size = $self->GetClientSize();
    my $ratio = $factor * min($win_size->width, $win_size->height) / max(@$mesh_size);
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
    my $mesh_size = $self->mesh_size;
    glOrtho(-$x/2, $x/2, -$y/2, $y/2, 0.5, 2 * max(@$mesh_size));
 
    glMatrixMode(GL_MODELVIEW);
    unless ($self->mview_init) {
      $self->mview_init(1);
      $self->ResetModelView(0.9);
    }
}
 
sub DESTROY {
    my $self = shift;
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

    my $mesh_size = $self->mesh_size;
    glTranslatef(0, 0, -max(@$mesh_size[0..1]));
    my @rotmat = quat_to_rotmatrix($self->quat);
    glMultMatrixd_p($rotmat[0], $rotmat[1], $rotmat[2], $rotmat[3],
                    $rotmat[4], $rotmat[5], $rotmat[6], $rotmat[7],
                    $rotmat[8], $rotmat[9], $rotmat[10], $rotmat[11],
                    $rotmat[12], $rotmat[13], $rotmat[14], $rotmat[15]);
    glTranslatef(map -$_, @{ $self->mesh_center });

    $self->draw_mesh;

    glPopMatrix();
    glFlush();
 
    $self->SwapBuffers();
}

sub draw_mesh {
    my $self = shift;
    
    glEnable(GL_CULL_FACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    
    glVertexPointer_p(3, $self->verts);
    
    glCullFace(GL_BACK);
    glNormalPointer_p($self->norms);
    glDrawArrays(GL_TRIANGLES, 0, $self->verts->elements / 3);
    
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

1;
