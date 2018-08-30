package Slic3r::GUI::Plater::3DPreview;
use strict;
use warnings;
use utf8;

use Slic3r::Print::State ':steps';
use Wx qw(:misc :sizer :slider :statictext :keycode wxWHITE wxCB_READONLY);
use Wx::Event qw(EVT_SLIDER EVT_KEY_DOWN EVT_CHECKBOX EVT_CHOICE EVT_CHECKLISTBOX);
use base qw(Wx::Panel Class::Accessor);

use Wx::Locale gettext => 'L';

__PACKAGE__->mk_accessors(qw(print gcode_preview_data enabled _loaded canvas slider_low slider_high single_layer));

sub new {
    my $class = shift;
    my ($parent, $print, $gcode_preview_data, $config) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition);
    $self->{config} = $config;
    $self->{number_extruders} = 1;
    # Show by feature type by default.
    $self->{preferred_color_mode} = 'feature';

    # init GUI elements
    my $canvas = Slic3r::GUI::3DScene->new($self);
    Slic3r::GUI::_3DScene::enable_shader($canvas, 1);
    Slic3r::GUI::_3DScene::set_config($canvas, $config);
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

    my $z_label_low_idx = $self->{z_label_low_idx} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_low_idx->SetFont($Slic3r::GUI::small_font);
    my $z_label_high_idx = $self->{z_label_high_idx} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_high_idx->SetFont($Slic3r::GUI::small_font);
        
    $self->single_layer(0);
    my $checkbox_singlelayer = $self->{checkbox_singlelayer} = Wx::CheckBox->new($self, -1, L("1 Layer"));
    
    my $label_view_type = $self->{label_view_type} = Wx::StaticText->new($self, -1, L("View"));
    
    my $choice_view_type = $self->{choice_view_type} = Wx::Choice->new($self, -1);
    $choice_view_type->Append(L("Feature type"));
    $choice_view_type->Append(L("Height"));
    $choice_view_type->Append(L("Width"));
    $choice_view_type->Append(L("Speed"));
    $choice_view_type->Append(L("Volumetric flow rate"));
    $choice_view_type->Append(L("Tool"));
    $choice_view_type->SetSelection(0);

    # the following value needs to be changed if new items are added into $choice_view_type before "Tool"
    $self->{tool_idx} = 5;
    
    my $label_show_features = $self->{label_show_features} = Wx::StaticText->new($self, -1, L("Show"));
    
    my $combochecklist_features = $self->{combochecklist_features} = Wx::ComboCtrl->new();
    $combochecklist_features->Create($self, -1, L("Feature types"), wxDefaultPosition, [200, -1], wxCB_READONLY);
    my $feature_text = L("Feature types");
    my $feature_items = L("Perimeter")."|"
                        .L("External perimeter")."|"
                        .L("Overhang perimeter")."|"
                        .L("Internal infill")."|"
                        .L("Solid infill")."|"
                        .L("Top solid infill")."|"
                        .L("Bridge infill")."|"
                        .L("Gap fill")."|"
                        .L("Skirt")."|"
                        .L("Support material")."|"
                        .L("Support material interface")."|"
                        .L("Wipe tower")."|"
                        .L("Custom");
    Slic3r::GUI::create_combochecklist($combochecklist_features, $feature_text, $feature_items, 1);
    
    my $checkbox_travel         = $self->{checkbox_travel}          = Wx::CheckBox->new($self, -1, L("Travel"));
    my $checkbox_retractions    = $self->{checkbox_retractions}     = Wx::CheckBox->new($self, -1, L("Retractions"));    
    my $checkbox_unretractions  = $self->{checkbox_unretractions}   = Wx::CheckBox->new($self, -1, L("Unretractions"));
    my $checkbox_shells         = $self->{checkbox_shells}          = Wx::CheckBox->new($self, -1, L("Shells"));

    my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
    my $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    my $vsizer_outer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_low, 3, wxALIGN_CENTER_HORIZONTAL, 0);
    $vsizer->Add($z_label_low_idx, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    $vsizer->Add($z_label_low, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_high, 3, wxALIGN_CENTER_HORIZONTAL, 0);
    $vsizer->Add($z_label_high_idx, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    $vsizer->Add($z_label_high, 0, 0, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    $vsizer_outer->Add($hsizer, 3, wxALIGN_CENTER_HORIZONTAL, 0);
    $vsizer_outer->Add($checkbox_singlelayer, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, 5);

    my $bottom_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $bottom_sizer->Add($label_view_type, 0, wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->Add($choice_view_type, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($label_show_features, 0, wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->Add($combochecklist_features, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(20);
    $bottom_sizer->Add($checkbox_travel, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($checkbox_retractions, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($checkbox_unretractions, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    $bottom_sizer->AddSpacer(10);
    $bottom_sizer->Add($checkbox_shells, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $sizer->Add($canvas, 1, wxALL | wxEXPAND, 0);
    $sizer->Add($vsizer_outer, 0, wxTOP | wxBOTTOM | wxEXPAND, 5);

    my $main_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $main_sizer->Add($sizer, 1, wxALL | wxEXPAND, 0);
    $main_sizer->Add($bottom_sizer, 0, wxALL | wxEXPAND, 0); 
    
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
    EVT_CHOICE($self, $choice_view_type, sub {
        my $selection = $choice_view_type->GetCurrentSelection();
        $self->{preferred_color_mode} = ($selection == $self->{tool_idx}) ? 'tool' : 'feature';
        $self->gcode_preview_data->set_type($selection);
        $self->reload_print;
    });
    EVT_CHECKLISTBOX($self, $combochecklist_features, sub {
        my $flags = Slic3r::GUI::combochecklist_get_flags($combochecklist_features);
        
        $self->gcode_preview_data->set_extrusion_flags($flags);
        $self->refresh_print;
    });    
    EVT_CHECKBOX($self, $checkbox_travel, sub {
        $self->gcode_preview_data->set_travel_visible($checkbox_travel->IsChecked());
        $self->refresh_print;
    });    
    EVT_CHECKBOX($self, $checkbox_retractions, sub {
        $self->gcode_preview_data->set_retractions_visible($checkbox_retractions->IsChecked());
        $self->refresh_print;
    });
    EVT_CHECKBOX($self, $checkbox_unretractions, sub {
        $self->gcode_preview_data->set_unretractions_visible($checkbox_unretractions->IsChecked());
        $self->refresh_print;
    });
    EVT_CHECKBOX($self, $checkbox_shells, sub {
        $self->gcode_preview_data->set_shells_visible($checkbox_shells->IsChecked());
        $self->refresh_print;
    });
    
    $self->SetSizer($main_sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    
    # init canvas
    $self->print($print);
    $self->gcode_preview_data($gcode_preview_data);
    
    # sets colors for gcode preview extrusion roles
    my @extrusion_roles_colors = (
                                    'Perimeter'                  => 'FFFF66',
                                    'External perimeter'         => 'FFA500',
                                    'Overhang perimeter'         => '0000FF',
                                    'Internal infill'            => 'B1302A',
                                    'Solid infill'               => 'D732D7',
                                    'Top solid infill'           => 'FF1A1A',
                                    'Bridge infill'              => '9999FF',
                                    'Gap fill'                   => 'FFFFFF',
                                    'Skirt'                      => '845321',
                                    'Support material'           => '00FF00',
                                    'Support material interface' => '008000',
                                    'Wipe tower'                 => 'B3E3AB',
                                    'Custom'                     => '28CC94',
                                 );
    $self->gcode_preview_data->set_extrusion_paths_colors(\@extrusion_roles_colors);
    
    $self->show_hide_ui_elements('none');
    $self->reload_print;
    
    return $self;
}

sub reload_print {
    my ($self, $force) = @_;

    Slic3r::GUI::_3DScene::reset_volumes($self->canvas);
    $self->_loaded(0);

    if (! $self->IsShown && ! $force) {
#        $self->{reload_delayed} = 1;
        return;
    }

    $self->load_print;
}

sub refresh_print {
    my ($self) = @_;

    $self->_loaded(0);
    
    if (! $self->IsShown) {
        return;
    }

    $self->load_print;
}

sub reset_gcode_preview_data {
    my ($self) = @_;
    $self->gcode_preview_data->reset;
    Slic3r::GUI::_3DScene::reset_legend_texture();
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
        $self->reset_sliders;
        Slic3r::GUI::_3DScene::reset_legend_texture();
        $self->canvas->Refresh;  # clears canvas
        return;
    }
    
    if ($self->{preferred_color_mode} eq 'tool_or_feature') {
        # It is left to Slic3r to decide whether the print shall be colored by the tool or by the feature.
        # Color by feature if it is a single extruder print.
        my $extruders = $self->{print}->extruders;
        my $type = (scalar(@{$extruders}) > 1) ? $self->{tool_idx} : 0;
        $self->gcode_preview_data->set_type($type);
        $self->{choice_view_type}->SetSelection($type);
        # If the ->SetSelection changed the following line, revert it to "decide yourself".
        $self->{preferred_color_mode} = 'tool_or_feature';
    }

    # Collect colors per extruder.
    my @colors = ();
    if (! $self->gcode_preview_data->empty() || $self->gcode_preview_data->type == $self->{tool_idx}) {
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
        # used to set the sliders to the extremes of the current zs range
        $self->{force_sliders_full_range} = 0;

        if ($self->gcode_preview_data->empty) {
            # load skirt and brim
            Slic3r::GUI::_3DScene::set_print($self->canvas, $self->print);
            Slic3r::GUI::_3DScene::load_preview($self->canvas, \@colors);
            $self->show_hide_ui_elements('simple');
        } else {
            $self->{force_sliders_full_range} = (Slic3r::GUI::_3DScene::get_volumes_count($self->canvas) == 0);
            Slic3r::GUI::_3DScene::set_print($self->canvas, $self->print);
            Slic3r::GUI::_3DScene::load_gcode_preview($self->canvas, $self->gcode_preview_data, \@colors);
            $self->show_hide_ui_elements('full');

            # recalculates zs and update sliders accordingly
            $self->{layers_z} = Slic3r::GUI::_3DScene::get_current_print_zs($self->canvas, 1);
            $n_layers = scalar(@{$self->{layers_z}});            
            if ($n_layers == 0) {
                # all layers filtered out
                $self->reset_sliders;
                $self->canvas->Refresh;  # clears canvas
            }
       }

        $self->update_sliders($n_layers) if ($n_layers > 0);
        $self->_loaded(1);
    }
}

sub reset_sliders {
    my ($self) = @_;
    $self->enabled(0);
    $self->set_z_range(0,0);
    $self->slider_low->Hide;
    $self->slider_high->Hide;
    $self->{z_label_low}->SetLabel("");
    $self->{z_label_high}->SetLabel("");
    $self->{z_label_low_idx}->SetLabel("");
    $self->{z_label_high_idx}->SetLabel("");
}

sub update_sliders
{
    my ($self, $n_layers) = @_;
        
    my $z_idx_low = $self->slider_low->GetValue;
    my $z_idx_high = $self->slider_high->GetValue;
    $self->enabled(1);
    $self->slider_low->SetRange(0, $n_layers - 1);
    $self->slider_high->SetRange(0, $n_layers - 1);
    
    if ($self->{force_sliders_full_range}) {
        $z_idx_low = 0;
        $z_idx_high = $n_layers - 1;
    } elsif ($z_idx_high < $n_layers && ($self->single_layer || $z_idx_high != 0)) {
        # search new indices for nearest z (size of $self->{layers_z} may change in dependence of what is shown)
        if (defined($self->{z_low})) {
            for (my $i = scalar(@{$self->{layers_z}}) - 1; $i >= 0; $i -= 1) {
                if ($self->{layers_z}[$i] <= $self->{z_low}) {
                    $z_idx_low = $i;
                    last;
                }
            }
        }
        if (defined($self->{z_high})) {
            for (my $i = scalar(@{$self->{layers_z}}) - 1; $i >= 0; $i -= 1) {
                if ($self->{layers_z}[$i] <= $self->{z_high}) {
                    $z_idx_high = $i;
                    last;
                }
            }
        }
    } elsif ($z_idx_high >= $n_layers) {
        # Out of range. Disable 'single layer' view.
        $self->single_layer(0);
        $self->{checkbox_singlelayer}->SetValue(0);
        $z_idx_low = 0;
        $z_idx_high = $n_layers - 1;
    } else {
        $z_idx_low = 0;
        $z_idx_high = $n_layers - 1;
    }
    
    $self->slider_low->SetValue($z_idx_low);
    $self->slider_high->SetValue($z_idx_high);
    $self->slider_low->Show;
    $self->slider_high->Show;
    $self->set_z_range($self->{layers_z}[$z_idx_low], $self->{layers_z}[$z_idx_high]);
    $self->Layout;    
}

sub set_z_range
{
    my ($self, $z_low, $z_high) = @_;
    
    return if !$self->enabled;
    $self->{z_low} = $z_low;
    $self->{z_high} = $z_high;
    $self->{z_label_low}->SetLabel(sprintf '%.2f', $z_low);
    $self->{z_label_high}->SetLabel(sprintf '%.2f', $z_high);
    
    my $layers_z = Slic3r::GUI::_3DScene::get_current_print_zs($self->canvas, 0);
    for (my $i = 0; $i < scalar(@{$layers_z}); $i += 1) {
        if (($z_low - 1e-6 < @{$layers_z}[$i]) && (@{$layers_z}[$i] < $z_low + 1e-6)) {
            $self->{z_label_low_idx}->SetLabel(sprintf '%d', $i + 1);
            last;
        }
    }
    for (my $i = 0; $i < scalar(@{$layers_z}); $i += 1) {
        if (($z_high - 1e-6 < @{$layers_z}[$i]) && (@{$layers_z}[$i] < $z_high + 1e-6)) {
            $self->{z_label_high_idx}->SetLabel(sprintf '%d', $i + 1);
            last;
        }
    }

    Slic3r::GUI::_3DScene::set_toolpaths_range($self->canvas, $z_low - 1e-6, $z_high + 1e-6);
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

sub set_number_extruders {
    my ($self, $number_extruders) = @_;
    if ($self->{number_extruders} != $number_extruders) {
        $self->{number_extruders} = $number_extruders;
        my $type = ($number_extruders > 1) ?
              $self->{tool_idx}  # color by a tool number
            : 0; # color by a feature type
        $self->{choice_view_type}->SetSelection($type);
        $self->gcode_preview_data->set_type($type);
        $self->{preferred_color_mode} = ($type == $self->{tool_idx}) ? 'tool_or_feature' : 'feature';
    }
}

sub show_hide_ui_elements {
    my ($self, $what) = @_;
    my $method = ($what eq 'full') ? 'Enable' : 'Disable';
    $self->{$_}->$method for qw(label_show_features combochecklist_features checkbox_travel checkbox_retractions checkbox_unretractions checkbox_shells);
    $method = ($what eq 'none') ? 'Disable' : 'Enable';
    $self->{$_}->$method for qw(label_view_type choice_view_type);
}

# Called by the Platter wxNotebook when this page is activated.
sub OnActivate {
#    my ($self) = @_;
#    $self->reload_print(1) if ($self->{reload_delayed});
}

1;
