# The "Expert" tab at the right of the main tabbed window.

# This file implements following packages:
#   Slic3r::GUI::Tab;
#       Slic3r::GUI::Tab::Print;
#       Slic3r::GUI::Tab::Filament;
#       Slic3r::GUI::Tab::Printer;
#   Slic3r::GUI::Tab::Page
#       - Option page: For example, the Slic3r::GUI::Tab::Print has option pages "Layers and perimeters", "Infill", "Skirt and brim" ...
#   Slic3r::GUI::SavePresetWindow
#       - Dialog to select a new preset name to store the configuration.
#   Slic3r::GUI::Tab::Preset;
#       - Single preset item: name, file is default or external.

package Slic3r::GUI::Tab;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use List::Util qw(first);
use Wx qw(:bookctrl :dialog :keycode :icon :id :misc :panel :sizer :treectrl :window
    :button wxTheApp wxCB_READONLY);
use Wx::Event qw(EVT_BUTTON EVT_COMBOBOX EVT_KEY_DOWN EVT_CHECKBOX EVT_TREE_SEL_CHANGED);
use base qw(Wx::Panel Class::Accessor);

sub new {
    my ($class, $parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    
    # Vertical sizer to hold the choice menu and the rest of the page.
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->{sizer}->SetSizeHints($self);
    $self->SetSizer($self->{sizer});
    
    # preset chooser
    {
        
        # choice menu
        $self->{presets_choice} = Wx::BitmapComboBox->new($self, -1, "", wxDefaultPosition, [270, -1], [], wxCB_READONLY);
        $self->{presets_choice}->SetFont($Slic3r::GUI::small_font);
        
        # buttons
        $self->{btn_save_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new(Slic3r::var("disk.png"), wxBITMAP_TYPE_PNG), 
            wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        $self->{btn_delete_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new(Slic3r::var("delete.png"), wxBITMAP_TYPE_PNG), 
            wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        $self->{show_incompatible_presets} = 0;
        $self->{bmp_show_incompatible_presets} = Wx::Bitmap->new(Slic3r::var("flag-red-icon.png"), wxBITMAP_TYPE_PNG);
        $self->{bmp_hide_incompatible_presets} = Wx::Bitmap->new(Slic3r::var("flag-green-icon.png"), wxBITMAP_TYPE_PNG);
        $self->{btn_hide_incompatible_presets} = Wx::BitmapButton->new($self, -1, 
            $self->{bmp_hide_incompatible_presets},
            wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        $self->{btn_save_preset}->SetToolTipString("Save current " . lc($self->title));
        $self->{btn_delete_preset}->SetToolTipString("Delete this preset");
        $self->{btn_delete_preset}->Disable;
        
        my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL); 
        $self->{sizer}->Add($hsizer, 0, wxBOTTOM, 3);
        $hsizer->Add($self->{presets_choice}, 1, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
        $hsizer->AddSpacer(4);
        $hsizer->Add($self->{btn_save_preset}, 0, wxALIGN_CENTER_VERTICAL);
        $hsizer->AddSpacer(4);
        $hsizer->Add($self->{btn_delete_preset}, 0, wxALIGN_CENTER_VERTICAL);
        $hsizer->AddSpacer(16);
        $hsizer->Add($self->{btn_hide_incompatible_presets}, 0, wxALIGN_CENTER_VERTICAL);
    }

    # Horizontal sizer to hold the tree and the selected page.
    $self->{hsizer} = Wx::BoxSizer->new(wxHORIZONTAL);
    $self->{sizer}->Add($self->{hsizer}, 1, wxEXPAND, 0);
    
    # left vertical sizer
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $self->{hsizer}->Add($left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);
    
    # tree
    $self->{treectrl} = Wx::TreeCtrl->new($self, -1, wxDefaultPosition, [185, -1], wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
    $left_sizer->Add($self->{treectrl}, 1, wxEXPAND);
    $self->{icons} = Wx::ImageList->new(16, 16, 1);
    # Map from an icon file name to its index in $self->{icons}.
    $self->{icon_index} = {};
    # Index of the last icon inserted into $self->{icons}.
    $self->{icon_count} = -1;
    $self->{treectrl}->AssignImageList($self->{icons});
    $self->{treectrl}->AddRoot("root");
    $self->{pages} = [];
    $self->{treectrl}->SetIndent(0);
    $self->{disable_tree_sel_changed_event} = 0;
    EVT_TREE_SEL_CHANGED($parent, $self->{treectrl}, sub {
        return if $self->{disable_tree_sel_changed_event};
        my $page = first { $_->{title} eq $self->{treectrl}->GetItemText($self->{treectrl}->GetSelection) } @{$self->{pages}}
            or return;
        $_->Hide for @{$self->{pages}};
        $page->Show;
        $self->{hsizer}->Layout;
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
    
    EVT_COMBOBOX($parent, $self->{presets_choice}, sub {
        $self->select_preset($self->{presets_choice}->GetStringSelection);
    });
    
    EVT_BUTTON($self, $self->{btn_save_preset}, sub { $self->save_preset });
    EVT_BUTTON($self, $self->{btn_delete_preset}, sub { $self->delete_preset });
    EVT_BUTTON($self, $self->{btn_hide_incompatible_presets}, sub { $self->_toggle_show_hide_incompatible });
    
    # Initialize the DynamicPrintConfig by default keys/values.
    # Possible %params keys: no_controller
    $self->build(%params);
    $self->rebuild_page_tree;
    $self->_update;
    
    return $self;
}

# Save the current preset into file.
# This removes the "dirty" flag of the preset, possibly creates a new preset under a new name,
# and activates the new preset.
# Wizard calls save_preset with a name "My Settings", otherwise no name is provided and this method
# opens a Slic3r::GUI::SavePresetWindow dialog.
sub save_preset {
    my ($self, $name) = @_;
    
    # since buttons (and choices too) don't get focus on Mac, we set focus manually
    # to the treectrl so that the EVT_* events are fired for the input field having
    # focus currently. is there anything better than this?
    $self->{treectrl}->SetFocus;
    
    if (!defined $name) {
        my $preset = $self->{presets}->get_selected_preset;
        my $default_name = $preset->default ? 'Untitled' : $preset->name;
        $default_name =~ s/\.[iI][nN][iI]$//;
        my $dlg = Slic3r::GUI::SavePresetWindow->new($self,
            title   => lc($self->title),
            default => $default_name,
            values  => [ map $_->name, grep !$_->default && !$_->external, @{$self->{presets}} ],
        );
        return unless $dlg->ShowModal == wxID_OK;
        $name = $dlg->get_name;
    }
    # Save the preset into Slic3r::data_dir/section_name/preset_name.ini
    eval { $self->{presets}->save_current_preset($name); };
    Slic3r::GUI::catch_error($self) and return;
    # Add the new item into the UI component, remove dirty flags and activate the saved item.
    $self->{presets}->update_tab_ui($self->{presets_choice}, $self->{show_incompatible_presets});
    # Update the selection boxes at the platter.
    $self->_on_presets_changed;
}

# Called for a currently selected preset.
sub delete_preset {
    my ($self) = @_;
    my $current_preset = $self->{presets}->get_selected_preset;
    # Don't let the user delete the '- default -' configuration.
    my $msg = 'Are you sure you want to ' . ($current_preset->external ? 'remove' : 'delete') . ' the selected preset?';
    my $title = ($current_preset->external ? 'Remove' : 'Delete') . ' Preset';
    return if $current_preset->default ||
        wxID_YES != Wx::MessageDialog->new($self, $msg, $title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION)->ShowModal;
    # Delete the file and select some other reasonable preset.
    # The 'external' presets will only be removed from the preset list, their files will not be deleted.
    eval { $self->{presets}->delete_current_preset; };
    Slic3r::GUI::catch_error($self) and return;
    # Load the newly selected preset into the UI, update selection combo boxes with their dirty flags.
    $self->load_current_preset;
}

sub _toggle_show_hide_incompatible {
    my ($self) = @_;
    $self->{show_incompatible_presets} = ! $self->{show_incompatible_presets};
    $self->_update_show_hide_incompatible_button;
    $self->{presets}->update_tab_ui($self->{presets_choice}, $self->{show_incompatible_presets});
}

sub _update_show_hide_incompatible_button {
    my ($self) = @_;
    $self->{btn_hide_incompatible_presets}->SetBitmap($self->{show_incompatible_presets} ?
        $self->{bmp_show_incompatible_presets} : $self->{bmp_hide_incompatible_presets});
    $self->{btn_hide_incompatible_presets}->SetToolTipString($self->{show_incompatible_presets} ?
        "Both compatible an incompatible presets are shown. Click to hide presets not compatible with the current printer." :
        "Only compatible presets are shown. Click to show both the presets compatible and not compatible with the current printer.");
}

# Register the on_value_change callback.
sub on_value_change {
    my ($self, $cb) = @_;
    $self->{on_value_change} = $cb;
}

# Register the on_presets_changed callback.
sub on_presets_changed {
    my ($self, $cb) = @_;
    $self->{on_presets_changed} = $cb;
}

# This method is called whenever an option field is changed by the user.
# Propagate event to the parent through the 'on_value_change' callback
# and call _update.
# The on_value_change callback triggers Platter::on_config_change() to configure the 3D preview
# (colors, wipe tower positon etc) and to restart the background slicing process.
sub _on_value_change {
    my ($self, $key, $value) = @_;
    $self->{on_value_change}->($key, $value) if $self->{on_value_change};
    $self->_update;
}

# Override this to capture changes of configuration caused either by loading or switching a preset,
# or by a user changing an option field.
# This callback is useful for cross-validating configuration values of a single preset.
sub _update {}

# Call a callback to update the selection of presets on the platter:
# To update the content of the selection boxes,
# to update the filament colors of the selection boxes,
# to update the "dirty" flags of the selection boxes,
# to uddate number of "filament" selection boxes when the number of extruders change.
sub _on_presets_changed {
    my ($self) = @_;
    $self->{on_presets_changed}->($self->{presets}) if $self->{on_presets_changed};
}

# For the printer profile, generate the extruder pages after a preset is loaded.
sub on_preset_loaded {}

# If the current preset is dirty, the user is asked whether the changes may be discarded.
# if the current preset was not dirty, or the user agreed to discard the changes, 1 is returned.
sub may_discard_current_dirty_preset
{
    my ($self, $presets, $new_printer_name) = @_;
    $presets //= $self->{presets};
    # Display a dialog showing the dirty options in a human readable form.
    my $old_preset = $presets->get_current_preset;
    my $type_name = $presets->name;
    my $name = $old_preset->default ? 
        ('Default ' . $type_name . ' preset') :
        ($type_name . " preset \"" . $old_preset->name . "\"");
    # Collect descriptions of the dirty options.
    my @option_names = ();
    foreach my $opt_key (@{$presets->current_dirty_options}) {
        my $opt = $Slic3r::Config::Options->{$opt_key};
        my $name = $opt->{full_label} // $opt->{label};
        $name = $opt->{category} . " > $name" if $opt->{category};
        push @option_names, $name;
    }
    # Show a confirmation dialog with the list of dirty options.
    my $changes = join "\n", map "- $_", @option_names;
    my $message = (defined $new_printer_name) ?
        "$name is not compatible with printer \"$new_printer_name\"\n and it has unsaved changes:" :
        "$name has unsaved changes:";
    my $confirm = Wx::MessageDialog->new($self, 
        $message . "\n$changes\n\nDiscard changes and continue anyway?",
        'Unsaved Changes', wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    return $confirm->ShowModal == wxID_YES;
}

# Called by the UI combo box when the user switches profiles.
# Select a preset by a name. If ! defined(name), then the default preset is selected.
# If the current profile is modified, user is asked to save the changes.
sub select_preset {
    my ($self, $name, $force) = @_;
    $force //= 0;
    my $current_dirty = $self->{presets}->current_is_dirty;
    my $canceled = 0;
    my $printer_tab = $self->{presets}->name eq 'printer';
    if (! $force && $current_dirty && ! $self->may_discard_current_dirty_preset) {
        $canceled = 1;
    } elsif ($printer_tab) {
        # Before switching the printer to a new one, verify, whether the currently active print and filament
        # are compatible with the new printer.
        # If they are not compatible and the the current print or filament are dirty, let user decide
        # whether to discard the changes or keep the current printer selection.
        my $new_printer_name = $name // '';
        my $new_printer_preset = $self->{presets}->find_preset($new_printer_name, 1);
        # my $new_nozzle_dmrs = $new_printer_preset->config->get('nozzle_diameter');
        my $print_presets = wxTheApp->{preset_bundle}->print;
        if ($print_presets->current_is_dirty &&
            ! $print_presets->get_edited_preset->is_compatible_with_printer($new_printer_name)) {
            if ($self->may_discard_current_dirty_preset($print_presets, $new_printer_name)) {
                $canceled = 1;
            } else {
                $print_presets->discard_current_changes;
            }
        }
        my $filament_presets = wxTheApp->{preset_bundle}->filament;
        # if ((@$new_nozzle_dmrs <= 1) && 
        if (! $canceled && $filament_presets->current_is_dirty &&
            ! $filament_presets->get_edited_preset->is_compatible_with_printer($new_printer_name)) {
            if ($self->may_discard_current_dirty_preset($filament_presets, $new_printer_name)) {
                $canceled = 1;
            } else {
                $filament_presets->discard_current_changes;
            }
        }
    }
    if ($canceled) {
        $self->{presets}->update_tab_ui($self->{presets_choice}, $self->{show_incompatible_presets});
        # Trigger the on_presets_changed event so that we also restore the previous value in the plater selector.
        $self->_on_presets_changed;
    } else {
        if (defined $name) {
            $self->{presets}->select_preset_by_name($name);
        } else {
            $self->{presets}->select_preset(0);
        }
        # Mark the print & filament enabled if they are compatible with the currently selected preset.
        wxTheApp->{preset_bundle}->update_compatible_with_printer(1)
            if $current_dirty || $printer_tab;
        # Initialize the UI from the current preset.
        $self->load_current_preset;
    }
}

# Initialize the UI from the current preset.
sub load_current_preset {
    my ($self) = @_;
    my $preset = $self->{presets}->get_current_preset;
    eval {
        local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
        my $method = $preset->default ? 'Disable' : 'Enable';
        $self->{btn_delete_preset}->$method;
        $self->_update;
        # For the printer profile, generate the extruder pages.
        $self->on_preset_loaded;
        # Reload preset pages with the new configuration values.
        $self->_reload_config;
    };
    # use CallAfter because some field triggers schedule on_change calls using CallAfter,
    # and we don't want them to be called after this update_dirty() as they would mark the 
    # preset dirty again
    # (not sure this is true anymore now that update_dirty is idempotent)
    wxTheApp->CallAfter(sub {
        $self->{presets}->update_tab_ui($self->{presets_choice}, $self->{show_incompatible_presets});
        $self->_on_presets_changed;
    });
}

sub add_options_page {
    my ($self, $title, $icon, %params) = @_;
    # Index of $icon in an icon list $self->{icons}.
    my $icon_idx = 0;
    if ($icon) {
        $icon_idx = $self->{icon_index}->{$icon};
        if (! defined $icon_idx) {
            # Add a new icon to the icon list.
            my $bitmap = Wx::Bitmap->new(Slic3r::var($icon), wxBITMAP_TYPE_PNG);
            $self->{icons}->Add($bitmap);
            $icon_idx = $self->{icon_count} + 1;
            $self->{icon_count} = $icon_idx;
            $self->{icon_index}->{$icon} = $icon_idx;
        }
    }
    # Initialize the page.
    my $page = Slic3r::GUI::Tab::Page->new($self, $title, $icon_idx);
    $page->Hide;
    $self->{hsizer}->Add($page, 1, wxEXPAND | wxLEFT, 5);
    push @{$self->{pages}}, $page;
    return $page;
}

# Reload current $self->{config} (aka $self->{presets}->edited_preset->config) into the UI fields.
sub _reload_config {
    my ($self) = @_;
    $self->Freeze;
    $_->reload_config for @{$self->{pages}};
    $self->Thaw;
}

# Regerenerate content of the page tree.
sub rebuild_page_tree {
    my ($self) = @_;
    $self->Freeze;
    # get label of the currently selected item
    my $selected = $self->{treectrl}->GetItemText($self->{treectrl}->GetSelection);    
    my $rootItem = $self->{treectrl}->GetRootItem;
    $self->{treectrl}->DeleteChildren($rootItem);
    my $have_selection = 0;
    foreach my $page (@{$self->{pages}}) {
        my $itemId = $self->{treectrl}->AppendItem($rootItem, $page->{title}, $page->{iconID});
        if ($page->{title} eq $selected) {
            $self->{disable_tree_sel_changed_event} = 1;
            $self->{treectrl}->SelectItem($itemId);
            $self->{disable_tree_sel_changed_event} = 0;
            $have_selection = 1;
        }
    }
    if (!$have_selection) {
        # this is triggered on first load, so we don't disable the sel change event
        $self->{treectrl}->SelectItem($self->{treectrl}->GetFirstChild($rootItem));
    }
    $self->Thaw;
}

# Update the combo box label of the selected preset based on its "dirty" state,
# comparing the selected preset config with $self->{config}.
sub update_dirty {
    my ($self) = @_;
    $self->{presets}->update_dirty_ui($self->{presets_choice});
    $self->_on_presets_changed;
}

# Load a provied DynamicConfig into the tab, modifying the active preset.
# This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
sub load_config {
    my ($self, $config) = @_;
    my $modified = 0;
    foreach my $opt_key (@{$self->{config}->diff($config)}) {
        $self->{config}->set($opt_key, $config->get($opt_key));
        $modified = 1;
    }
    if ($modified) {
        $self->update_dirty;
        # Initialize UI components with the config values.
        $self->_reload_config;
        $self->_update;
    }
}

# To be called by custom widgets, load a value into a config, 
# update the preset selection boxes (the dirty flags)
sub _load_key_value {
    my ($self, $opt_key, $value) = @_;
    $self->{config}->set($opt_key, $value);
    # Mark the print & filament enabled if they are compatible with the currently selected preset.
    if ($opt_key eq 'compatible_printers') {
        wxTheApp->{preset_bundle}->update_compatible_with_printer(0);
        $self->{presets}->update_tab_ui($self->{presets_choice}, $self->{show_incompatible_presets});
    } else {
        $self->{presets}->update_dirty_ui($self->{presets_choice});
    }
    $self->_on_presets_changed;
    $self->_update;
}

# Find a field with an index over all pages of this tab.
# This method is used often and everywhere, therefore it shall be quick.
sub get_field {
    my ($self, $opt_key, $opt_index) = @_;
    foreach my $page (@{$self->{pages}}) {
        my $field = $page->get_field($opt_key, $opt_index);
        return $field if defined $field;
    }
    return undef;
}

# Set a key/value pair on this page. Return true if the value has been modified.
# Currently used for distributing extruders_count over preset pages of Slic3r::GUI::Tab::Printer
# after a preset is loaded.
sub set_value {
    my ($self, $opt_key, $value) = @_;
    my $changed = 0;
    foreach my $page (@{$self->{pages}}) {
        $changed = 1 if $page->set_value($opt_key, $value);
    }
    return $changed;
}

# Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
sub _compatible_printers_widget {
    my ($self) = @_;
    
    return sub {
        my ($parent) = @_;
        
        my $checkbox = $self->{compatible_printers_checkbox} = Wx::CheckBox->new($parent, -1, "All");
        
        my $btn = $self->{compatible_printers_btn} = Wx::Button->new($parent, -1, "Set…", wxDefaultPosition, wxDefaultSize,
            wxBU_LEFT | wxBU_EXACTFIT);
        $btn->SetFont($Slic3r::GUI::small_font);
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("printer_empty.png"), wxBITMAP_TYPE_PNG));
        
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($checkbox, 0, wxALIGN_CENTER_VERTICAL);
        $sizer->Add($btn, 0, wxALIGN_CENTER_VERTICAL);
        
        EVT_CHECKBOX($self, $checkbox, sub {
            my $method = $checkbox->GetValue ? 'Disable' : 'Enable';
            $btn->$method;
            # All printers have been made compatible with this preset.
            $self->_load_key_value('compatible_printers', []) if $checkbox->GetValue;
        });
        
        EVT_BUTTON($self, $btn, sub {
            # Collect names of non-default non-external printer profiles.
            my @presets = map $_->name, grep !$_->default && !$_->external,
                @{wxTheApp->{preset_bundle}->printer};
            my $dlg = Wx::MultiChoiceDialog->new($self,
                "Select the printers this profile is compatible with.",
                "Compatible printers", \@presets);
            # Collect and set indices of printers marked as compatible.
            my @selections = ();
            foreach my $preset_name (@{ $self->{config}->get('compatible_printers') }) {
                my $idx = first { $presets[$_] eq $preset_name } 0..$#presets;
                push @selections, $idx if defined $idx;
            }
            $dlg->SetSelections(@selections);
            # Show the dialog.
            if ($dlg->ShowModal == wxID_OK) {
                my $value = [ @presets[$dlg->GetSelections] ];
                if (!@$value) {
                    $checkbox->SetValue(1);
                    $btn->Disable;
                }
                # All printers have been made compatible with this preset.
                $self->_load_key_value('compatible_printers', $value);
            }
        });
        
        return $sizer;
    };
}

sub _reload_compatible_printers_widget {
    my ($self) = @_;
    my $has_any = int(@{$self->{config}->get('compatible_printers')}) > 0;
    my $method = $has_any ? 'Enable' : 'Disable';
    $self->{compatible_printers_checkbox}->SetValue(! $has_any);
    $self->{compatible_printers_btn}->$method;
}

sub update_ui_from_settings {
    my ($self) = @_;
    # Show the 'show / hide presets' button only for the print and filament tabs, and only if enabled
    # in application preferences.
    my $show   = wxTheApp->{app_config}->get("show_incompatible_presets") && $self->{presets}->name ne 'printer';
    my $method = $show ? 'Show' : 'Hide';
    $self->{btn_hide_incompatible_presets}->$method;
    # If the 'show / hide presets' button is hidden, hide the incompatible presets.
    if ($show) {
        $self->_update_show_hide_incompatible_button;
    } else {
        if ($self->{show_incompatible_presets}) {
            $self->{show_incompatible_presets} = 0;
            $self->{presets}->update_tab_ui($self->{presets_choice}, 0);
        }
    }
}

package Slic3r::GUI::Tab::Print;
use base 'Slic3r::GUI::Tab';

use List::Util qw(first);
use Wx qw(:icon :dialog :id wxTheApp);

sub name { 'print' }
sub title { 'Print Settings' }

sub build {
    my $self = shift;
    
    $self->{presets} = wxTheApp->{preset_bundle}->print;
    $self->{config} = $self->{presets}->get_edited_preset->config;
    
    {
        my $page = $self->add_options_page('Layers and perimeters', 'layers.png');
        {
            my $optgroup = $page->new_optgroup('Layer height');
            $optgroup->append_single_option_line('layer_height');
            $optgroup->append_single_option_line('first_layer_height');
        }
        {
            my $optgroup = $page->new_optgroup('Vertical shells');
            $optgroup->append_single_option_line('perimeters');
            $optgroup->append_single_option_line('spiral_vase');
        }
        {
            my $optgroup = $page->new_optgroup('Horizontal shells');
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label => 'Solid layers',
            );
            $line->append_option($optgroup->get_option('top_solid_layers'));
            $line->append_option($optgroup->get_option('bottom_solid_layers'));
            $optgroup->append_line($line);
        }
        {
            my $optgroup = $page->new_optgroup('Quality (slower slicing)');
            $optgroup->append_single_option_line('extra_perimeters');
            $optgroup->append_single_option_line('ensure_vertical_shell_thickness');
            $optgroup->append_single_option_line('avoid_crossing_perimeters');
            $optgroup->append_single_option_line('thin_walls');
            $optgroup->append_single_option_line('overhangs');
        }
        {
            my $optgroup = $page->new_optgroup('Advanced');
            $optgroup->append_single_option_line('seam_position');
            $optgroup->append_single_option_line('external_perimeters_first');
        }
    }
    
    {
        my $page = $self->add_options_page('Infill', 'infill.png');
        {
            my $optgroup = $page->new_optgroup('Infill');
            $optgroup->append_single_option_line('fill_density');
            $optgroup->append_single_option_line('fill_pattern');
            $optgroup->append_single_option_line('external_fill_pattern');
        }
        {
            my $optgroup = $page->new_optgroup('Reducing printing time');
            $optgroup->append_single_option_line('infill_every_layers');
            $optgroup->append_single_option_line('infill_only_where_needed');
        }
        {
            my $optgroup = $page->new_optgroup('Advanced');
            $optgroup->append_single_option_line('solid_infill_every_layers');
            $optgroup->append_single_option_line('fill_angle');
            $optgroup->append_single_option_line('solid_infill_below_area');
            $optgroup->append_single_option_line('bridge_angle');
            $optgroup->append_single_option_line('only_retract_when_crossing_perimeters');
            $optgroup->append_single_option_line('infill_first');
        }
    }
    
    {
        my $page = $self->add_options_page('Skirt and brim', 'box.png');
        {
            my $optgroup = $page->new_optgroup('Skirt');
            $optgroup->append_single_option_line('skirts');
            $optgroup->append_single_option_line('skirt_distance');
            $optgroup->append_single_option_line('skirt_height');
            $optgroup->append_single_option_line('min_skirt_length');
        }
        {
            my $optgroup = $page->new_optgroup('Brim');
            $optgroup->append_single_option_line('brim_width');
        }
    }
    
    {
        my $page = $self->add_options_page('Support material', 'building.png');
        {
            my $optgroup = $page->new_optgroup('Support material');
            $optgroup->append_single_option_line('support_material');
            $optgroup->append_single_option_line('support_material_threshold');
            $optgroup->append_single_option_line('support_material_enforce_layers');
        }
        {
            my $optgroup = $page->new_optgroup('Raft');
            $optgroup->append_single_option_line('raft_layers');
#            $optgroup->append_single_option_line('raft_contact_distance');
        }
        {
            my $optgroup = $page->new_optgroup('Options for support material and raft');
            $optgroup->append_single_option_line('support_material_contact_distance');
            $optgroup->append_single_option_line('support_material_pattern');
            $optgroup->append_single_option_line('support_material_with_sheath');
            $optgroup->append_single_option_line('support_material_spacing');
            $optgroup->append_single_option_line('support_material_angle');
            $optgroup->append_single_option_line('support_material_interface_layers');
            $optgroup->append_single_option_line('support_material_interface_spacing');
            $optgroup->append_single_option_line('support_material_interface_contact_loops');
            $optgroup->append_single_option_line('support_material_buildplate_only');
            $optgroup->append_single_option_line('support_material_xy_spacing');
            $optgroup->append_single_option_line('dont_support_bridges');
            $optgroup->append_single_option_line('support_material_synchronize_layers');
        }
    }
    
    {
        my $page = $self->add_options_page('Speed', 'time.png');
        {
            my $optgroup = $page->new_optgroup('Speed for print moves');
            $optgroup->append_single_option_line('perimeter_speed');
            $optgroup->append_single_option_line('small_perimeter_speed');
            $optgroup->append_single_option_line('external_perimeter_speed');
            $optgroup->append_single_option_line('infill_speed');
            $optgroup->append_single_option_line('solid_infill_speed');
            $optgroup->append_single_option_line('top_solid_infill_speed');
            $optgroup->append_single_option_line('support_material_speed');
            $optgroup->append_single_option_line('support_material_interface_speed');
            $optgroup->append_single_option_line('bridge_speed');
            $optgroup->append_single_option_line('gap_fill_speed');
        }
        {
            my $optgroup = $page->new_optgroup('Speed for non-print moves');
            $optgroup->append_single_option_line('travel_speed');
        }
        {
            my $optgroup = $page->new_optgroup('Modifiers');
            $optgroup->append_single_option_line('first_layer_speed');
        }
        {
            my $optgroup = $page->new_optgroup('Acceleration control (advanced)');
            $optgroup->append_single_option_line('perimeter_acceleration');
            $optgroup->append_single_option_line('infill_acceleration');
            $optgroup->append_single_option_line('bridge_acceleration');
            $optgroup->append_single_option_line('first_layer_acceleration');
            $optgroup->append_single_option_line('default_acceleration');
        }
        {
            my $optgroup = $page->new_optgroup('Autospeed (advanced)');
            $optgroup->append_single_option_line('max_print_speed');
            $optgroup->append_single_option_line('max_volumetric_speed');
            $optgroup->append_single_option_line('max_volumetric_extrusion_rate_slope_positive');
            $optgroup->append_single_option_line('max_volumetric_extrusion_rate_slope_negative');
        }
    }
    
    {
        my $page = $self->add_options_page('Multiple Extruders', 'funnel.png');
        {
            my $optgroup = $page->new_optgroup('Extruders');
            $optgroup->append_single_option_line('perimeter_extruder');
            $optgroup->append_single_option_line('infill_extruder');
            $optgroup->append_single_option_line('solid_infill_extruder');
            $optgroup->append_single_option_line('support_material_extruder');
            $optgroup->append_single_option_line('support_material_interface_extruder');
        }
        {
            my $optgroup = $page->new_optgroup('Ooze prevention');
            $optgroup->append_single_option_line('ooze_prevention');
            $optgroup->append_single_option_line('standby_temperature_delta');
        }
        {
            my $optgroup = $page->new_optgroup('Wipe tower');
            $optgroup->append_single_option_line('wipe_tower');
            $optgroup->append_single_option_line('wipe_tower_x');
            $optgroup->append_single_option_line('wipe_tower_y');
            $optgroup->append_single_option_line('wipe_tower_width');
            $optgroup->append_single_option_line('wipe_tower_per_color_wipe');
        }
        {
            my $optgroup = $page->new_optgroup('Advanced');
            $optgroup->append_single_option_line('interface_shells');
        }
    }
    
    {
        my $page = $self->add_options_page('Advanced', 'wrench.png');
        {
            my $optgroup = $page->new_optgroup('Extrusion width',
                label_width => 180,
            );
            $optgroup->append_single_option_line('extrusion_width');
            $optgroup->append_single_option_line('first_layer_extrusion_width');
            $optgroup->append_single_option_line('perimeter_extrusion_width');
            $optgroup->append_single_option_line('external_perimeter_extrusion_width');
            $optgroup->append_single_option_line('infill_extrusion_width');
            $optgroup->append_single_option_line('solid_infill_extrusion_width');
            $optgroup->append_single_option_line('top_infill_extrusion_width');
            $optgroup->append_single_option_line('support_material_extrusion_width');
        }
        {
            my $optgroup = $page->new_optgroup('Overlap');
            $optgroup->append_single_option_line('infill_overlap');
        }
        {
            my $optgroup = $page->new_optgroup('Flow');
            $optgroup->append_single_option_line('bridge_flow_ratio');
        }
        {
            my $optgroup = $page->new_optgroup('Other');
            $optgroup->append_single_option_line('clip_multipart_objects');
            $optgroup->append_single_option_line('elefant_foot_compensation');
            $optgroup->append_single_option_line('xy_size_compensation');
#            $optgroup->append_single_option_line('threads');
            $optgroup->append_single_option_line('resolution');
        }
    }
    
    {
        my $page = $self->add_options_page('Output options', 'page_white_go.png');
        {
            my $optgroup = $page->new_optgroup('Sequential printing');
            $optgroup->append_single_option_line('complete_objects');
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label => 'Extruder clearance (mm)',
            );
            foreach my $opt_key (qw(extruder_clearance_radius extruder_clearance_height)) {
                my $option = $optgroup->get_option($opt_key);
                $option->width(60);
                $line->append_option($option);
            }
            $optgroup->append_line($line);
        }
        {
            my $optgroup = $page->new_optgroup('Output file');
            $optgroup->append_single_option_line('gcode_comments');
            
            {
                my $option = $optgroup->get_option('output_filename_format');
                $option->full_width(1);
                $optgroup->append_single_option_line($option);
            }
        }
        {
            my $optgroup = $page->new_optgroup('Post-processing scripts',
                label_width => 0,
            );
            my $option = $optgroup->get_option('post_process');
            $option->full_width(1);
            $option->height(50);
            $optgroup->append_single_option_line($option);
        }
    }
    
    {
        my $page = $self->add_options_page('Notes', 'note.png');
        {
            my $optgroup = $page->new_optgroup('Notes',
                label_width => 0,
            );
            my $option = $optgroup->get_option('notes');
            $option->full_width(1);
            $option->height(250);
            $optgroup->append_single_option_line($option);
        }
    }

    {
        my $page = $self->add_options_page('Dependencies', 'wrench.png');
        {
            my $optgroup = $page->new_optgroup('Profile dependencies');
            {
                my $line = Slic3r::GUI::OptionsGroup::Line->new(
                    label       => 'Compatible printers',
                    widget      => $self->_compatible_printers_widget,
                );
                $optgroup->append_line($line);
            }
        }
    }
}

# Reload current $self->{config} (aka $self->{presets}->edited_preset->config) into the UI fields.
sub _reload_config {
    my ($self) = @_;
    $self->_reload_compatible_printers_widget;
    $self->SUPER::_reload_config;
}

# Slic3r::GUI::Tab::Print::_update is called after a configuration preset is loaded or switched, or when a single option is modifed by the user.
sub _update {
    my ($self) = @_;
    $self->Freeze;

    my $config = $self->{config};
    
    if ($config->spiral_vase && !($config->perimeters == 1 && $config->top_solid_layers == 0 && $config->fill_density == 0)) {
        my $dialog = Wx::MessageDialog->new($self,
            "The Spiral Vase mode requires:\n"
            . "- one perimeter\n"
            . "- no top solid layers\n"
            . "- 0% fill density\n"
            . "- no support material\n"
            . "- no ensure_vertical_shell_thickness\n"
            . "\nShall I adjust those settings in order to enable Spiral Vase?",
            'Spiral Vase', wxICON_WARNING | wxYES | wxNO);
        my $new_conf = Slic3r::Config->new;
        if ($dialog->ShowModal() == wxID_YES) {
            $new_conf->set("perimeters", 1);
            $new_conf->set("top_solid_layers", 0);
            $new_conf->set("fill_density", 0);
            $new_conf->set("support_material", 0);
            $new_conf->set("ensure_vertical_shell_thickness", 0);
        } else {
            $new_conf->set("spiral_vase", 0);
        }
        $self->load_config($new_conf);
    }

    if ($config->wipe_tower && 
        ($config->first_layer_height != 0.2 || $config->layer_height < 0.15 || $config->layer_height > 0.35)) {
        my $dialog = Wx::MessageDialog->new($self,
            "The Wipe Tower currently supports only:\n"
            . "- first layer height 0.2mm\n"
            . "- layer height from 0.15mm to 0.35mm\n"
            . "\nShall I adjust those settings in order to enable the Wipe Tower?",
            'Wipe Tower', wxICON_WARNING | wxYES | wxNO);
        my $new_conf = Slic3r::Config->new;
        if ($dialog->ShowModal() == wxID_YES) {
            $new_conf->set("first_layer_height", 0.2);
            $new_conf->set("layer_height", 0.15) if  $config->layer_height < 0.15;
            $new_conf->set("layer_height", 0.35) if  $config->layer_height > 0.35;
        } else {
            $new_conf->set("wipe_tower", 0);
        }
        $self->load_config($new_conf);
    }

    if ($config->wipe_tower && $config->support_material && $config->support_material_contact_distance > 0. && 
        ($config->support_material_extruder != 0 || $config->support_material_interface_extruder != 0)) {
        my $dialog = Wx::MessageDialog->new($self,
            "The Wipe Tower currently supports the non-soluble supports only\n"
            . "if they are printed with the current extruder without triggering a tool change.\n"
            . "(both support_material_extruder and support_material_interface_extruder need to be set to 0).\n"
            . "\nShall I adjust those settings in order to enable the Wipe Tower?",
            'Wipe Tower', wxICON_WARNING | wxYES | wxNO);
        my $new_conf = Slic3r::Config->new;
        if ($dialog->ShowModal() == wxID_YES) {
            $new_conf->set("support_material_extruder", 0);
            $new_conf->set("support_material_interface_extruder", 0);
        } else {
            $new_conf->set("wipe_tower", 0);
        }
        $self->load_config($new_conf);
    }

    if ($config->wipe_tower && $config->support_material && $config->support_material_contact_distance == 0 && 
        ! $config->support_material_synchronize_layers) {
        my $dialog = Wx::MessageDialog->new($self,
            "For the Wipe Tower to work with the soluble supports, the support layers\n"
            . "need to be synchronized with the object layers.\n"
            . "\nShall I synchronize support layers in order to enable the Wipe Tower?",
            'Wipe Tower', wxICON_WARNING | wxYES | wxNO);
        my $new_conf = Slic3r::Config->new;
        if ($dialog->ShowModal() == wxID_YES) {
            $new_conf->set("support_material_synchronize_layers", 1);
        } else {
            $new_conf->set("wipe_tower", 0);
        }
        $self->load_config($new_conf);
    }

    if ($config->support_material) {
        # Ask only once.
        if (! $self->{support_material_overhangs_queried}) {
            $self->{support_material_overhangs_queried} = 1;
            if ($config->overhangs != 1) {
                my $dialog = Wx::MessageDialog->new($self,
                    "Supports work better, if the following feature is enabled:\n"
                    . "- Detect bridging perimeters\n"
                    . "\nShall I adjust those settings for supports?",
                    'Support Generator', wxICON_WARNING | wxYES | wxNO | wxCANCEL);
                my $answer = $dialog->ShowModal();
                my $new_conf = Slic3r::Config->new;
                if ($answer == wxID_YES) {
                    # Enable "detect bridging perimeters".
                    $new_conf->set("overhangs", 1);
                } elsif ($answer == wxID_NO) {
                    # Do nothing, leave supports on and "detect bridging perimeters" off.
                } elsif ($answer == wxID_CANCEL) {
                    # Disable supports.
                    $new_conf->set("support_material", 0);
                    $self->{support_material_overhangs_queried} = 0;
                }
                $self->load_config($new_conf);
            }
        }
    } else {
        $self->{support_material_overhangs_queried} = 0;
    }
    
    if ($config->fill_density == 100
        && !first { $_ eq $config->fill_pattern } @{$Slic3r::Config::Options->{external_fill_pattern}{values}}) {
        my $dialog = Wx::MessageDialog->new($self,
            "The " . $config->fill_pattern . " infill pattern is not supposed to work at 100% density.\n"
            . "\nShall I switch to rectilinear fill pattern?",
            'Infill', wxICON_WARNING | wxYES | wxNO);
        
        my $new_conf = Slic3r::Config->new;
        if ($dialog->ShowModal() == wxID_YES) {
            $new_conf->set("fill_pattern", 'rectilinear');
            $new_conf->set("fill_density", 100);
        } else {
            $new_conf->set("fill_density", 40);
        }
        $self->load_config($new_conf);
    }
    
    my $have_perimeters = $config->perimeters > 0;
    $self->get_field($_)->toggle($have_perimeters)
        for qw(extra_perimeters ensure_vertical_shell_thickness thin_walls overhangs seam_position external_perimeters_first
            external_perimeter_extrusion_width
            perimeter_speed small_perimeter_speed external_perimeter_speed);
    
    my $have_infill = $config->fill_density > 0;
    # infill_extruder uses the same logic as in Print::extruders()
    $self->get_field($_)->toggle($have_infill)
        for qw(fill_pattern infill_every_layers infill_only_where_needed solid_infill_every_layers
            solid_infill_below_area infill_extruder);
    
    my $have_solid_infill = ($config->top_solid_layers > 0) || ($config->bottom_solid_layers > 0);
    # solid_infill_extruder uses the same logic as in Print::extruders()
    $self->get_field($_)->toggle($have_solid_infill)
        for qw(external_fill_pattern infill_first solid_infill_extruder solid_infill_extrusion_width
            solid_infill_speed);
    
    $self->get_field($_)->toggle($have_infill || $have_solid_infill)
        for qw(fill_angle bridge_angle infill_extrusion_width infill_speed bridge_speed);
    
    $self->get_field('gap_fill_speed')->toggle($have_perimeters && $have_infill);
    
    my $have_top_solid_infill = $config->top_solid_layers > 0;
    $self->get_field($_)->toggle($have_top_solid_infill)
        for qw(top_infill_extrusion_width top_solid_infill_speed);
    
    my $have_default_acceleration = $config->default_acceleration > 0;
    $self->get_field($_)->toggle($have_default_acceleration)
        for qw(perimeter_acceleration infill_acceleration bridge_acceleration first_layer_acceleration);
    
    my $have_skirt = $config->skirts > 0 || $config->min_skirt_length > 0;
    $self->get_field($_)->toggle($have_skirt)
        for qw(skirt_distance skirt_height);
    
    my $have_brim = $config->brim_width > 0;
    # perimeter_extruder uses the same logic as in Print::extruders()
    $self->get_field('perimeter_extruder')->toggle($have_perimeters || $have_brim);
    
    my $have_raft = $config->raft_layers > 0;
    my $have_support_material = $config->support_material || $have_raft;
    my $have_support_interface = $config->support_material_interface_layers > 0;
    my $have_support_soluble = $have_support_material && $config->support_material_contact_distance == 0;
    $self->get_field($_)->toggle($have_support_material)
        for qw(support_material_threshold support_material_pattern support_material_with_sheath
            support_material_spacing support_material_angle
            support_material_interface_layers dont_support_bridges
            support_material_extrusion_width support_material_contact_distance support_material_xy_spacing);
    $self->get_field($_)->toggle($have_support_material && $have_support_interface)
        for qw(support_material_interface_spacing support_material_interface_extruder
            support_material_interface_speed support_material_interface_contact_loops);
    $self->get_field('support_material_synchronize_layers')->toggle($have_support_soluble);

    $self->get_field('perimeter_extrusion_width')->toggle($have_perimeters || $have_skirt || $have_brim);
    $self->get_field('support_material_extruder')->toggle($have_support_material || $have_skirt);
    $self->get_field('support_material_speed')->toggle($have_support_material || $have_brim || $have_skirt);
    
    my $have_sequential_printing = $config->complete_objects;
    $self->get_field($_)->toggle($have_sequential_printing)
        for qw(extruder_clearance_radius extruder_clearance_height);
    
    my $have_ooze_prevention = $config->ooze_prevention;
    $self->get_field($_)->toggle($have_ooze_prevention)
        for qw(standby_temperature_delta);

    my $have_wipe_tower = $config->wipe_tower;
    $self->get_field($_)->toggle($have_wipe_tower)
        for qw(wipe_tower_x wipe_tower_y wipe_tower_width wipe_tower_per_color_wipe);

    $self->Thaw;
}

package Slic3r::GUI::Tab::Filament;
use base 'Slic3r::GUI::Tab';
use Wx qw(wxTheApp);

sub name { 'filament' }
sub title { 'Filament Settings' }

sub build {
    my $self = shift;
    
    $self->{presets} = wxTheApp->{preset_bundle}->filament;
    $self->{config} = $self->{presets}->get_edited_preset->config;
    
    {
        my $page = $self->add_options_page('Filament', 'spool.png');
        {
            my $optgroup = $page->new_optgroup('Filament');
            $optgroup->append_single_option_line('filament_colour', 0);
            $optgroup->append_single_option_line('filament_diameter', 0);
            $optgroup->append_single_option_line('extrusion_multiplier', 0);
            $optgroup->append_single_option_line('filament_density', 0);
            $optgroup->append_single_option_line('filament_cost', 0);
        }
    
        {
            my $optgroup = $page->new_optgroup('Temperature (°C)');
        
            {
                my $line = Slic3r::GUI::OptionsGroup::Line->new(
                    label => 'Extruder',
                );
                $line->append_option($optgroup->get_option('first_layer_temperature', 0));
                $line->append_option($optgroup->get_option('temperature', 0));
                $optgroup->append_line($line);
            }
        
            {
                my $line = Slic3r::GUI::OptionsGroup::Line->new(
                    label => 'Bed',
                );
                $line->append_option($optgroup->get_option('first_layer_bed_temperature', 0));
                $line->append_option($optgroup->get_option('bed_temperature', 0));
                $optgroup->append_line($line);
            }
        }
    }
    
    {
        my $page = $self->add_options_page('Cooling', 'hourglass.png');
        {
            my $optgroup = $page->new_optgroup('Enable');
            $optgroup->append_single_option_line('fan_always_on', 0);
            $optgroup->append_single_option_line('cooling', 0);
            
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label       => '',
                full_width  => 1,
                widget      => sub {
                    my ($parent) = @_;
                    return $self->{cooling_description_line} = Slic3r::GUI::OptionsGroup::StaticText->new($parent);
                },
            );
            $optgroup->append_line($line);
        }
        {
            my $optgroup = $page->new_optgroup('Fan settings');
            
            {
                my $line = Slic3r::GUI::OptionsGroup::Line->new(
                    label => 'Fan speed',
                );
                $line->append_option($optgroup->get_option('min_fan_speed', 0));
                $line->append_option($optgroup->get_option('max_fan_speed', 0));
                $optgroup->append_line($line);
            }
            
            $optgroup->append_single_option_line('bridge_fan_speed', 0);
            $optgroup->append_single_option_line('disable_fan_first_layers', 0);
        }
        {
            my $optgroup = $page->new_optgroup('Cooling thresholds',
                label_width => 250,
            );
            $optgroup->append_single_option_line('fan_below_layer_time', 0);
            $optgroup->append_single_option_line('slowdown_below_layer_time', 0);
            $optgroup->append_single_option_line('min_print_speed', 0);
        }
    }

    {
        my $page = $self->add_options_page('Advanced', 'wrench.png');
        {
            my $optgroup = $page->new_optgroup('Filament properties');
            $optgroup->append_single_option_line('filament_type', 0);
            $optgroup->append_single_option_line('filament_soluble', 0);
            
            $optgroup = $page->new_optgroup('Print speed override');
            $optgroup->append_single_option_line('filament_max_volumetric_speed', 0);

            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label       => '',
                full_width  => 1,
                widget      => sub {
                    my ($parent) = @_;
                    return $self->{volumetric_speed_description_line} = Slic3r::GUI::OptionsGroup::StaticText->new($parent);
                },
            );
            $optgroup->append_line($line);
        }
    }

    {
        my $page = $self->add_options_page('Custom G-code', 'cog.png');
        {
            my $optgroup = $page->new_optgroup('Start G-code',
                label_width => 0,
            );
            my $option = $optgroup->get_option('start_filament_gcode', 0);
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
        {
            my $optgroup = $page->new_optgroup('End G-code',
                label_width => 0,
            );
            my $option = $optgroup->get_option('end_filament_gcode', 0);
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
    }
    
    {
        my $page = $self->add_options_page('Notes', 'note.png');
        {
            my $optgroup = $page->new_optgroup('Notes',
                label_width => 0,
            );
            my $option = $optgroup->get_option('filament_notes', 0);
            $option->full_width(1);
            $option->height(250);
            $optgroup->append_single_option_line($option);
        }
    }

    {
        my $page = $self->add_options_page('Dependencies', 'wrench.png');
        {
            my $optgroup = $page->new_optgroup('Profile dependencies');
            {
                my $line = Slic3r::GUI::OptionsGroup::Line->new(
                    label       => 'Compatible printers',
                    widget      => $self->_compatible_printers_widget,
                );
                $optgroup->append_line($line);
            }
        }
    }
}

# Reload current $self->{config} (aka $self->{presets}->edited_preset->config) into the UI fields.
sub _reload_config {
    my ($self) = @_;
    $self->_reload_compatible_printers_widget;
    $self->SUPER::_reload_config;
}

# Slic3r::GUI::Tab::Filament::_update is called after a configuration preset is loaded or switched, or when a single option is modifed by the user.
sub _update {
    my ($self) = @_;
    
    $self->{cooling_description_line}->SetText(
        Slic3r::GUI::PresetHints::cooling_description($self->{presets}->get_edited_preset));
    $self->{volumetric_speed_description_line}->SetText(
        Slic3r::GUI::PresetHints::maximum_volumetric_flow_description(wxTheApp->{preset_bundle}));
    
    my $cooling = $self->{config}->cooling->[0];
    my $fan_always_on = $cooling || $self->{config}->fan_always_on->[0];
    $self->get_field($_, 0)->toggle($cooling)
        for qw(max_fan_speed fan_below_layer_time slowdown_below_layer_time min_print_speed);
    $self->get_field($_, 0)->toggle($fan_always_on)
        for qw(min_fan_speed disable_fan_first_layers);
}

package Slic3r::GUI::Tab::Printer;
use base 'Slic3r::GUI::Tab';
use Wx qw(wxTheApp :sizer :button :bitmap :misc :id :icon :dialog);
use Wx::Event qw(EVT_BUTTON);

sub name { 'printer' }
sub title { 'Printer Settings' }

sub build {
    my ($self, %params) = @_;
    
    $self->{presets} = wxTheApp->{preset_bundle}->printer;
    $self->{config} = $self->{presets}->get_edited_preset->config;
    $self->{extruders_count} = scalar @{$self->{config}->nozzle_diameter};

    my $bed_shape_widget = sub {
        my ($parent) = @_;
        
        my $btn = Wx::Button->new($parent, -1, "Set…", wxDefaultPosition, wxDefaultSize,
            wxBU_LEFT | wxBU_EXACTFIT);
        $btn->SetFont($Slic3r::GUI::small_font);
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("printer_empty.png"), wxBITMAP_TYPE_PNG));
        
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($btn);
        
        EVT_BUTTON($self, $btn, sub {
            my $dlg = Slic3r::GUI::BedShapeDialog->new($self, $self->{config}->bed_shape);
            $self->_load_key_value('bed_shape', $dlg->GetValue) if $dlg->ShowModal == wxID_OK;
        });
        
        return $sizer;
    };
        
    {
        my $page = $self->add_options_page('General', 'printer_empty.png');
        {
            my $optgroup = $page->new_optgroup('Size and coordinates');
            
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label       => 'Bed shape',
                widget      => $bed_shape_widget,
            );
            $optgroup->append_line($line);
            
            $optgroup->append_single_option_line('z_offset');
        }
        {
            my $optgroup = $page->new_optgroup('Capabilities');
            {
                my $option = Slic3r::GUI::OptionsGroup::Option->new(
                    opt_id      => 'extruders_count',
                    type        => 'i',
                    default     => 1,
                    label       => 'Extruders',
                    tooltip     => 'Number of extruders of the printer.',
                    min         => 1,
                );
                $optgroup->append_single_option_line($option);
                $optgroup->append_single_option_line('single_extruder_multi_material');
            }
            $optgroup->on_change(sub {
                my ($opt_key, $value) = @_;
                wxTheApp->CallAfter(sub {
                    if ($opt_key eq 'extruders_count') {
                        $self->_extruders_count_changed($optgroup->get_value('extruders_count'));
                        $self->update_dirty;
                    } else {
                        $self->update_dirty;
                        $self->_on_value_change($opt_key, $value);
                    }
                });
            });
        }
        if (!$params{no_controller})
        {
            my $optgroup = $page->new_optgroup('USB/Serial connection');
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label => 'Serial port',
            );
            my $serial_port = $optgroup->get_option('serial_port');
            $serial_port->side_widget(sub {
                my ($parent) = @_;
                
                my $btn = Wx::BitmapButton->new($parent, -1, Wx::Bitmap->new(Slic3r::var("arrow_rotate_clockwise.png"), wxBITMAP_TYPE_PNG),
                    wxDefaultPosition, wxDefaultSize, &Wx::wxBORDER_NONE);
                $btn->SetToolTipString("Rescan serial ports")
                    if $btn->can('SetToolTipString');
                EVT_BUTTON($self, $btn, \&_update_serial_ports);
                
                return $btn;
            });
            my $serial_test = sub {
                my ($parent) = @_;
                
                my $btn = $self->{serial_test_btn} = Wx::Button->new($parent, -1,
                    "Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
                $btn->SetFont($Slic3r::GUI::small_font);
                $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("wrench.png"), wxBITMAP_TYPE_PNG));
                
                EVT_BUTTON($self, $btn, sub {
                    my $sender = Slic3r::GCode::Sender->new;
                    my $res = $sender->connect(
                        $self->{config}->serial_port,
                        $self->{config}->serial_speed,
                    );
                    if ($res && $sender->wait_connected) {
                        Slic3r::GUI::show_info($self, "Connection to printer works correctly.", "Success!");
                    } else {
                        Slic3r::GUI::show_error($self, "Connection failed.");
                    }
                });
                return $btn;
            };
            $line->append_option($serial_port);
            $line->append_option($optgroup->get_option('serial_speed'));
            $line->append_widget($serial_test);
            $optgroup->append_line($line);
        }
        {
            my $optgroup = $page->new_optgroup('OctoPrint upload');
            
            # append two buttons to the Host line
            my $octoprint_host_browse = sub {
                my ($parent) = @_;
                
                my $btn = Wx::Button->new($parent, -1, "Browse…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
                $btn->SetFont($Slic3r::GUI::small_font);
                $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("zoom.png"), wxBITMAP_TYPE_PNG));
                
                if (!eval "use Net::Bonjour; 1") {
                    $btn->Disable;
                }
                
                EVT_BUTTON($self, $btn, sub {
                    # look for devices
                    my $entries;
                    {
                        my $res = Net::Bonjour->new('http');
                        $res->discover;
                        $entries = [ $res->entries ];
                    }
                    if (@{$entries}) {
                        my $dlg = Slic3r::GUI::BonjourBrowser->new($self, $entries);
                        $self->_load_key_value('octoprint_host', $dlg->GetValue . ":" . $dlg->GetPort)
                            if $dlg->ShowModal == wxID_OK;
                    } else {
                        Wx::MessageDialog->new($self, 'No Bonjour device found', 'Device Browser', wxOK | wxICON_INFORMATION)->ShowModal;
                    }
                });
                
                return $btn;
            };
            my $octoprint_host_test = sub {
                my ($parent) = @_;
                
                my $btn = $self->{octoprint_host_test_btn} = Wx::Button->new($parent, -1,
                    "Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
                $btn->SetFont($Slic3r::GUI::small_font);
                $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("wrench.png"), wxBITMAP_TYPE_PNG));
                
                EVT_BUTTON($self, $btn, sub {
                    my $ua = LWP::UserAgent->new;
                    $ua->timeout(10);
    
                    my $res = $ua->get(
                        "http://" . $self->{config}->octoprint_host . "/api/version",
                        'X-Api-Key' => $self->{config}->octoprint_apikey,
                    );
                    if ($res->is_success) {
                        Slic3r::GUI::show_info($self, "Connection to OctoPrint works correctly.", "Success!");
                    } else {
                        Slic3r::GUI::show_error($self,
                            "I wasn't able to connect to OctoPrint (" . $res->status_line . "). "
                            . "Check hostname and OctoPrint version (at least 1.1.0 is required).");
                    }
                });
                return $btn;
            };
            
            my $host_line = $optgroup->create_single_option_line('octoprint_host');
            $host_line->append_widget($octoprint_host_browse);
            $host_line->append_widget($octoprint_host_test);
            $optgroup->append_line($host_line);
            $optgroup->append_single_option_line('octoprint_apikey');
        }
        {
            my $optgroup = $page->new_optgroup('Firmware');
            $optgroup->append_single_option_line('gcode_flavor');
        }
        {
            my $optgroup = $page->new_optgroup('Advanced');
            $optgroup->append_single_option_line('use_relative_e_distances');
            $optgroup->append_single_option_line('use_firmware_retraction');
            $optgroup->append_single_option_line('use_volumetric_e');
            $optgroup->append_single_option_line('variable_layer_height');
        }
    }
    {
        my $page = $self->add_options_page('Custom G-code', 'cog.png');
        {
            my $optgroup = $page->new_optgroup('Start G-code',
                label_width => 0,
            );
            my $option = $optgroup->get_option('start_gcode');
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
        {
            my $optgroup = $page->new_optgroup('End G-code',
                label_width => 0,
            );
            my $option = $optgroup->get_option('end_gcode');
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
        {
            my $optgroup = $page->new_optgroup('Before layer change G-code',
                label_width => 0,
            );
            my $option = $optgroup->get_option('before_layer_gcode');
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
        {
            my $optgroup = $page->new_optgroup('After layer change G-code',
                label_width => 0,
            );
            my $option = $optgroup->get_option('layer_gcode');
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
        {
            my $optgroup = $page->new_optgroup('Tool change G-code',
                label_width => 0,
            );
            my $option = $optgroup->get_option('toolchange_gcode');
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
        {
            my $optgroup = $page->new_optgroup('Between objects G-code (for sequential printing)',
                label_width => 0,
            );
            my $option = $optgroup->get_option('between_objects_gcode');
            $option->full_width(1);
            $option->height(150);
            $optgroup->append_single_option_line($option);
        }
    }
    
    {
        my $page = $self->add_options_page('Notes', 'note.png');
        {
            my $optgroup = $page->new_optgroup('Notes',
                label_width => 0,
            );
            my $option = $optgroup->get_option('printer_notes');
            $option->full_width(1);
            $option->height(250);
            $optgroup->append_single_option_line($option);
        }
    }

    $self->{extruder_pages} = [];
    $self->_build_extruder_pages;

    $self->_update_serial_ports if (!$params{no_controller});
}

sub _update_serial_ports {
    my ($self) = @_;
    
    $self->get_field('serial_port')->set_values([ Slic3r::GUI::scan_serial_ports ]);
}

sub _extruders_count_changed {
    my ($self, $extruders_count) = @_;
    $self->{extruders_count} = $extruders_count;
    wxTheApp->{preset_bundle}->printer->get_edited_preset->set_num_extruders($extruders_count);
    wxTheApp->{preset_bundle}->update_multi_material_filament_presets;
    $self->_build_extruder_pages;
    $self->_on_value_change('extruders_count', $extruders_count);
}

sub _build_extruder_pages {
    my ($self) = @_;    
    my $default_config = Slic3r::Config::Full->new;

    foreach my $extruder_idx (@{$self->{extruder_pages}} .. $self->{extruders_count}-1) {
        # build page
        my $page = $self->{extruder_pages}[$extruder_idx] = $self->add_options_page("Extruder " . ($extruder_idx + 1), 'funnel.png');
        {
            my $optgroup = $page->new_optgroup('Size');
            $optgroup->append_single_option_line('nozzle_diameter', $extruder_idx);
        }
        {
            my $optgroup = $page->new_optgroup('Layer height limits');
            $optgroup->append_single_option_line($_, $extruder_idx)
                for qw(min_layer_height max_layer_height);
        }
        {
            my $optgroup = $page->new_optgroup('Position (for multi-extruder printers)');
            $optgroup->append_single_option_line('extruder_offset', $extruder_idx);
        }
        {
            my $optgroup = $page->new_optgroup('Retraction');
            $optgroup->append_single_option_line($_, $extruder_idx)
                for qw(retract_length retract_lift);
            
            {
                my $line = Slic3r::GUI::OptionsGroup::Line->new(
                    label => 'Only lift Z',
                );
                $line->append_option($optgroup->get_option('retract_lift_above', $extruder_idx));
                $line->append_option($optgroup->get_option('retract_lift_below', $extruder_idx));
                $optgroup->append_line($line);
            }
            
            $optgroup->append_single_option_line($_, $extruder_idx)
                for qw(retract_speed deretract_speed retract_restart_extra retract_before_travel retract_layer_change wipe retract_before_wipe);
        }
        {
            my $optgroup = $page->new_optgroup('Retraction when tool is disabled (advanced settings for multi-extruder setups)');
            $optgroup->append_single_option_line($_, $extruder_idx)
                for qw(retract_length_toolchange retract_restart_extra_toolchange);
        }
        {
            my $optgroup = $page->new_optgroup('Preview');
            $optgroup->append_single_option_line('extruder_colour', $extruder_idx);
        }
    }
    
    # remove extra pages
    if ($self->{extruders_count} <= $#{$self->{extruder_pages}}) {
        $_->Destroy for @{$self->{extruder_pages}}[$self->{extruders_count}..$#{$self->{extruder_pages}}];
        splice @{$self->{extruder_pages}}, $self->{extruders_count};
    }
    
    # rebuild page list
    my @pages_without_extruders = (grep $_->{title} !~ /^Extruder \d+/, @{$self->{pages}});
    my $page_notes = pop @pages_without_extruders;
    @{$self->{pages}} = (
        @pages_without_extruders,
        @{$self->{extruder_pages}}[ 0 .. $self->{extruders_count}-1 ],
        $page_notes
    );
    $self->rebuild_page_tree;
}

# Slic3r::GUI::Tab::Printer::_update is called after a configuration preset is loaded or switched, or when a single option is modifed by the user.
sub _update {
    my ($self) = @_;
    $self->Freeze;

    my $config = $self->{config};
    
    my $serial_speed = $self->get_field('serial_speed');
    if ($serial_speed) {
        $self->get_field('serial_speed')->toggle($config->get('serial_port'));
        if ($config->get('serial_speed') && $config->get('serial_port')) {
            $self->{serial_test_btn}->Enable;
        } else {
            $self->{serial_test_btn}->Disable;
        }
    }
    if ($config->get('octoprint_host') && eval "use LWP::UserAgent; 1") {
        $self->{octoprint_host_test_btn}->Enable;
    } else {
        $self->{octoprint_host_test_btn}->Disable;
    }
    $self->get_field('octoprint_apikey')->toggle($config->get('octoprint_host'));
    
    my $have_multiple_extruders = $self->{extruders_count} > 1;
    $self->get_field('toolchange_gcode')->toggle($have_multiple_extruders);
    $self->get_field('single_extruder_multi_material')->toggle($have_multiple_extruders);
    
    for my $i (0 .. ($self->{extruders_count}-1)) {
        my $have_retract_length = $config->get_at('retract_length', $i) > 0;
        
        # when using firmware retraction, firmware decides retraction length
        $self->get_field('retract_length', $i)->toggle(!$config->use_firmware_retraction);
        
        # user can customize travel length if we have retraction length or we're using
        # firmware retraction
        $self->get_field('retract_before_travel', $i)->toggle($have_retract_length || $config->use_firmware_retraction);
        
        # user can customize other retraction options if retraction is enabled
        my $retraction = ($have_retract_length || $config->use_firmware_retraction);
        $self->get_field($_, $i)->toggle($retraction)
            for qw(retract_lift retract_layer_change);
        
        # retract lift above/below only applies if using retract lift
        $self->get_field($_, $i)->toggle($retraction && $config->get_at('retract_lift', $i) > 0)
            for qw(retract_lift_above retract_lift_below);
        
        # some options only apply when not using firmware retraction
        $self->get_field($_, $i)->toggle($retraction && !$config->use_firmware_retraction)
            for qw(retract_speed deretract_speed retract_before_wipe retract_restart_extra wipe);

        my $wipe = $config->get_at('wipe', $i);
        $self->get_field('retract_before_wipe', $i)->toggle($wipe);

        if ($config->use_firmware_retraction && $wipe) {
            my $dialog = Wx::MessageDialog->new($self,
                "The Wipe option is not available when using the Firmware Retraction mode.\n"
                . "\nShall I disable it in order to enable Firmware Retraction?",
                'Firmware Retraction', wxICON_WARNING | wxYES | wxNO);
            
            my $new_conf = Slic3r::Config->new;
            if ($dialog->ShowModal() == wxID_YES) {
                my $wipe = $config->wipe;
                $wipe->[$i] = 0;
                $new_conf->set("wipe", $wipe);
            } else {
                $new_conf->set("use_firmware_retraction", 0);
            }
            $self->load_config($new_conf);
        }
        
        $self->get_field('retract_length_toolchange', $i)->toggle($have_multiple_extruders);
        
        my $toolchange_retraction = $config->get_at('retract_length_toolchange', $i) > 0;
        $self->get_field('retract_restart_extra_toolchange', $i)->toggle
            ($have_multiple_extruders && $toolchange_retraction);
    }

    $self->Thaw;
}

# this gets executed after preset is loaded and before GUI fields are updated
sub on_preset_loaded {
    my ($self) = @_;
    # update the extruders count field
    my $extruders_count = scalar @{ $self->{config}->nozzle_diameter };
    $self->set_value('extruders_count', $extruders_count);
    # update the GUI field according to the number of nozzle diameters supplied
    $self->_extruders_count_changed($extruders_count);
}

# Single Tab page containing a {vsizer} of {optgroups}
package Slic3r::GUI::Tab::Page;
use Wx qw(wxTheApp :misc :panel :sizer);
use base 'Wx::ScrolledWindow';

sub new {
    my ($class, $parent, $title, $iconID) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    $self->{optgroups}  = [];
    $self->{title}      = $title;
    $self->{iconID}     = $iconID;
    
    $self->SetScrollbars(1, 1, 1, 1);
    
    $self->{vsizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->SetSizer($self->{vsizer});
    
    return $self;
}

sub new_optgroup {
    my ($self, $title, %params) = @_;
    
    my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
        parent          => $self,
        title           => $title,
        config          => $self->GetParent->{config},
        label_width     => $params{label_width} // 200,
        on_change       => sub {
            my ($opt_key, $value) = @_;
            wxTheApp->CallAfter(sub {
                $self->GetParent->update_dirty;
                $self->GetParent->_on_value_change($opt_key, $value);
            });
        },
    );
    
    push @{$self->{optgroups}}, $optgroup;
    $self->{vsizer}->Add($optgroup->sizer, 0, wxEXPAND | wxALL, 10);
    
    return $optgroup;
}

sub reload_config {
    my ($self) = @_;
    $_->reload_config for @{$self->{optgroups}};
}

sub get_field {
    my ($self, $opt_key, $opt_index) = @_;
    foreach my $optgroup (@{ $self->{optgroups} }) {
        my $field = $optgroup->get_fieldc($opt_key, $opt_index);
        return $field if defined $field;
    }
    return undef;
}

sub set_value {
    my ($self, $opt_key, $value) = @_;    
    my $changed = 0;
    foreach my $optgroup (@{$self->{optgroups}}) {
        $changed = 1 if $optgroup->set_value($opt_key, $value);
    }
    return $changed;
}

# Dialog to select a new file name for a modified preset to be saved.
# Called from Tab::save_preset().
package Slic3r::GUI::SavePresetWindow;
use Wx qw(:combobox :dialog :id :misc :sizer);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my ($class, $parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Save preset", wxDefaultPosition, wxDefaultSize);
    
    my @values = @{$params{values}};
    
    my $text = Wx::StaticText->new($self, -1, "Save " . lc($params{title}) . " as:", wxDefaultPosition, wxDefaultSize);
    $self->{combo} = Wx::ComboBox->new($self, -1, $params{default}, wxDefaultPosition, wxDefaultSize, \@values,
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
    if (($self->{chosen_name} = Slic3r::normalize_utf8_nfc($self->{combo}->GetValue))) {
        if ($self->{chosen_name} !~ /^[^<>:\/\\|?*\"]+$/) {
            Slic3r::GUI::show_error($self, "The supplied name is not valid; the following characters are not allowed: <>:/\|?*\"");
        } elsif ($self->{chosen_name} eq '- default -') {
            Slic3r::GUI::show_error($self, "The supplied name is not available.");
        } else {
            $self->EndModal(wxID_OK);
        }
    }
}

sub get_name {
    my ($self) = @_;
    return $self->{chosen_name};
}

1;
