package Slic3r::GUI::Plater::3DPreview;
use strict;
use warnings;
use utf8;

use Slic3r::Print::State ':steps';
use Wx qw(:misc :sizer :slider :statictext :keycode wxWHITE);
use Wx::Event qw(EVT_SLIDER EVT_KEY_DOWN EVT_CHECKBOX);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(print enabled _loaded canvas slider_low slider_high single_layer));

sub new {
    my $class = shift;
    my ($parent, $print, $config) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition);
    $self->{config} = $config;
    $self->{number_extruders} = 1;
    $self->{preferred_color_mode} = 'feature';

    # init GUI elements
    my $canvas = Slic3r::GUI::3DScene->new($self);
    $canvas->use_plain_shader(1);
    $self->canvas($canvas);
    my $slider_low = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        # we set max to a bogus non-zero value because the MSW implementation of wxSlider
        # will skip drawing the slider if max <= min:
        1,                              # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    $self->slider_low($slider_low);
    my $slider_high = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        # we set max to a bogus non-zero value because the MSW implementation of wxSlider
        # will skip drawing the slider if max <= min:
        1,                              # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    $self->slider_high($slider_high);
    
    my $z_label_low = $self->{z_label_low} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_low->SetFont($Slic3r::GUI::small_font);
    my $z_label_high = $self->{z_label_high} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_high->SetFont($Slic3r::GUI::small_font);

    $self->single_layer(0);
    $self->{color_by_extruder} = 0;
    my $checkbox_singlelayer = $self->{checkbox_singlelayer} = Wx::CheckBox->new($self, -1, "1 Layer");
    my $checkbox_color_by_extruder = $self->{checkbox_color_by_extruder} = Wx::CheckBox->new($self, -1, "Tool");
    
    my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
    my $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    my $vsizer_outer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_low, 3, 0, 0);
    $vsizer->Add($z_label_low, 0, 0, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_high, 3, 0, 0);
    $vsizer->Add($z_label_high, 0, 0, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    $vsizer_outer->Add($hsizer, 3, wxALIGN_CENTER_HORIZONTAL, 0);
    $vsizer_outer->Add($checkbox_singlelayer, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, 5);
    $vsizer_outer->Add($checkbox_color_by_extruder, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, 5);

    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $sizer->Add($canvas, 1, wxALL | wxEXPAND, 0);
    $sizer->Add($vsizer_outer, 0, wxTOP | wxBOTTOM | wxEXPAND, 5);
    
    EVT_SLIDER($self, $slider_low,  sub {
        $slider_high->SetValue($slider_low->GetValue) if $self->single_layer;
        $self->set_z_idx_low ($slider_low ->GetValue)
    });
    EVT_SLIDER($self, $slider_high, sub { 
        $slider_low->SetValue($slider_high->GetValue) if $self->single_layer;
        $self->set_z_idx_high($slider_high->GetValue) 
    });
    EVT_KEY_DOWN($canvas, sub {
        my ($s, $event) = @_;
        my $key = $event->GetKeyCode;
        if ($event->HasModifiers) {
            $event->Skip;
        } else {
            if ($key == ord('U')) {
                $slider_high->SetValue($slider_high->GetValue + 1);
                $slider_low->SetValue($slider_high->GetValue) if ($event->ShiftDown());
                $self->set_z_idx_high($slider_high->GetValue);
            } elsif ($key == ord('D')) {
                $slider_high->SetValue($slider_high->GetValue - 1);
                $slider_low->SetValue($slider_high->GetValue) if ($event->ShiftDown());
                $self->set_z_idx_high($slider_high->GetValue);
            } elsif ($key == ord('S')) {
                $checkbox_singlelayer->SetValue(! $checkbox_singlelayer->GetValue());
                $self->single_layer($checkbox_singlelayer->GetValue());
                if ($self->single_layer) {
                    $slider_low->SetValue($slider_high->GetValue);
                    $self->set_z_idx_high($slider_high->GetValue);
                }
            } else {
                $event->Skip;
            }
        }
    });
    EVT_KEY_DOWN($slider_low, sub {
        my ($s, $event) = @_;
        my $key = $event->GetKeyCode;
        if ($event->HasModifiers) {
            $event->Skip;
        } else {
            if ($key == WXK_LEFT) {
            } elsif ($key == WXK_RIGHT) {
                $slider_high->SetFocus;
            } else {
                $event->Skip;
            }
        }
    });
    EVT_KEY_DOWN($slider_high, sub {
        my ($s, $event) = @_;
        my $key = $event->GetKeyCode;
        if ($event->HasModifiers) {
            $event->Skip;
        } else {
            if ($key == WXK_LEFT) {
                $slider_low->SetFocus;
            } elsif ($key == WXK_RIGHT) {
            } else {
                $event->Skip;
            }
        }
    });
    EVT_CHECKBOX($self, $checkbox_singlelayer, sub {
        $self->single_layer($checkbox_singlelayer->GetValue());
        if ($self->single_layer) {
            $slider_low->SetValue($slider_high->GetValue);
            $self->set_z_idx_high($slider_high->GetValue);
        }
    });
    EVT_CHECKBOX($self, $checkbox_color_by_extruder, sub {
        $self->{color_by_extruder} = $checkbox_color_by_extruder->GetValue();
        $self->{preferred_color_mode} = $self->{color_by_extruder} ? 'tool' : 'feature';
        $self->reload_print;
    });
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    
    # init canvas
    $self->print($print);
    $self->reload_print;
    
    return $self;
}

sub reload_print {
    my ($self, $force) = @_;
    
    $self->canvas->reset_objects;
    $self->_loaded(0);

    if (! $self->IsShown && ! $force) {
        $self->{reload_delayed} = 1;
        return;
    }

    $self->load_print;
}

sub load_print {
    my ($self) = @_;
    
    return if $self->_loaded;
    
    # we require that there's at least one object and the posSlice step
    # is performed on all of them (this ensures that _shifted_copies was
    # populated and we know the number of layers)
    my $n_layers = 0;
    if ($self->print->object_step_done(STEP_SLICE)) {
        my %z = ();  # z => 1
        foreach my $object (@{$self->{print}->objects}) {
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                $z{$layer->print_z} = 1;
            }
        }
        $self->{layers_z} = [ sort { $a <=> $b } keys %z ];
        $n_layers = scalar(@{$self->{layers_z}});
    }

    if ($n_layers == 0) {
        $self->enabled(0);
        $self->set_z_range(0,0);
        $self->slider_low->Hide;
        $self->slider_high->Hide;
        $self->canvas->Refresh;  # clears canvas
        return;
    }
    
    my $z_idx_low = $self->slider_low->GetValue;
    my $z_idx_high = $self->slider_high->GetValue;
    $self->enabled(1);
    $self->slider_low->SetRange(0, $n_layers - 1);
    $self->slider_high->SetRange(0, $n_layers - 1);
    if ($z_idx_high < $n_layers && ($self->single_layer || $z_idx_high != 0)) {
        # use $z_idx
    } else {
        # Out of range. Disable 'single layer' view.
        $self->single_layer(0);
        $self->{checkbox_singlelayer}->SetValue(0);
        $z_idx_low = 0;
        $z_idx_high = $n_layers - 1;
    }
    if ($self->single_layer) {
        $z_idx_low = $z_idx_high;
    } elsif ($z_idx_low > $z_idx_high) {
        $z_idx_low = 0;
    }
    $self->slider_low->SetValue($z_idx_low);
    $self->slider_high->SetValue($z_idx_high);
    $self->slider_low->Show;
    $self->slider_high->Show;
    $self->Layout;

    my $by_tool = $self->{color_by_extruder};
    if ($self->{preferred_color_mode} eq 'tool_or_feature') {
        # It is left to Slic3r to decide whether the print shall be colored by the tool or by the feature.
        # Color by feature if it is a single extruder print.
        my $extruders = $self->{print}->extruders;
        $by_tool = scalar(@{$extruders}) > 1;
        $self->{color_by_extruder} = $by_tool;
        $self->{checkbox_color_by_extruder}->SetValue($by_tool);
        $self->{preferred_color_mode} = 'tool_or_feature';
    }

    # Collect colors per extruder.
    # Leave it empty, if the print should be colored by a feature.
    my @colors = ();
    if ($by_tool) {
        my @extruder_colors = @{$self->{config}->extruder_colour};
        my @filament_colors = @{$self->{config}->filament_colour};
        for (my $i = 0; $i <= $#extruder_colors; $i += 1) {
            my $color = $extruder_colors[$i];
            $color = $filament_colors[$i] if (! defined($color) || $color !~ m/^#[[:xdigit:]]{6}/);
            $color = '#FFFFFF' if (! defined($color) || $color !~ m/^#[[:xdigit:]]{6}/);
            push @colors, $color;
        }
    }

    if ($self->IsShown) {
        # load skirt and brim
        $self->canvas->load_print_toolpaths($self->print, \@colors);
        $self->canvas->load_wipe_tower_toolpaths($self->print, \@colors);
        
        foreach my $object (@{$self->print->objects}) {
            $self->canvas->load_print_object_toolpaths($object, \@colors);
            
            # Show the objects in very transparent color.
            #my @volume_ids = $self->canvas->load_object($object->model_object);
            #$self->canvas->volumes->[$_]->color->[3] = 0.2 for @volume_ids;
        }
        $self->canvas->zoom_to_volumes;
        $self->_loaded(1);
    }
    
    $self->set_z_range($self->{layers_z}[$z_idx_low], $self->{layers_z}[$z_idx_high]);
}

sub set_z_range
{
    my ($self, $z_low, $z_high) = @_;
    
    return if !$self->enabled;
    $self->{z_label_low}->SetLabel(sprintf '%.2f', $z_low);
    $self->{z_label_high}->SetLabel(sprintf '%.2f', $z_high);
    $self->canvas->set_toolpaths_range($z_low - 1e-6, $z_high + 1e-6);
    $self->canvas->Refresh if $self->IsShown;
}

sub set_z_idx_low
{
    my ($self, $idx_low) = @_;
    if ($self->enabled) {
        my $idx_high = $self->slider_high->GetValue;
        if ($idx_low >= $idx_high) {
            $idx_high = $idx_low;
            $self->slider_high->SetValue($idx_high);
        }
        $self->set_z_range($self->{layers_z}[$idx_low], $self->{layers_z}[$idx_high]);
    }
}

sub set_z_idx_high
{
    my ($self, $idx_high) = @_;
    if ($self->enabled) {
        my $idx_low  = $self->slider_low->GetValue;
        if ($idx_low > $idx_high) {
            $idx_low = $idx_high;
            $self->slider_low->SetValue($idx_low);
        }
        $self->set_z_range($self->{layers_z}[$idx_low], $self->{layers_z}[$idx_high]);
    }
}

sub set_bed_shape {
    my ($self, $bed_shape) = @_;
    $self->canvas->set_bed_shape($bed_shape);
}

sub set_number_extruders {
    my ($self, $number_extruders) = @_;
    if ($self->{number_extruders} != $number_extruders) {
        $self->{number_extruders} = $number_extruders;
        my $by_tool = $number_extruders > 1;
        $self->{color_by_extruder} = $by_tool;
        $self->{checkbox_color_by_extruder}->SetValue($by_tool);
        $self->{preferred_color_mode} = $by_tool ? 'tool_or_feature' : 'feature';
    }
}

# Called by the Platter wxNotebook when this page is activated.
sub OnActivate {
    my ($self) = @_;
    $self->reload_print(1) if ($self->{reload_delayed});
}

1;
