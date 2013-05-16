package Slic3r::GUI::PreviewCanvas;
 
use strict;
 
use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_TIMER);
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions);
use base qw(Wx::GLCanvas Class::Accessor::Fast);
use Wx::GLCanvas qw(:all);
 
__PACKAGE__->mk_accessors( qw(timer x_rot y_rot dirty init mesh) );
 
sub new {
    my( $class, $parent, $mesh ) = @_;
    my $self = $class->SUPER::new($parent);
    $self->mesh($mesh);
    
    my $timer = $self->timer( Wx::Timer->new( $self ) );
    $timer->Start( 50 );
 
    $self->x_rot( 0 );
    $self->y_rot( 0 );
 
    EVT_PAINT( $self,
               sub {
                   my $dc = Wx::PaintDC->new( $self );
                   $self->Render( $dc );
               } );
    EVT_SIZE( $self, sub { $self->dirty( 1 ) } );
    EVT_IDLE( $self, sub {
                  return unless $self->dirty;
                  $self->Resize( $self->GetSizeWH );
                  $self->Refresh;
              } );
    EVT_TIMER( $self, -1, sub {
                   my( $self, $e ) = @_;
 
                   $self->x_rot( $self->x_rot - 1 );
                   $self->y_rot( $self->y_rot + 2 );
 
                   $self->dirty( 1 );
                   Wx::WakeUpIdle;
               } );
 
    return $self;
}
 
sub GetContext {
    my( $self ) = @_;
 
    if( Wx::wxVERSION >= 2.009 ) {
        return $self->{context} ||= Wx::GLContext->new( $self );
    } else {
        return $self->SUPER::GetContext;
    }
}
 
sub SetCurrent {
    my( $self, $context ) = @_;
 
    if( Wx::wxVERSION >= 2.009 ) {
        return $self->SUPER::SetCurrent( $context );
    } else {
        return $self->SUPER::SetCurrent;
    }
}
 
sub Resize {
    my( $self, $x, $y ) = @_;
 
    return unless $self->GetContext;
    $self->dirty( 0 );
 
    $self->SetCurrent( $self->GetContext );
    glViewport( 0, 0, $x, $y );
 
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    my_gluPerspective( 45, $x/$y, .5, 100 );
 
    glMatrixMode(GL_MODELVIEW);
}
 
use Math::Trig;
 
sub my_gluPerspective {
    my( $fov, $ratio, $near, $far ) = @_;
 
    my $top = tan(deg2rad($fov)*0.5) * $near;
    my $bottom = -$top;
    my $left = $ratio * $bottom;
    my $right = $ratio * $top;
 
    glFrustum( $left, $right, $bottom, $top, $near, $far );
}
 
sub DESTROY {
    my( $self ) = @_;
 
    $self->timer->Stop;
    $self->timer( undef );
}
 
package Slic3r::GUI::PreviewCanvas::Cube;
 
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions);
use base qw(Slic3r::GUI::PreviewCanvas);
use Slic3r::Geometry qw(X Y Z MIN MAX);
 
sub cube {
    my( @v ) = ( [ 1, 1, 1 ], [ -1, 1, 1 ],
                 [ -1, -1, 1 ], [ 1, -1, 1 ],
                 [ 1, 1, -1 ], [ -1, 1, -1 ],
                 [ -1, -1, -1 ], [ 1, -1, -1 ] );
    my( @c ) = ( [ 1, 1, 0 ], [ 1, 0, 1 ],
                 [ 0, 1, 1 ], [ 1, 1, 1 ],
                 [ 0, 0, 1 ], [ 0, 1, 0 ],
                 [ 1, 0, 1 ], [ 1, 1, 0 ] );
    my( @s ) = ( [ 0, 1, 2, 3 ], [ 4, 5, 6, 7 ],
                 [ 0, 1, 5, 4 ], [ 2, 3, 7, 6 ],
                 [ 1, 2, 6, 5 ], [ 0, 3, 7, 4 ] );
    
    for my $i ( 0 .. 5 ) {
        my $s = $s[$i];
        glBegin(GL_QUADS);
        foreach my $j ( @$s ) {
            glColor3f( @{$c[$j]} );
            glVertex3f( @{$v[$j]} );
        }
        glEnd();
    }
}

sub draw_mesh {
    my $self = shift;
    
    my $mesh = $self->mesh;
    
    #glEnable(GL_CULL_FACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    #glEnableClientState(GL_NORMAL_ARRAY);
    
    my @verts = map 0.1 * $_, map @{ $mesh->vertices->[$_] }, map @$_, @{$mesh->facets};
    my $verts = OpenGL::Array->new_list(GL_FLOAT, @verts);
    
    #my @norms = map @$_, map {my $f = $_; Slic3r::Geometry::triangle_normal(map $mesh->vertices->[$_], @$f) } @{$mesh->facets};
    #my $norms = OpenGL::Array->new_list(GL_FLOAT, @norms);
    
    #my @inv_norms = map @$_, map {my $f = $_; Slic3r::Geometry::triangle_normal(reverse map $mesh->vertices->[$_], @$f) } @{$mesh->facets};
    #my $inv_norms = OpenGL::Array->new_list(GL_FLOAT, @inv_norms);
    
    glVertexPointer_p(3, $verts);
    
    #glCullFace(GL_BACK);
    #glNormalPointer_p($norms);
    glDrawArrays(GL_TRIANGLES, 0, scalar @verts);
    
    #glCullFace(GL_FRONT);
    #glNormalPointer_p($inv_norms);
    #glDrawArrays(GL_TRIANGLES, 0, scalar @verts);
    
    #glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}
 
sub InitGL {
    my $self = shift;
 
    return if $self->init;
    return unless $self->GetContext;
    $self->init( 1 );
 
    $self->mesh->align_to_origin;
 
    glDisable( GL_LIGHTING );
    glDepthFunc( GL_LESS );
    glEnable( GL_DEPTH_TEST );
    
    if (0) {
        # Settings for our light.
        my @Light_Ambient  = ( 0.1, 0.1, 0.1, 1.0 );
        my @Light_Diffuse  = ( 1.2, 1.2, 1.2, 1.0 );
        my @Light_Position = ( 2.0, 2.0, 0.0, 1.0 );
    
        
            # Enables Smooth Color Shading; try GL_FLAT for (lack of) fun.
          glShadeModel(GL_SMOOTH);
        
          # Set up a light, turn it on.
          glLightfv_p(GL_LIGHT1, GL_POSITION, @Light_Position);
          glLightfv_p(GL_LIGHT1, GL_AMBIENT,  @Light_Ambient);
          glLightfv_p(GL_LIGHT1, GL_DIFFUSE,  @Light_Diffuse);
          glEnable(GL_LIGHT1);
          
          # A handy trick -- have surface material mirror the color.
        glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_COLOR_MATERIAL);
    }
}
 
sub Render {
    my( $self, $dc ) = @_;
 
    return unless $self->GetContext;
    $self->SetCurrent( $self->GetContext );
    $self->InitGL;
 
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
 
    glPushMatrix();
    glTranslatef( 0, 0, -5 );
    glRotatef( $self->x_rot, 1, 0, 0 );
    glRotatef( $self->y_rot, 0, 0, 1 );
 
    #cube();
    $self->draw_mesh; 

    glPopMatrix();
    glFlush();
 
    $self->SwapBuffers();
}

1;