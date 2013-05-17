package Slic3r::GUI::PreviewCanvas;
use strict;
use warnings;
 
use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_TIMER EVT_MOUSEWHEEL);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions);
use base qw(Wx::GLCanvas Class::Accessor);
use Slic3r::Geometry qw(X Y Z MIN MAX triangle_normal normalize deg2rad tan);
use Wx::GLCanvas qw(:all);
 
__PACKAGE__->mk_accessors( qw(timer x_rot y_rot dirty init mesh_center zoom
                            verts norms) );
 
sub new {
    my ($class, $parent, $mesh) = @_;
    my $self = $class->SUPER::new($parent);
    
    # prepare mesh
    {
        $self->mesh_center($mesh->center);
        $self->zoom(0.1);
        
        my @verts = map $self->zoom * $_, map @{ $mesh->vertices->[$_] }, map @$_, @{$mesh->facets};
        $self->verts(OpenGL::Array->new_list(GL_FLOAT, @verts));
        
        my @norms = map { @$_, @$_, @$_ } map normalize(triangle_normal(map $mesh->vertices->[$_], @$_)), @{$mesh->facets};
        $self->norms(OpenGL::Array->new_list(GL_FLOAT, @norms));
    }
    
    my $timer = $self->timer( Wx::Timer->new($self) );
    $timer->Start(50);
    
    $self->x_rot(0);
    $self->y_rot(0);
    
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
    EVT_TIMER($self, -1, sub {
        my ($self, $e) = @_;
        
        $self->x_rot( $self->x_rot - 1 );
        $self->y_rot( $self->y_rot + 2 );
        
        $self->dirty(1);
        Wx::WakeUpIdle;
    });
    EVT_MOUSEWHEEL($self, sub {
        my ($self, $e) = @_;
        
        my $zoom = $self->zoom * (1.0 - $e->GetWheelRotation() / $e->GetWheelDelta() / 10);
        $zoom = 0.001 if $zoom < 0.001;
        $zoom = 0.1 if $zoom > 0.1;
        $self->zoom($zoom);
        
        $self->Refresh;
    });
    
    return $self;
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
    my_gluPerspective(45, $x/$y, .5, 100);
 
    glMatrixMode(GL_MODELVIEW);
}
 
sub my_gluPerspective {
    my ($fov, $ratio, $near, $far) = @_;
 
    my $top = tan(deg2rad($fov)*0.5) * $near;
    my $bottom = -$top;
    my $left = $ratio * $bottom;
    my $right = $ratio * $top;
 
    glFrustum( $left, $right, $bottom, $top, $near, $far );
}
 
sub DESTROY {
    my $self = shift;
 
    $self->timer->Stop;
    $self->timer(undef);
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
    glTranslatef( 0, 0, -5 );
    
    # this needs to get a lot better...
    glRotatef( $self->x_rot, 1, 0, 0 );
    glRotatef( $self->y_rot, 0, 0, 1 );
    glTranslatef(map -$_ * $self->zoom, @{ $self->mesh_center });
 
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
