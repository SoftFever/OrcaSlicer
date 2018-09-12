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
# must load OpenGL *before* Wx::GLCanvas
use OpenGL qw(:glconstants :glfunctions :glufunctions :gluconstants);
use base qw(Wx::GLCanvas Class::Accessor);
use Wx::GLCanvas qw(:all);

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

    Slic3r::GUI::_3DScene::add_canvas($self);
    Slic3r::GUI::_3DScene::allow_multisample($self, $can_multisample);
    
    return $self;
}

sub Destroy {
    my ($self) = @_;
    Slic3r::GUI::_3DScene::remove_canvas($self);    
    return $self->SUPER::Destroy;
}

# The 3D canvas to display objects and tool paths.
package Slic3r::GUI::3DScene;
use base qw(Slic3r::GUI::3DScene::Base);

sub new {
    my $class = shift;
    
    my $self = $class->SUPER::new(@_);    
    return $self;
}

1;
