package Slic3r::GUI::Tab;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use List::Util qw(first);
use Wx qw(:bookctrl :dialog :keycode :icon :id :misc :panel :sizer :treectrl :window);
use Wx::Event qw(EVT_BUTTON EVT_CHOICE EVT_KEY_DOWN EVT_TREE_SEL_CHANGED);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    $self->{options} = []; # array of option names handled by this tab
    $self->{$_} = $params{$_} for qw(plater on_value_change);
    
    # horizontal sizer
    $self->{sizer} = Wx::BoxSizer->new(wxHORIZONTAL);
    $self->{sizer}->SetSizeHints($self);
    $self->SetSizer($self->{sizer});
    
    # left vertical sizer
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $self->{sizer}->Add($left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);
    
    my $left_col_width = 150;
    
    # preset chooser
    {
        
        # choice menu
        $self->{presets_choice} = Wx::Choice->new($self, -1, wxDefaultPosition, [$left_col_width, -1], []);
        $self->{presets_choice}->SetFont($Slic3r::GUI::small_font);
        
        # buttons
        $self->{btn_save_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/disk.png", wxBITMAP_TYPE_PNG));
        $self->{btn_delete_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG));
        $self->{btn_save_preset}->SetToolTipString("Save current " . lc($self->title));
        $self->{btn_delete_preset}->SetToolTipString("Delete this preset");
        $self->{btn_delete_preset}->Disable;
        
        ### These cause GTK warnings:
        ###my $box = Wx::StaticBox->new($self, -1, "Presets:", wxDefaultPosition, [$left_col_width, 50]);
        ###my $hsizer = Wx::StaticBoxSizer->new($box, wxHORIZONTAL);
        
        my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
        
        $left_sizer->Add($hsizer, 0, wxEXPAND | wxBOTTOM, 5);
        $hsizer->Add($self->{presets_choice}, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 3);
        $hsizer->Add($self->{btn_save_preset}, 0, wxALIGN_CENTER_VERTICAL);
        $hsizer->Add($self->{btn_delete_preset}, 0, wxALIGN_CENTER_VERTICAL);
    }
    
    # tree
    $self->{treectrl} = Wx::TreeCtrl->new($self, -1, wxDefaultPosition, [$left_col_width, -1], wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
    $left_sizer->Add($self->{treectrl}, 1, wxEXPAND);
    $self->{icons} = Wx::ImageList->new(16, 16, 1);
    $self->{treectrl}->AssignImageList($self->{icons});
    $self->{iconcount} = -1;
    $self->{treectrl}->AddRoot("root");
    $self->{pages} = [];
    $self->{treectrl}->SetIndent(0);
    EVT_TREE_SEL_CHANGED($parent, $self->{treectrl}, sub {
        my $page = first { $_->{title} eq $self->{treectrl}->GetItemText($self->{treectrl}->GetSelection) } @{$self->{pages}}
            or return;
        $_->Hide for @{$self->{pages}};
        $page->Show;
        $self->{sizer}->Layout;
        $self->Refresh;
    });
    EVT_KEY_DOWN($self->{treectrl}, sub {
        my ($treectrl, $event) = @_;
        if ($event->GetKeyCode == WXK_TAB) {
            $treectrl->Navigate($event->ShiftDown ? &Wx::wxNavigateBackward : &Wx::wxNavigateForward);
        } else {
            $event->Skip;
        }
    });
    
    EVT_CHOICE($parent, $self->{presets_choice}, sub {
        $self->on_select_preset;
        $self->sync_presets;
    });
    
    EVT_BUTTON($self, $self->{btn_save_preset}, sub {
        
        # since buttons (and choices too) don't get focus on Mac, we set focus manually
        # to the treectrl so that the EVT_* events are fired for the input field having
        # focus currently. is there anything better than this?
        $self->{treectrl}->SetFocus;
        
        my $preset = $self->current_preset;
        my $default_name = $preset->{default} ? 'Untitled' : basename($preset->{name});
        $default_name =~ s/\.ini$//i;
        
        my $dlg = Slic3r::GUI::SavePresetWindow->new($self,
            title   => lc($self->title),
            default => $default_name,
            values  => [ map { my $name = $_->{name}; $name =~ s/\.ini$//i; $name } @{$self->{presets}} ],
        );
        return unless $dlg->ShowModal == wxID_OK;
        
        my $file = sprintf "$Slic3r::GUI::datadir/%s/%s.ini", $self->name, $dlg->get_name;
        $self->config->save($file);
        $self->set_dirty(0);
        $self->load_presets;
        $self->{presets_choice}->SetSelection(first { basename($self->{presets}[$_]{file}) eq $dlg->get_name . ".ini" } 1 .. $#{$self->{presets}});
        $self->on_select_preset;
        $self->sync_presets;
    });
    
    EVT_BUTTON($self, $self->{btn_delete_preset}, sub {
        my $i = $self->{presets_choice}->GetSelection;
        return if $i == 0;  # this shouldn't happen but let's trap it anyway
        my $res = Wx::MessageDialog->new($self, "Are you sure you want to delete the selected preset?", 'Delete Preset', wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION)->ShowModal;
        return unless $res == wxID_YES;
        if (-e $self->{presets}[$i]{file}) {
            unlink $self->{presets}[$i]{file};
        }
        splice @{$self->{presets}}, $i, 1;
        $self->set_dirty(0);
        $self->{presets_choice}->Delete($i);
        $self->{presets_choice}->SetSelection(0);
        $self->on_select_preset;
        $self->sync_presets;
    });
    
    $self->{config} = Slic3r::Config->new;
    $self->build;
    if ($self->hidden_options) {
        $self->{config}->apply(Slic3r::Config->new_from_defaults($self->hidden_options));
        push @{$self->{options}}, $self->hidden_options;
    }
    $self->load_presets;
    
    return $self;
}

sub current_preset {
    my $self = shift;
    return $self->{presets}[ $self->{presets_choice}->GetSelection ];
}

sub get_preset {
    my $self = shift;
    return $self->{presets}[ $_[0] ];
}

# propagate event to the parent
sub on_value_change {
    my $self = shift;
    $self->{on_value_change}->(@_) if $self->{on_value_change};
}

sub on_preset_loaded {}
sub hidden_options {}
sub config { $_[0]->{config}->clone }

sub select_default_preset {
    my $self = shift;
    $self->{presets_choice}->SetSelection(0);
}

sub select_preset {
    my $self = shift;
    $self->{presets_choice}->SetSelection($_[0]);
    $self->on_select_preset;
}

sub on_select_preset {
    my $self = shift;
    
    if (defined $self->{dirty}) {
        my $name = $self->{dirty} == 0 ? 'Default preset' : "Preset \"$self->{presets}[$self->{dirty}]{name}\"";
        my $confirm = Wx::MessageDialog->new($self, "$name has unsaved changes. Discard changes and continue anyway?",
                                             'Unsaved Changes', wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        if ($confirm->ShowModal == wxID_NO) {
            $self->{presets_choice}->SetSelection($self->{dirty});
            return;
        }
        $self->set_dirty(0);
    }
    
    my $preset = $self->current_preset;
    my $preset_config = $self->get_preset_config($preset);
    eval {
        local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
        foreach my $opt_key (@{$self->{options}}) {
            $self->{config}->set($opt_key, $preset_config->get($opt_key))
                if $preset_config->has($opt_key);
        }
    };
    Slic3r::GUI::catch_error($self);
    ($preset->{default} || $preset->{external})
        ? $self->{btn_delete_preset}->Disable
        : $self->{btn_delete_preset}->Enable;
    
    $self->on_preset_loaded;
    $self->reload_values;
    $self->set_dirty(0);
    $Slic3r::GUI::Settings->{presets}{$self->name} = $preset->{file} ? basename($preset->{file}) : '';
    Slic3r::GUI->save_settings;
}

sub get_preset_config {
    my $self = shift;
    my ($preset) = @_;
    
    if ($preset->{default}) {
        return Slic3r::Config->new_from_defaults(@{$self->{options}});
    } else {
        if (!-e $preset->{file}) {
            Slic3r::GUI::show_error($self, "The selected preset does not exist anymore ($preset->{file}).");
            return;
        }
        
        # apply preset values on top of defaults
        my $external_config = Slic3r::Config->load($preset->{file});
        my $config = Slic3r::Config->new;
        $config->set($_, $external_config->get($_))
            for grep $external_config->has($_), @{$self->{options}};
        
        return $config;
    }
}

sub add_options_page {
    my $self = shift;
    my ($title, $icon, %params) = @_;
    
    if ($icon) {
        my $bitmap = Wx::Bitmap->new("$Slic3r::var/$icon", wxBITMAP_TYPE_PNG);
        $self->{icons}->Add($bitmap);
        $self->{iconcount}++;
    }
    
    {
        # get all config options being added to the current page; remove indexes; associate defaults
        my @options = map { $_ =~ s/#.+//; $_ } grep !ref($_), map @{$_->{options}}, @{$params{optgroups}};
        my %defaults_to_set = map { $_ => 1 } @options;
        
        # apply default values for the options we don't have already
        delete $defaults_to_set{$_} for @{$self->{options}};
        $self->{config}->apply(Slic3r::Config->new_from_defaults(keys %defaults_to_set)) if %defaults_to_set;
        
        # append such options to our list
        push @{$self->{options}}, @options;
    }
    
    my $page = Slic3r::GUI::Tab::Page->new($self, $title, $self->{iconcount}, %params, on_change => sub {
        $self->on_value_change(@_);
        $self->set_dirty(1);
        $self->sync_presets;
    });
    $page->Hide;
    $self->{sizer}->Add($page, 1, wxEXPAND | wxLEFT, 5);
    push @{$self->{pages}}, $page;
    $self->update_tree;
    return $page;
}

sub set_value {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    my $changed = 0;
    foreach my $page (@{$self->{pages}}) {
        $changed = 1 if $page->set_value($opt_key, $value);
    }
    return $changed;
}

sub reload_values {
    my $self = shift;
    
    $self->set_value($_, $self->{config}->get($_)) for keys %{$self->{config}};
}

sub update_tree {
    my $self = shift;
    my ($select) = @_;
    
    $select //= 0; #/
    
    my $rootItem = $self->{treectrl}->GetRootItem;
    $self->{treectrl}->DeleteChildren($rootItem);
    foreach my $page (@{$self->{pages}}) {
        my $itemId = $self->{treectrl}->AppendItem($rootItem, $page->{title}, $page->{iconID});
        $self->{treectrl}->SelectItem($itemId) if $self->{treectrl}->GetChildrenCount($rootItem) == $select + 1;
    }
}

sub set_dirty {
    my $self = shift;
    my ($dirty) = @_;
    
    my $selection = $self->{presets_choice}->GetSelection;
    my $i = $self->{dirty} // $selection; #/
    my $text = $self->{presets_choice}->GetString($i);
    
    if ($dirty) {
        $self->{dirty} = $i;
        if ($text !~ / \(modified\)$/) {
            $self->{presets_choice}->SetString($i, "$text (modified)");
            $self->{presets_choice}->SetSelection($selection);  # http://trac.wxwidgets.org/ticket/13769
        }
    } else {
        $self->{dirty} = undef;
        $text =~ s/ \(modified\)$//;
        $self->{presets_choice}->SetString($i, $text);
        $self->{presets_choice}->SetSelection($selection);  # http://trac.wxwidgets.org/ticket/13769
    }
    $self->sync_presets;
}

sub is_dirty {
    my $self = shift;
    return (defined $self->{dirty});
}

sub load_presets {
    my $self = shift;
    
    $self->{presets} = [{
        default => 1,
        name    => '- default -',
    }];
    
    opendir my $dh, "$Slic3r::GUI::datadir/" . $self->name or die "Failed to read directory $Slic3r::GUI::datadir/" . $self->name . " (errno: $!)\n";
    foreach my $file (sort grep /\.ini$/i, readdir $dh) {
        my $name = basename($file);
        $name =~ s/\.ini$//;
        push @{$self->{presets}}, {
            file => "$Slic3r::GUI::datadir/" . $self->name . "/$file",
            name => $name,
        };
    }
    closedir $dh;
    
    $self->{presets_choice}->Clear;
    $self->{presets_choice}->Append($_->{name}) for @{$self->{presets}};
    {
        # load last used preset
        my $i = first { basename($self->{presets}[$_]{file}) eq ($Slic3r::GUI::Settings->{presets}{$self->name} || '') } 1 .. $#{$self->{presets}};
        $self->{presets_choice}->SetSelection($i || 0);
        $self->on_select_preset;
    }
    $self->sync_presets;
}

sub load_external_config {
    my $self = shift;
    my ($file) = @_;
    
    # look for the loaded config among the existing menu items
    my $i = first { $self->{presets}[$_]{file} eq $file && $self->{presets}[$_]{external} } 1..$#{$self->{presets}};
    if (!$i) {
        my $preset_name = basename($file);  # keep the .ini suffix
        push @{$self->{presets}}, {
            file        => $file,
            name        => $preset_name,
            external    => 1,
        };
        $self->{presets_choice}->Append($preset_name);
        $i = $#{$self->{presets}};
    }
    $self->{presets_choice}->SetSelection($i);
    $self->on_select_preset;
    $self->sync_presets;
}

sub sync_presets {
    my $self = shift;
    $self->{plater}->update_presets($self->name, [$self->{presets_choice}->GetStrings], $self->{presets_choice}->GetSelection);
}

package Slic3r::GUI::Tab::Print;
use base 'Slic3r::GUI::Tab';

sub name { 'print' }
sub title { 'Print Settings' }

sub build {
    my $self = shift;
    
    $self->add_options_page('Layers and perimeters', 'layers.png', optgroups => [
        {
            title => 'Layer height',
            options => [qw(layer_height first_layer_height)],
        },
        {
            title => 'Vertical shells',
            options => [qw(perimeters randomize_start extra_perimeters)],
        },
        {
            title => 'Horizontal shells',
            options => [qw(top_solid_layers bottom_solid_layers)],
            lines => [
                {
                    label   => 'Solid layers',
                    options => [qw(top_solid_layers bottom_solid_layers)],
                },
            ],
        },
    ]);
    
    $self->add_options_page('Infill', 'shading.png', optgroups => [
        {
            title => 'Infill',
            options => [qw(fill_density fill_pattern solid_fill_pattern)],
        },
        {
            title => 'Advanced',
            options => [qw(infill_every_layers solid_infill_every_layers fill_angle solid_infill_below_area only_retract_when_crossing_perimeters)],
        },
    ]);
    
    $self->add_options_page('Speed', 'time.png', optgroups => [
        {
            title => 'Speed for print moves',
            options => [qw(perimeter_speed small_perimeter_speed external_perimeter_speed infill_speed solid_infill_speed top_solid_infill_speed support_material_speed bridge_speed gap_fill_speed)],
        },
        {
            title => 'Speed for non-print moves',
            options => [qw(travel_speed)],
        },
        {
            title => 'Modifiers',
            options => [qw(first_layer_speed)],
        },
        {
            title => 'Acceleration control (advanced)',
            options => [qw(perimeter_acceleration infill_acceleration default_acceleration)],
        },
    ]);
    
    $self->add_options_page('Skirt and brim', 'box.png', optgroups => [
        {
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height min_skirt_length)],
        },
        {
            title => 'Brim',
            options => [qw(brim_width)],
        },
    ]);
    
    $self->add_options_page('Support material', 'building.png', optgroups => [
        {
            title => 'Support material',
            options => [qw(support_material support_material_threshold support_material_pattern support_material_spacing support_material_angle)],
        },
    ]);
    
    $self->add_options_page('Notes', 'note.png', optgroups => [
        {
            title => 'Notes',
            no_labels => 1,
            options => [qw(notes)],
        },
    ]);
    
    $self->add_options_page('Output options', 'page_white_go.png', optgroups => [
        {
            title => 'Sequential printing',
            options => [qw(complete_objects extruder_clearance_radius extruder_clearance_height)],
            lines => [
                Slic3r::GUI::OptionsGroup->single_option_line('complete_objects'),
                {
                    label   => 'Extruder clearance (mm)',
                    options => [qw(extruder_clearance_radius extruder_clearance_height)],
                },
            ],
        },
        {
            title => 'Output file',
            options => [qw(gcode_comments output_filename_format)],
        },
        {
            title => 'Post-processing scripts',
            no_labels => 1,
            options => [qw(post_process)],
        },
    ]);
    
    $self->add_options_page('Multiple Extruders', 'funnel.png', optgroups => [
        {
            title => 'Extruders',
            options => [qw(perimeter_extruder infill_extruder support_material_extruder)],
        },
    ]);
    
    $self->add_options_page('Advanced', 'wrench.png', optgroups => [
        {
            title => 'Extrusion width',
            label_width => 180,
            options => [qw(extrusion_width first_layer_extrusion_width perimeter_extrusion_width infill_extrusion_width support_material_extrusion_width)],
        },
        {
            title => 'Flow',
            options => [qw(bridge_flow_ratio)],
        },
        $Slic3r::have_threads ? {
            title => 'Other',
            options => [qw(threads)],
        } : (),
    ]);
}

sub hidden_options { !$Slic3r::have_threads ? qw(threads) : () }

package Slic3r::GUI::Tab::Filament;
use base 'Slic3r::GUI::Tab';

sub name { 'filament' }
sub title { 'Filament Settings' }

sub build {
    my $self = shift;
    
    $self->add_options_page('Filament', 'spool.png', optgroups => [
        {
            title => 'Filament',
            options => ['filament_diameter#0', 'extrusion_multiplier#0'],
        },
        {
            title => 'Temperature (°C)',
            options => ['temperature#0', 'first_layer_temperature#0', qw(bed_temperature first_layer_bed_temperature)],
            lines => [
                {
                    label   => 'Extruder',
                    options => ['first_layer_temperature#0', 'temperature#0'],
                },
                {
                    label   => 'Bed',
                    options => [qw(first_layer_bed_temperature bed_temperature)],
                },
            ],
        },
    ]);
    
    $self->add_options_page('Cooling', 'hourglass.png', optgroups => [
        {
            title => 'Enable',
            options => [qw(cooling)],
            lines => [
                Slic3r::GUI::OptionsGroup->single_option_line('cooling'),
                {
                    label => '',
                    widget => ($self->{description_line} = Slic3r::GUI::OptionsGroup::StaticTextLine->new),
                },
            ],
        },
        {
            title => 'Fan settings',
            options => [qw(min_fan_speed max_fan_speed bridge_fan_speed disable_fan_first_layers fan_always_on)],
            lines => [
                {
                    label   => 'Fan speed',
                    options => [qw(min_fan_speed max_fan_speed)],
                },
                Slic3r::GUI::OptionsGroup->single_option_line('bridge_fan_speed'),
                Slic3r::GUI::OptionsGroup->single_option_line('disable_fan_first_layers'),
                Slic3r::GUI::OptionsGroup->single_option_line('fan_always_on'),
            ],
        },
        {
            title => 'Cooling thresholds',
            label_width => 250,
            options => [qw(fan_below_layer_time slowdown_below_layer_time min_print_speed)],
        },
    ]);
}

sub _update_description {
    my $self = shift;
    
    my $config = $self->config;
    
    my $msg = "";
    if ($config->cooling) {
        $msg = sprintf "If estimated layer time is below ~%ds, fan will run at 100%% and print speed will be reduced so that no less than %ds are spent on that layer (however, speed will never be reduced below %dmm/s).",
            $config->slowdown_below_layer_time, $config->slowdown_below_layer_time, $config->min_print_speed;
        if ($config->fan_below_layer_time > $config->slowdown_below_layer_time) {
            $msg .= sprintf "\nIf estimated layer time is greater, but still below ~%ds, fan will run at a proportionally decreasing speed between %d%% and %d%%.",
                $config->fan_below_layer_time, $config->max_fan_speed, $config->min_fan_speed;
        }
        if ($config->fan_always_on) {
            $msg .= sprintf "\nDuring the other layers, fan will always run at %d%%.", $config->min_fan_speed;
        } else {
            $msg .= "\nDuring the other layers, fan will be turned off."
        }
    }
    $self->{description_line}->SetText($msg);
}

sub on_value_change {
    my $self = shift;
    my ($opt_key) = @_;
    $self->SUPER::on_value_change(@_);
    
    $self->_update_description;
}

package Slic3r::GUI::Tab::Printer;
use base 'Slic3r::GUI::Tab';

sub name { 'printer' }
sub title { 'Printer Settings' }

sub build {
    my $self = shift;
    
    $self->{extruders_count} = 1;
    
    $self->add_options_page('General', 'printer_empty.png', optgroups => [
        {
            title => 'Size and coordinates',
            options => [qw(bed_size print_center z_offset)],
        },
        {
            title => 'Firmware',
            options => [qw(gcode_flavor use_relative_e_distances)],
        },
        {
            title => 'Capabilities',
            options => [
                {
                    opt_key => 'extruders_count',
                    label   => 'Extruders',
                    tooltip => 'Number of extruders of the printer.',
                    type    => 'i',
                    min     => 1,
                    default => 1,
                    on_change => sub { $self->{extruders_count} = $_[0] },
                },
            ],
        },
        {
            title => 'Advanced',
            options => [qw(vibration_limit)],
        },
    ]);
    
    $self->add_options_page('Custom G-code', 'cog.png', optgroups => [
        {
            title => 'Start G-code',
            no_labels => 1,
            options => [qw(start_gcode)],
        },
        {
            title => 'End G-code',
            no_labels => 1,
            options => [qw(end_gcode)],
        },
        {
            title => 'Layer change G-code',
            no_labels => 1,
            options => [qw(layer_gcode)],
        },
        {
            title => 'Tool change G-code',
            no_labels => 1,
            options => [qw(toolchange_gcode)],
        },
    ]);
    
    $self->{extruder_pages} = [];
    $self->_build_extruder_pages;
}

sub _extruder_options { qw(nozzle_diameter extruder_offset retract_length retract_lift retract_speed retract_restart_extra retract_before_travel
    retract_length_toolchange retract_restart_extra_toolchange) }

sub config {
    my $self = shift;
    
    my $config = $self->SUPER::config(@_);
    
    # remove all unused values
    foreach my $opt_key ($self->_extruder_options) {
        splice @{ $config->{$opt_key} }, $self->{extruders_count};
    }
    
    return $config;
}

sub _build_extruder_pages {
    my $self = shift;
    
    foreach my $extruder_idx (0 .. $self->{extruders_count}-1) {
        # build page if it doesn't exist
        $self->{extruder_pages}[$extruder_idx] ||= $self->add_options_page("Extruder " . ($extruder_idx + 1), 'funnel.png', optgroups => [
            {
                title => 'Size',
                options => ['nozzle_diameter#' . $extruder_idx],
            },
            {
                title => 'Position (for multi-extruder printers)',
                options => ['extruder_offset#' . $extruder_idx],
            },
            {
                title => 'Retraction',
                options => [
                    map "${_}#${extruder_idx}",
                        qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel)
                ],
            },
            {
                title => 'Retraction when tool is disabled (advanced settings for multi-extruder setups)',
                options => [
                    map "${_}#${extruder_idx}",
                        qw(retract_length_toolchange retract_restart_extra_toolchange)
                ],
            },
        ]);
        $self->{extruder_pages}[$extruder_idx]{disabled} = 0;
    }
    
    # rebuild page list
    @{$self->{pages}} = (
        (grep $_->{title} !~ /^Extruder \d+/, @{$self->{pages}}),
        @{$self->{extruder_pages}}[ 0 .. $self->{extruders_count}-1 ],
    );
}

sub on_value_change {
    my $self = shift;
    my ($opt_key) = @_;
    $self->SUPER::on_value_change(@_);
    
    if ($opt_key eq 'extruders_count') {
        # remove unused pages from list
        my @unused_pages = @{ $self->{extruder_pages} }[$self->{extruders_count} .. $#{$self->{extruder_pages}}];
        for my $page (@unused_pages) {
            @{$self->{pages}} = grep $_ ne $page, @{$self->{pages}};
            $page->{disabled} = 1;
        }
        
        # add extra pages
        $self->_build_extruder_pages;
        
        # update page list and select first page (General)
        $self->update_tree(0);
    }
}

# this gets executed after preset is loaded and before GUI fields are updated
sub on_preset_loaded {
    my $self = shift;
    
    # update the extruders count field
    {
        # update the GUI field according to the number of nozzle diameters supplied
        $self->set_value('extruders_count', scalar @{ $self->{config}->nozzle_diameter });
        
        # update extruder page list
        $self->on_value_change('extruders_count');
    }
}

sub load_external_config {
    my $self = shift;
    $self->SUPER::load_external_config(@_);
    
    Slic3r::GUI::warning_catcher($self)->(
        "Your configuration was imported. However, Slic3r is currently only able to import settings "
        . "for the first defined filament. We recommend you don't use exported configuration files "
        . "for multi-extruder setups and rely on the built-in preset management system instead.")
        if @{ $self->{config}->nozzle_diameter } > 1;
}

package Slic3r::GUI::Tab::Page;
use Wx qw(:misc :panel :sizer);
use base 'Wx::ScrolledWindow';

sub new {
    my $class = shift;
    my ($parent, $title, $iconID, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    $self->{optgroups}  = [];
    $self->{title}      = $title;
    $self->{iconID}     = $iconID;
    
    $self->SetScrollbars(1, 1, 1, 1);
    
    $self->{vsizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->SetSizer($self->{vsizer});
    
    if ($params{optgroups}) {
        $self->append_optgroup(
            %$_,
            config      => $parent->{config},
            on_change   => $params{on_change},
        ) for @{$params{optgroups}};
    }
    
    return $self;
}

sub append_optgroup {
    my $self = shift;
    my %params = @_;
    
    my $class = $params{class} || 'Slic3r::GUI::ConfigOptionsGroup';
    my $optgroup = $class->new(
        parent      => $self,
        config      => $self->GetParent->{config},
        label_width => 200,
        %params,
    );
    $self->{vsizer}->Add($optgroup->sizer, 0, wxEXPAND | wxALL, 5);
    push @{$self->{optgroups}}, $optgroup;
}

sub set_value {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    my $changed = 0;
    foreach my $optgroup (@{$self->{optgroups}}) {
        $changed = 1 if $optgroup->set_value($opt_key, $value);
    }
    return $changed;
}

package Slic3r::GUI::SavePresetWindow;
use Wx qw(:combobox :dialog :id :misc :sizer);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Save preset", wxDefaultPosition, wxDefaultSize);
    
    my $text = Wx::StaticText->new($self, -1, "Save " . lc($params{title}) . " as:", wxDefaultPosition, wxDefaultSize);
    $self->{combo} = Wx::ComboBox->new($self, -1, $params{default}, wxDefaultPosition, wxDefaultSize, $params{values},
                                       wxTE_PROCESS_ENTER);
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($text, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($self->{combo}, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    EVT_BUTTON($self, wxID_OK, \&accept);
    EVT_TEXT_ENTER($self, $self->{combo}, \&accept);
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

sub accept {
    my ($self, $event) = @_;

    if (($self->{chosen_name} = $self->{combo}->GetValue)) {
        if ($self->{chosen_name} =~ /^[^<>:\/\\|?*\"]+$/i) {
            $self->EndModal(wxID_OK);
        } else {
            Slic3r::GUI::show_error($self, "The supplied name is not valid; the following characters are not allowed: <>:/\|?*\"");
        }
    }
}

sub get_name {
    my $self = shift;
    return $self->{chosen_name};
}

1;
