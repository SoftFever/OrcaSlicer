package Slic3r::GUI::PreviewCanvas;
use strict;
use warnings;
 
use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions);
use base qw(Wx::GLCanvas Class::Accessor);
use Slic3r::Geometry qw(X Y Z MIN MAX triangle_normal normalize deg2rad tan);
use Wx::GLCanvas qw(:all);
 
__PACKAGE__->mk_accessors( qw(quat dirty init mesh_center
                            verts norms initpos) );
 
sub new {
    my ($class, $parent, $mesh) = @_;
    my $self = $class->SUPER::new($parent);
   
    $self->quat((0, 0, 0, 1));

    # prepare mesh
    {
        $self->mesh_center($mesh->center);
        
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

sub trackball {
  return (0, 0, 0, 1);
}

sub mulquat {
  my ($self, @q1, @rq) = @_;
  return ($q1[3] * $rq[0] + $q1[0] * $rq[3] + $q1[1] * $rq[2] - $q1[2] * $rq[1],
          $q1[3] * $rq[1] + $q1[1] * $rq[3] + $q1[2] * $rq[0] - $q1[0] * $rq[2],
          $q1[3] * $rq[2] + $q1[2] * $rq[3] + $q1[0] * $rq[1] - $q1[1] * $rq[0],
          $q1[3] * $rq[3] - $q1[0] * $rq[0] - $q1[1] * $rq[1] - $q1[2] * $rq[2])
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
    @quat = $self->trackball($orig->x / ($size->width / 2) - 1,
                             1 - $orig->y / ($size->height / 2),
                             $new->x / ($size->width / 2) - 1,
                             1 - $new->y / ($size->height / 2));
    $self->quat($self->mulquat($self->quat, @quat));
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
 
sub Resize {
    my ($self, $x, $y) = @_;
 
    return unless $self->GetContext;
    $self->dirty(0);
 
    $self->SetCurrent($self->GetContext);
    glViewport(0, 0, $x, $y);
 
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-$x/2, $x/2, -$y/2, $y/2, 0.5, 100);
 
    glMatrixMode(GL_MODELVIEW);
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
