package Slic3r::GUI::Plater::2DToolpaths;
use strict;
use warnings;
use utf8;

use List::Util qw();
use Slic3r::Geometry qw();
use Wx qw(:misc :sizer :slider :statictext);
use Wx::Event qw(EVT_SLIDER EVT_KEY_DOWN);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, $print) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition);
    
    # init print
    $self->{print} = $print;
    $self->reload_print;
    
    # init GUI elements
    my $canvas = $self->{canvas} = Slic3r::GUI::Plater::2DToolpaths::Canvas->new($self, $print);
    my $slider = $self->{slider} = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        scalar(@{$self->{layers_z}})-1,    # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    my $z_label = $self->{z_label} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label->SetFont($Slic3r::GUI::small_font);
    
    my $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider, 1, wxALL | wxEXPAND | wxALIGN_CENTER, 3);
    $vsizer->Add($z_label, 0, wxALL | wxEXPAND | wxALIGN_CENTER, 3);
    
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $sizer->Add($canvas, 1, wxALL | wxEXPAND, 0);
    $sizer->Add($vsizer, 0, wxTOP | wxBOTTOM | wxEXPAND, 5);
    
    EVT_SLIDER($self, $slider, sub {
        $self->set_z($self->{layers_z}[$slider->GetValue]);
    });
    EVT_KEY_DOWN($canvas, sub {
        my ($s, $event) = @_;
        
        my $key = $event->GetKeyCode;
        if ($key == 85 || $key == 315) {
            $slider->SetValue($slider->GetValue + 1);
            $self->set_z($self->{layers_z}[$slider->GetValue]);
        } elsif ($key == 68 || $key == 317) {
            $slider->SetValue($slider->GetValue - 1);
            $self->set_z($self->{layers_z}[$slider->GetValue]);
        }
    });
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    
    $self->set_z($self->{layers_z}[0]);
    
    return $self;
}

sub reload_print {
    my ($self) = @_;
    
    my %z = ();  # z => 1
    foreach my $object (@{$self->{print}->objects}) {
        foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
            $z{$layer->print_z} = 1;
        }
    }
    $self->{layers_z} = [ sort { $a <=> $b } keys %z ];
}

sub set_z {
    my ($self, $z) = @_;
    
    $self->{z_label}->SetLabel(sprintf '%.2f', $z);
    $self->{canvas}->set_z($z);
}


package Slic3r::GUI::Plater::2DToolpaths::Canvas;

use Wx::Event qw(EVT_PAINT EVT_SIZE EVT_ERASE_BACKGROUND EVT_IDLE EVT_MOUSEWHEEL EVT_MOUSE_EVENTS);
use OpenGL qw(:glconstants :glfunctions :glufunctions);
use base qw(Wx::GLCanvas Class::Accessor);
use Wx::GLCanvas qw(:all);
use List::Util qw(min first);
use Slic3r::Geometry qw(scale unscale epsilon);

__PACKAGE__->mk_accessors(qw(print z layers color init dirty bb));

# make OpenGL::Array thread-safe
{
    no warnings 'redefine';
    *OpenGL::Array::CLONE_SKIP = sub { 1 };
}

sub new {
    my ($class, $parent, $print) = @_;
    
    my $self = $class->SUPER::new($parent);
    $self->print($print);
    $self->bb($self->print->total_bounding_box);
    
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
    
    return $self;
}

sub set_z {
    my ($self, $z) = @_;
    
    my $print = $self->print;
    
    # can we have interlaced layers?
    my $interlaced = (defined first { $_->config->support_material } @{$print->objects})
        || (defined first { $_->config->infill_every_layers > 1 } @{$print->regions});
    
    my $max_layer_height = $print->max_layer_height;
    
    my @layers = ();
    foreach my $object (@{$print->objects}) {
        foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
            if ($interlaced) {
                push @layers, $layer
                    if $z > ($layer->print_z - $max_layer_height - epsilon)
                        && $z <= $layer->print_z + epsilon;
            } else {
                push @layers, $layer if abs($layer->print_z - $z) < epsilon;
            }
        }
    }
    
    $self->z($z);
    $self->layers([ @layers ]);
    $self->dirty(1);
}

sub Render {
    my ($self, $dc) = @_;
    
    # prevent calling SetCurrent() when window is not shown yet
    return unless $self->IsShownOnScreen;
    return unless my $context = $self->GetContext;
    $self->SetCurrent($context);
    $self->InitGL;
    
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    my $bb = $self->bb;
    my ($x1, $y1, $x2, $y2) = ($bb->x_min, $bb->y_min, $bb->x_max, $bb->y_max);
    my ($x, $y) = $self->GetSizeWH;
    if (($x2 - $x1)/($y2 - $y1) > $x/$y) {
        # adjust Y
        my $new_y = $y * ($x2 - $x1) / $x;
        $y1 = ($y2 + $y1)/2 - $new_y/2;
        $y2 = $y1 + $new_y;
    } else {
        my $new_x = $x * ($y2 - $y1) / $y;
        $x1 = ($x2 + $x1)/2 - $new_x/2;
        $x2 = $x1 + $new_x;
    }
    glOrtho($x1, $x2, $y1, $y2, 0, 1);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    glClearColor(1, 1, 1, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    
    my $skirt_drawn = 0;
    my $brim_drawn = 0;
    foreach my $layer (@{$self->layers}) {
        my $object = $layer->object;
        my $print_z = $layer->print_z;
        
        # draw brim
        if ($layer->id == 0 && !$brim_drawn) {
            $self->color([0, 0, 0]);
            $self->_draw(undef, $print_z, $_) for @{$self->print->brim};
            $brim_drawn = 1;
        }
        if (($self->print->config->skirt_height == -1 || $self->print->config->skirt_height >= $layer->id) && !$skirt_drawn) {
            $self->color([0, 0, 0]);
            $self->_draw(undef, $print_z, $_) for @{$self->print->skirt};
            $skirt_drawn = 1;
        }
        
        foreach my $layerm (@{$layer->regions}) {
            $self->color([0.7, 0, 0]);
            $self->_draw($object, $print_z, $_) for @{$layerm->perimeters};
            
            $self->color([0, 0, 0.7]);
            $self->_draw($object, $print_z, $_) for map @$_, @{$layerm->fills};
        }
        
        if ($layer->isa('Slic3r::Layer::Support')) {
            $self->color([0, 0, 0]);
            $self->_draw($object, $print_z, $_) for @{$layer->support_fills};
            $self->_draw($object, $print_z, $_) for @{$layer->support_interface_fills};
        }
    }
    
    glFlush();
    $self->SwapBuffers;
}

sub _draw {
    my ($self, $object, $print_z, $path) = @_;
    
    my @paths = $path->isa('Slic3r::ExtrusionLoop')
        ? @$path
        : ($path);
    
    $self->_draw_path($object, $print_z, $_) for @paths;
}

sub _draw_path {
    my ($self, $object, $print_z, $path) = @_;
    
    return if $print_z - $path->height > $self->z - epsilon;
    
    if (abs($print_z - $self->z) < epsilon) {
        glColor3f(@{$self->color});
    } else {
        glColor3f(0.8, 0.8, 0.8);
    }
    
    glLineWidth(1);
    
    if (defined $object) {
        foreach my $copy (@{ $object->_shifted_copies }) {
            foreach my $line (@{$path->polyline->lines}) {
                $line->translate(@$copy);
                glBegin(GL_LINES);
                glVertex2f(@{$line->a});
                glVertex2f(@{$line->b});
                glEnd();
            }
        }
    } else {
        foreach my $line (@{$path->polyline->lines}) {
            glBegin(GL_LINES);
            glVertex2f(@{$line->a});
            glVertex2f(@{$line->b});
            glEnd();
        }
    }
}

sub InitGL {
    my $self = shift;
 
    return if $self->init;
    return unless $self->GetContext;
    $self->init(1);
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
}

sub line {
    my (
        $x1, $y1, $x2, $y2,     # coordinates of the line
        $w,                     # width/thickness of the line in pixel
        $Cr, $Cg, $Cb,          # RGB color components
        $Br, $Bg, $Bb,          # color of background when alphablend=false
                                # Br=alpha of color when alphablend=true
        $alphablend,            # use alpha blend or not
    ) = @_;
    
    my $t;
    my $R;
    my $f = $w - int($w);
    my $A;
    
    if ($alphablend) {
        $A = $Br;
    } else {
        $A = 1;
    }
    
    # determine parameters t,R
    if ($w >= 0 && $w < 1) {
        $t = 0.05; $R = 0.48 + 0.32 * $f;
        if (!$alphablend) {
            $Cr += 0.88 * (1-$f);
            $Cg += 0.88 * (1-$f);
            $Cb += 0.88 * (1-$f);
            $Cr = 1.0 if ($Cr > 1.0);
            $Cg = 1.0 if ($Cg > 1.0);
            $Cb = 1.0 if ($Cb > 1.0);
        } else {
            $A *= $f;
        }
    } elsif ($w >= 1.0 && $w < 2.0) {
        $t = 0.05 + $f*0.33; $R = 0.768 + 0.312*$f;
    } elsif ($w >= 2.0 && $w < 3.0) {
        $t = 0.38 + $f*0.58; $R = 1.08;
    } elsif ($w >= 3.0 && $w < 4.0) {
        $t = 0.96 + $f*0.48; $R = 1.08;
    } elsif ($w >= 4.0 && $w < 5.0) {
        $t= 1.44 + $f*0.46; $R = 1.08;
    } elsif ($w >= 5.0 && $w < 6.0) {
        $t= 1.9 + $f*0.6; $R = 1.08;
    } elsif ($w >= 6.0) {
        my $ff = $w - 6.0;
        $t = 2.5 + $ff*0.50; $R = 1.08;
    }
    #printf( "w=%f, f=%f, C=%.4f\n", $w, $f, $C);
    
    # determine angle of the line to horizontal
    my $tx = 0; my $ty = 0; # core thinkness of a line
    my $Rx = 0; my $Ry = 0; # fading edge of a line
    my $cx = 0; my $cy = 0; # cap of a line
    my $ALW = 0.01;
    my $dx = $x2 - $x1;
    my $dy = $y2 - $y1;
    if (abs($dx) < $ALW) {
        # vertical
        $tx = $t; $ty = 0;
        $Rx = $R; $Ry = 0;
        if ($w > 0.0 && $w < 1.0) {
            $tx *= 8;
        } elsif ($w == 1.0) {
            $tx *= 10;
        }
    } elsif (abs($dy) < $ALW) {
        #horizontal
        $tx = 0; $ty = $t;
        $Rx = 0; $Ry = $R;
        if ($w > 0.0 && $w < 1.0) {
            $ty *= 8;
        } elsif ($w == 1.0) {
            $ty *= 10;
        }
    } else {
        if ($w < 3) { # approximate to make things even faster
            my $m = $dy/$dx;
            # and calculate tx,ty,Rx,Ry
            if ($m > -0.4142 && $m <= 0.4142) {
                # -22.5 < $angle <= 22.5, approximate to 0 (degree)
                $tx = $t * 0.1; $ty = $t;
                $Rx = $R * 0.6; $Ry = $R;
            } elsif ($m > 0.4142 && $m <= 2.4142) {
                # 22.5 < $angle <= 67.5, approximate to 45 (degree)
                $tx = $t * -0.7071; $ty = $t * 0.7071;
                $Rx = $R * -0.7071; $Ry = $R * 0.7071;
            } elsif ($m > 2.4142 || $m <= -2.4142) {
                # 67.5 < $angle <= 112.5, approximate to 90 (degree)
                $tx = $t; $ty = $t*0.1;
                $Rx = $R; $Ry = $R*0.6;
            } elsif ($m > -2.4142 && $m < -0.4142) {
                # 112.5 < angle < 157.5, approximate to 135 (degree)
                $tx = $t * 0.7071; $ty = $t * 0.7071;
                $Rx = $R * 0.7071; $Ry = $R * 0.7071;
            } else {
                # error in determining angle
                printf("error in determining angle: m=%.4f\n", $m);
            }
        } else {  # calculate to exact
            $dx= $y1 - $y2;
            $dy= $x2 - $x1;
            my $L = sqrt($dx*$dx + $dy*$dy);
            $dx /= $L;
            $dy /= $L;
            $cx = -0.6*$dy; $cy=0.6*$dx;
            $tx = $t*$dx; $ty = $t*$dy;
            $Rx = $R*$dx; $Ry = $R*$dy;
        }
    }

    # draw the line by triangle strip
    glBegin(GL_TRIANGLE_STRIP);
    if (!$alphablend) {
        glColor3f($Br, $Bg, $Bb);
    } else {
        glColor4f($Cr, $Cg, $Cb, 0);
    }
    glVertex2f($x1 - $tx - $Rx, $y1 - $ty - $Ry);   # fading edge
    glVertex2f($x2 - $tx - $Rx, $y2 - $ty - $Ry);
    
    if (!$alphablend) {
        glColor3f($Cr, $Cg, $Cb);
    } else {
        glColor4f($Cr, $Cg, $Cb, $A);
    }
    glVertex2f($x1 - $tx, $y1 - $ty); # core
    glVertex2f($x2 - $tx, $y2 - $ty);
    glVertex2f($x1 + $tx, $y1 + $ty);
    glVertex2f($x2 + $tx, $y2 + $ty);
    
    if ((abs($dx) < $ALW || abs($dy) < $ALW) && $w <= 1.0) {
        # printf("skipped one fading edge\n");
    } else {
        if (!$alphablend) {
            glColor3f($Br, $Bg, $Bb);
        } else {
            glColor4f($Cr, $Cg, $Cb, 0);
        }
        glVertex2f($x1 + $tx+ $Rx, $y1 + $ty + $Ry);    # fading edge
        glVertex2f($x2 + $tx+ $Rx, $y2 + $ty + $Ry);
    }
    glEnd();

    # cap
    if ($w < 3) {
        # do not draw cap
    } else {
        # draw cap
        glBegin(GL_TRIANGLE_STRIP);
        if (!$alphablend) {
            glColor3f($Br, $Bg, $Bb);
        } else {
            glColor4f($Cr, $Cg, $Cb, 0);
        }
        glVertex2f($x1 - $Rx + $cx, $y1 - $Ry + $cy);
        glVertex2f($x1 + $Rx + $cx, $y1 + $Ry + $cy);
        glColor3f($Cr, $Cg, $Cb);
        glVertex2f($x1 - $tx - $Rx, $y1 - $ty - $Ry);
        glVertex2f($x1 + $tx + $Rx, $y1 + $ty + $Ry);
        glEnd();
        glBegin(GL_TRIANGLE_STRIP);
        if (!$alphablend) {
            glColor3f($Br, $Bg, $Bb);
        } else {
            glColor4f($Cr, $Cg, $Cb, 0);
        }
        glVertex2f($x2 - $Rx - $cx, $y2 - $Ry - $cy);
        glVertex2f($x2 + $Rx - $cx, $y2 + $Ry - $cy);
        glColor3f($Cr, $Cg, $Cb);
        glVertex2f($x2 - $tx - $Rx, $y2 - $ty - $Ry);
        glVertex2f($x2 + $tx + $Rx, $y2 + $ty + $Ry);
        glEnd();
    }
}


package Slic3r::GUI::Plater::2DToolpaths::Dialog;

use Wx qw(:dialog :id :misc :sizer);
use Wx::Event qw(EVT_CLOSE);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, $print) = @_;
    my $self = $class->SUPER::new($parent, -1, "Toolpaths", wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add(Slic3r::GUI::Plater::2DToolpaths->new($self, $print), 1, wxEXPAND, 0);
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    # needed to actually free memory
    EVT_CLOSE($self, sub {
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    return $self;
}

1;
