############################################################
#
# Stripped down from the Perl OpenGL::Shader package by Vojtech Bubnik
# to only support the GLSL shaders. The original source was not maintained
# and did not install properly through the CPAN archive, and it was unnecessary
# complex.
#
# Original copyright:
#
# Copyright 2007 Graphcomp - ALL RIGHTS RESERVED
# Author: Bob "grafman" Free - grafman@graphcomp.com
#
# This program is free software; you can redistribute it and/or
# modify it under the same terms as Perl itself.
#
############################################################

package Slic3r::GUI::GLShader;
use OpenGL(':all');

# Avoid cloning this class by the worker threads.
sub CLONE_SKIP { 1 }

# Shader constructor
sub new
{
    # Check for required OpenGL extensions
    my $error_message = '';
    my $extensions_valid = 1;
    foreach my $i (map "GL_ARB_$_", qw(shader_objects fragment_shader vertex_shader shading_language_100)) {
        if (OpenGL::glpCheckExtension($i)) {
            $error_message .= "Missing OpenGL extension: $i\n";
            $extensions_valid = 0;
        }
    }

    if (! $extensions_valid) {
        # Cannot create a shader object, because some of the necessary OpenGL extensions are missing.
        # Form an error message.
        my $gl_version       = glGetString(GL_VERSION);
        my $gl_vendor        = glGetString(GL_VENDOR);
        my $gl_renderer      = glGetString(GL_RENDERER);
        my $glsl_version_ARB = glGetString(GL_SHADING_LANGUAGE_VERSION_ARB) // '';
        my $glsl_version     = glGetString(GL_SHADING_LANGUAGE_VERSION) // $glsl_version_ARB;
        $glsl_version .= 'ARB(' . $glsl_version_ARB . ')' if ($glsl_version_ARB ne '' && $glsl_version ne $glsl_version_ARB);
        my $out = '';
        if ($^O eq 'MSWin32') {
            if ($gl_vendor eq 'Microsoft Corporation' && $renderer eq 'GDI Generic') {
                $out .= "Windows is using a software OpenGL renderer.\n";
                $out .= "You are either connected over remote desktop,\n";
                $out .= "or a hardware acceleration is not available.\n";
            }
        }
        $out .= "GL version:   ${gl_version}\n";
        $out .= "vendor:       ${gl_vendor}\n";
        $out .= "renderer:     ${gl_renderer}\n";
        $out .= "GLSL version: ${glsl_version}\n";
        $error_message = $out . $error_message;
        return $error_message;
    }

    my $this = shift;
    my $class = ref($this) || $this;
    my($type) = @_;
    my $self = {type => uc($type)};
    bless($self, $class);

    # Get GL_SHADING_LANGUAGE_VERSION_ARB
    my $shader_ver = glGetString(0x8B8C);
    $shader_ver =~ m|([\d\.]+)|;
    $self->{version} = $1 || '0';
#    print "Shader version: $self->{version}\n";
    return $self;
}

# Shader destructor
# Must be disabled first
sub DESTROY
{
    my($self) = @_;

    if ($self->{program}) {
        glDetachObjectARB($self->{program},$self->{fragment_id}) if ($self->{fragment_id});
        glDetachObjectARB($self->{program},$self->{vertex_id}) if ($self->{vertex_id});
        glDeleteProgramsARB_p($self->{program});
    }

    glDeleteProgramsARB_p($self->{fragment_id}) if ($self->{fragment_id});
    glDeleteProgramsARB_p($self->{vertex_id}) if ($self->{vertex_id});
}

# Load shader strings
sub Load
{
    my($self, $fragment, $vertex) = @_;

    # Load fragment code
    if ($fragment) {
        $self->{fragment_id} = glCreateShaderObjectARB(GL_FRAGMENT_SHADER);
        return "glCreateShaderObjectARB failed." if (!$self->{fragment_id});
        glShaderSourceARB_p($self->{fragment_id}, $fragment);
        glCompileShaderARB($self->{fragment_id});
        my $stat = glGetInfoLogARB_p($self->{fragment_id});
        return "Fragment shader compilation failed:\n$stat" if ($stat);
    }

    # Load vertex code
    if ($vertex) {
        $self->{vertex_id} = glCreateShaderObjectARB(GL_VERTEX_SHADER);
        return "glCreateShaderObjectARB failed." if (!$self->{vertex_id});
        glShaderSourceARB_p($self->{vertex_id}, $vertex);
        glCompileShaderARB($self->{vertex_id});
        $stat = glGetInfoLogARB_p($self->{vertex_id});
        return "Vertex shader compilation failed:\n$stat" if ($stat);
    }

    # Link shaders
    my $sp = glCreateProgramObjectARB();
    return "glCreateProgramObjectARB failed." if (!sp);
    glAttachObjectARB($sp, $self->{fragment_id}) if ($fragment);
    glAttachObjectARB($sp, $self->{vertex_id}) if ($vertex);
    glLinkProgramARB($sp);
    my $linked = glGetObjectParameterivARB_p($sp, GL_OBJECT_LINK_STATUS_ARB);
    if (!$linked) {
        $stat = glGetInfoLogARB_p($sp);
        return "Shader linking failed:\n$stat" if ($stat);
        return 'Unable to link the shader.';
    }

    $self->{program} = $sp;
    return undef;
}

# Enable shader
sub Enable
{
    my($self) = @_;
    glUseProgramObjectARB($self->{program}) if ($self->{program});
}

# Disable shader
sub Disable
{
    my($self) = @_;
    glUseProgramObjectARB(0) if ($self->{program});
}

# Return shader vertex attribute ID
sub MapAttr
{
    my($self,$attr) = @_;
    return undef if (!$self->{program});
    my $id = glGetAttribLocationARB_p($self->{program},$attr);
    return undef if ($id < 0);
    return $id;
}

# Return shader uniform variable ID
sub Map
{
    my($self,$var) = @_;
    return undef if (!$self->{program});
    my $id = glGetUniformLocationARB_p($self->{program},$var);
    return undef if ($id < 0);
    return $id;
}

# Set shader vector
sub SetVector
{
    my($self,$var,@values) = @_;

    my $id = $self->Map($var);
    return 'Unable to map $var' if (!defined($id));

    my $count = scalar(@values);
    eval('glUniform'.$count.'fARB($id,@values)');

    return '';
}

# Set shader 4x4 matrix
sub SetMatrix
{
    my($self,$var,$oga) = @_;

    my $id = $self->Map($var);
    return 'Unable to map $var' if (!defined($id));

    glUniformMatrix4fvARB_c($id,1,0,$oga->ptr());
    return '';
}

1;
__END__
