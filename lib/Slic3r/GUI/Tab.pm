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
    :button wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_CHOICE EVT_KEY_DOWN EVT_CHECKBOX EVT_TREE_SEL_CHANGED);
use base qw(Wx::Panel Class::Accessor);

# Index of the currently active preset.
__PACKAGE__->mk_accessors(qw(current_preset));

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    
    # Vertical sizer to hold the choice menu and the rest of the page.
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->{sizer}->SetSizeHints($self);
    $self->SetSizer($self->{sizer});
    
    # preset chooser
    {
        
        # choice menu
        $self->{presets_choice} = Wx::Choice->new($self, -1, wxDefaultPosition, [270, -1], []);
        $self->{presets_choice}->SetFont($Slic3r::GUI::small_font);
        
        # buttons
        $self->{btn_save_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new($Slic3r::var->("disk.png"), wxBITMAP_TYPE_PNG), 
            wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        $self->{btn_delete_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new($Slic3r::var->("delete.png"), wxBITMAP_TYPE_PNG), 
            wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        $self->{btn_save_preset}->SetToolTipString("Save current " . lc($self->title));
        $self->{btn_delete_preset}->SetToolTipString("Delete this preset");
        $self->{btn_delete_preset}->Disable;
        
        my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);        
        $self->{sizer}->Add($hsizer, 0, wxBOTTOM, 3);
        $hsizer->Add($self->{presets_choice}, 1, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
        $hsizer->Add($self->{btn_save_preset}, 0, wxALIGN_CENTER_VERTICAL);
        $hsizer->Add($self->{btn_delete_preset}, 0, wxALIGN_CENTER_VERTICAL);
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
    $self->{treectrl}->AssignImageList($self->{icons});
    $self->{iconcount} = -1;
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
    
    EVT_CHOICE($parent, $self->{presets_choice}, sub {
        $self->on_select_preset;
        $self->_on_presets_changed;
    });
    
    EVT_BUTTON($self, $self->{btn_save_preset}, sub { $self->save_preset });
    
    EVT_BUTTON($self, $self->{btn_delete_preset}, sub {
        my $i = $self->current_preset;
        # Don't let the user delete the '- default -' configuration.
        # This shall not happen as the 'delete' button is disabled for the '- default -' entry,
        # but better be safe than sorry.
        return if ($i == 0 && $self->get_current_preset->{default});
        my $res = Wx::MessageDialog->new($self, "Are you sure you want to delete the selected preset?", 'Delete Preset', wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION)->ShowModal;
        return unless $res == wxID_YES;
        # Delete the file.
        my $path = $self->{presets}[$i]->file;
        my $enc_path = Slic3r::encode_path($path);
        if (-e $enc_path && ! unlink $enc_path) {
            # Cannot delete the file, therefore the item will not be removed from the selection.
            Slic3r::GUI::show_error($self, "Cannot delete file $path : $!");
            return;
        }
        # Delete the preset.
        splice @{$self->{presets}}, $i, 1;
        # Delete the item from the UI component.
        $self->{presets_choice}->Delete($i - $self->{default_suppressed});
        $self->current_preset(undef);
        if ($self->{default_suppressed} && scalar(@{$self->{presets}}) == 1) {
            # Empty selection. Add the '- default -' item into the drop down selection.
            $self->{presets_choice}->Append($self->{presets}->[0]->name);
            # and remember that the '- default -' is shown.
            $self->{default_suppressed} = 0;
        }
        # Select the 0th item. If default is suppressed, select the first valid.
        $self->select_preset($self->{default_suppressed});
        $self->_on_presets_changed;
    });
    
    # C++ instance DynamicPrintConfig
    $self->{config} = Slic3r::Config->new;
    # Initialize the DynamicPrintConfig by default keys/values.
    # Possible %params keys: no_controller
    $self->build(%params);
    $self->update_tree;
    $self->_update;
    if ($self->hidden_options) {
        $self->{config}->apply(Slic3r::Config->new_from_defaults($self->hidden_options));
    }
    
    return $self;
}

# Are the '- default -' selections suppressed by the Slic3r GUI preferences?
sub no_defaults {
    return $Slic3r::GUI::Settings->{_}{no_defaults} ? 1 : 0;
}

# Get a currently active preset (Perl class Slic3r::GUI::Tab::Preset).
sub get_current_preset {
    my $self = shift;
    return $self->get_preset($self->current_preset);
}

# Get a preset (Perl class Slic3r::GUI::Tab::Preset) with an index $i.
sub get_preset {
    my ($self, $i) = @_;
    return $self->{presets}[$i];
}

sub save_preset {
    my ($self, $name) = @_;
    
    # since buttons (and choices too) don't get focus on Mac, we set focus manually
    # to the treectrl so that the EVT_* events are fired for the input field having
    # focus currently. is there anything better than this?
    $self->{treectrl}->SetFocus;
    
    if (!defined $name) {
        my $preset = $self->get_current_preset;
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
    
    $self->config->save(sprintf "$Slic3r::GUI::datadir/%s/%s.ini", $self->name, $name);
    $self->load_presets;
    $self->select_preset_by_name($name);
    $self->_on_presets_changed;
}

sub on_value_change {
    my ($self, $cb) = @_;
    $self->{on_value_change} = $cb;
}

sub on_presets_changed {
    my ($self, $cb) = @_;
    $self->{on_presets_changed} = $cb;
}

# This method is called whenever an option field is changed by the user.
# Propagate event to the parent through the 'on_value_changed' callback
# and call _update.
sub _on_value_change {
    my ($self, $key, $value) = @_;
    $self->{on_value_change}->($key, $value) if $self->{on_value_change};
    $self->_update({ $key => 1 });
}

# Override this to capture changes of configuration caused either by loading or switching a preset,
# or by a user changing an option field.
sub _update {}

sub _on_presets_changed {
    my $self = shift;
    
    $self->{on_presets_changed}->(
        $self->{presets},
        $self->{default_suppressed},
        scalar($self->{presets_choice}->GetSelection) + $self->{default_suppressed},
        $self->is_dirty,
    ) if $self->{on_presets_changed};
}

sub on_preset_loaded {}
sub hidden_options {}
sub config { $_[0]->{config}->clone }

sub select_default_preset {
    my $self = shift;
    $self->select_preset(0);
}

sub select_preset {
    my ($self, $i) = @_;
    if ($self->{default_suppressed} && $i == 0) {
        # Selecting the '- default -'. Add it to the combo box.
        $self->{default_suppressed} = 0;
        $self->{presets_choice}->Insert($self->{presets}->[0]->name, 0);
    } elsif ($self->no_defaults && ! $self->{default_suppressed} && $i > 0) {
        # The user wants to hide the '- default -' items and a non-default item has been added to the presets.
        # Hide the '- default -' item.
        $self->{presets_choice}->Delete(0);
        $self->{default_suppressed} = 1;
    }
    $self->{presets_choice}->SetSelection($i - $self->{default_suppressed});
    $self->on_select_preset;
}

sub select_preset_by_name {
    my ($self, $name) = @_;
    
    $name = Slic3r::normalize_utf8_nfc($name);
    $self->select_preset(first { $self->{presets}[$_]->name eq $name } 0 .. $#{$self->{presets}});
}

sub on_select_preset {
    my $self = shift;
    
    if ($self->is_dirty) {
        # Display a dialog showing the dirty options in a human readable form.
        my $old_preset = $self->get_current_preset;
        my $name = $old_preset->default ? 'Default preset' : "Preset \"" . $old_preset->name . "\"";
        
        my @option_names = ();
        foreach my $opt_key (@{$self->dirty_options}) {
            my $opt = $Slic3r::Config::Options->{$opt_key};
            my $name = $opt->{full_label} // $opt->{label};
            if ($opt->{category}) {
                $name = $opt->{category} . " > $name";
            }
            push @option_names, $name;
        }
        
        my $changes = join "\n", map "- $_", @option_names;
        my $confirm = Wx::MessageDialog->new($self, "$name has unsaved changes:\n$changes\n\nDiscard changes and continue anyway?",
                                             'Unsaved Changes', wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
        if ($confirm->ShowModal == wxID_NO) {
            $self->{presets_choice}->SetSelection($self->current_preset - $self->{default_suppressed});
            
            # trigger the on_presets_changed event so that we also restore the previous value
            # in the plater selector
            $self->_on_presets_changed;
            return;
        }
    }
    
    $self->current_preset($self->{presets_choice}->GetSelection + $self->{default_suppressed});
    my $preset = $self->get_current_preset;
    my $preset_config = $self->get_preset_config($preset);
    eval {
        local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
        my %keys_modified = ();
        foreach my $opt_key (@{$self->{config}->get_keys}) {
            if ($preset_config->has($opt_key)) {
                if ($self->{config}->serialize($opt_key) ne $preset_config->serialize($opt_key)) {
                    $self->{config}->set($opt_key, $preset_config->get($opt_key));
                    $keys_modified{$opt_key} = 1;
                }
            }
        }
        ($preset->default || $preset->external)
            ? $self->{btn_delete_preset}->Disable
            : $self->{btn_delete_preset}->Enable;
        
        $self->_update(\%keys_modified);
        $self->on_preset_loaded;
        $self->reload_config;
        $Slic3r::GUI::Settings->{presets}{$self->name} = $preset->file ? basename($preset->file) : '';
    };
    if ($@) {
        $@ = "I was unable to load the selected config file: $@";
        Slic3r::GUI::catch_error($self);
        $self->select_default_preset;
    }
        
    # use CallAfter because some field triggers schedule on_change calls using CallAfter,
    # and we don't want them to be called after this update_dirty() as they would mark the 
    # preset dirty again
    # (not sure this is true anymore now that update_dirty is idempotent)
    wxTheApp->CallAfter(sub {
        $self->_on_presets_changed;
        $self->update_dirty;
    });
    
    wxTheApp->save_settings;
}

sub init_config_options {
    my ($self, @opt_keys) = @_;
    $self->{config}->apply(Slic3r::Config->new_from_defaults(@opt_keys));
}

sub add_options_page {
    my $self = shift;
    my ($title, $icon, %params) = @_;
    
    if ($icon) {
        my $bitmap = Wx::Bitmap->new($Slic3r::var->($icon), wxBITMAP_TYPE_PNG);
        $self->{icons}->Add($bitmap);
        $self->{iconcount}++;
    }
    
    my $page = Slic3r::GUI::Tab::Page->new($self, $title, $self->{iconcount});
    $page->Hide;
    $self->{hsizer}->Add($page, 1, wxEXPAND | wxLEFT, 5);
    push @{$self->{pages}}, $page;
    return $page;
}

sub reload_config {
    my $self = shift;
    $_->reload_config for @{$self->{pages}};
}

sub update_tree {
    my ($self) = @_;
    
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
}

# Update the combo box label of the selected preset based on its "dirty" state,
# comparing the selected preset config with $self->{config}.
sub update_dirty {
    my ($self) = @_;
    my $list_updated;
    foreach my $i ($self->{default_suppressed}..$#{$self->{presets}}) {
        my $preset = $self->get_preset($i);
        my $label  = ($i == $self->current_preset && $self->is_dirty) ? $preset->name . " (modified)" : $preset->name;
        my $idx    = $i - $self->{default_suppressed};
        if ($self->{presets_choice}->GetString($idx) ne $label) {
            $self->{presets_choice}->SetString($idx, $label);
            $list_updated = 1;
        }
    }
    $self->{presets_choice}->SetSelection($self->current_preset - $self->{default_suppressed}) if ($list_updated);  # http://trac.wxwidgets.org/ticket/13769
    $self->_on_presets_changed;
}

# Has the selected preset been modified?
sub is_dirty {
    my ($self) = @_;
    return @{$self->dirty_options} > 0;
}

# Which options of the selected preset were modified?
sub dirty_options {
    my ($self) = @_;
    return [] if !defined $self->current_preset;  # happens during initialization
    return $self->get_preset_config($self->get_current_preset)->diff($self->{config});
}

# Search all ini files in the presets directory, add them into the list of $self->{presets} in the form of Slic3r::GUI::Tab::Preset.
# Initialize the drop down list box.
sub load_presets {
    my ($self) = @_;
    
    $self->{presets} = [
        Slic3r::GUI::Tab::Preset->new(
            default => 1,
            name    => '- default -',
        ),
    ];
    
    my %presets = wxTheApp->presets($self->name);
    foreach my $preset_name (sort keys %presets) {
        push @{$self->{presets}}, Slic3r::GUI::Tab::Preset->new(
            name => $preset_name,
            file => $presets{$preset_name},
        );
    }
    $self->current_preset(undef);
    $self->{default_suppressed} = Slic3r::GUI::Tab->no_defaults && scalar(@{$self->{presets}}) > 1;
    $self->{presets_choice}->Clear;
    foreach my $preset (@{$self->{presets}}) {
        next if ($preset->default && $self->{default_suppressed});
        $self->{presets_choice}->Append($preset->name);
    }
    {
        # load last used preset
        my $i = first { basename($self->{presets}[$_]->file) eq ($Slic3r::GUI::Settings->{presets}{$self->name} || '') } 1 .. $#{$self->{presets}};
        $self->select_preset($i || $self->{default_suppressed});
    }
    $self->_on_presets_changed;
}

# Load a config file containing a Print, Filament & Printer preset.
sub load_config_file {
    my ($self, $file) = @_;
    # look for the loaded config among the existing menu items
    my $i = first { $self->{presets}[$_]{file} eq $file && $self->{presets}[$_]{external} } 1..$#{$self->{presets}};
    if (!$i) {
        my $preset_name = basename($file);  # keep the .ini suffix
        my $preset_new = Slic3r::GUI::Tab::Preset->new(
            file        => $file,
            name        => $preset_name,
            external    => 1,
        );
        # Try to load the config file before it is entered into the list. If the loading fails, an undef is returned.
        return undef if ! defined $preset_new->config;
        push @{$self->{presets}}, $preset_new;
        $self->{presets_choice}->Append($preset_name);
        $i = $#{$self->{presets}};
    }
    $self->{presets_choice}->SetSelection($i - $self->{default_suppressed});
    $self->on_select_preset;
    $self->_on_presets_changed;
    return 1;
}

# Load a provied DynamicConfig into the tab, modifying the active preset.
# This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
sub load_config {
    my ($self, $config) = @_;
    
    my %keys_modified = ();
    foreach my $opt_key (@{$self->{config}->diff($config)}) {
        $self->{config}->set($opt_key, $config->get($opt_key));
        $keys_modified{$opt_key} = 1;
    }
    if (keys(%keys_modified)) {
        $self->update_dirty;
        # Initialize UI components with the config values.
        $self->reload_config;
        $self->_update(\%keys_modified);
    }
}

# Load and return a config from the file associated with the $preset (Perl type Slic3r::GUI::Tab::Preset).
sub get_preset_config {
    my ($self, $preset) = @_;
    return $preset->config($self->{config}->get_keys);
}

sub get_field {
    my ($self, $opt_key, $opt_index) = @_;
    
    foreach my $page (@{ $self->{pages} }) {
        my $field = $page->get_field($opt_key, $opt_index);
        return $field if defined $field;
    }
    return undef;
}

sub set_value {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    my $changed = 0;
    foreach my $page (@{ $self->{pages} }) {
        $changed = 1 if $page->set_value($opt_key, $value);
    }
    return $changed;
}

package Slic3r::GUI::Tab::Print;
use base 'Slic3r::GUI::Tab';

use List::Util qw(first);
use Wx qw(:icon :dialog :id wxTheApp);

sub name { 'print' }
sub title { 'Print Settings' }

sub build {
    my $self = shift;
    
    $self->{config}->apply(wxTheApp->{preset_bundle}->prints->default_preset->config);
    
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
#            $optgroup->append_single_option_line('threads') if $Slic3r::have_threads;
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
}

sub reload_config {
    my ($self) = @_;
#    $self->_reload_compatible_printers_widget;
    $self->SUPER::reload_config;
}

# Slic3r::GUI::Tab::Print::_update is called after a configuration preset is loaded or switched, or when a single option is modifed by the user.
sub _update {
    # $keys_modified is a reference to hash with modified keys set to 1, unmodified keys missing.
    my ($self, $keys_modified) = @_;
    $keys_modified //= {};

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

    if ($keys_modified->{'layer_height'}) {
        # If the user had set the variable layer height, reset it and let the user know.
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
}

#sub hidden_options { !$Slic3r::have_threads ? qw(threads) : () }

package Slic3r::GUI::Tab::Filament;
use base 'Slic3r::GUI::Tab';
use Wx qw(wxTheApp);

sub name { 'filament' }
sub title { 'Filament Settings' }

sub build {
    my $self = shift;
    
    $self->{config}->apply(wxTheApp->{preset_bundle}->filaments->default_preset->config);
    
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
                    return $self->{description_line} = Slic3r::GUI::OptionsGroup::StaticText->new($parent);
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
}

# Slic3r::GUI::Tab::Filament::_update is called after a configuration preset is loaded or switched, or when a single option is modifed by the user.
sub _update {
    # $keys_modified is a reference to hash with modified keys set to 1, unmodified keys missing.
    my ($self, $keys_modified) = @_;
    
    $self->_update_description;
    
    my $cooling = $self->{config}->cooling->[0];
    my $fan_always_on = $cooling || $self->{config}->fan_always_on->[0];
    $self->get_field($_, 0)->toggle($cooling)
        for qw(max_fan_speed fan_below_layer_time slowdown_below_layer_time min_print_speed);
    $self->get_field($_, 0)->toggle($fan_always_on)
        for qw(min_fan_speed disable_fan_first_layers);
}

sub _update_description {
    my $self = shift;
    
    my $config = $self->config;
    
    my $msg = "";
    my $fan_other_layers = $config->fan_always_on->[0]
        ? sprintf "will always run at %d%%%s.", $config->min_fan_speed->[0],
                ($config->disable_fan_first_layers->[0] > 1
                    ? " except for the first " . $config->disable_fan_first_layers->[0] . " layers"
                    : $config->disable_fan_first_layers->[0] == 1
                        ? " except for the first layer"
                        : "")
        : "will be turned off.";
    
    if ($config->cooling->[0]) {
        $msg = sprintf "If estimated layer time is below ~%ds, fan will run at %d%% and print speed will be reduced so that no less than %ds are spent on that layer (however, speed will never be reduced below %dmm/s).",
            $config->slowdown_below_layer_time->[0], $config->max_fan_speed->[0], $config->slowdown_below_layer_time->[0], $config->min_print_speed->[0];
        if ($config->fan_below_layer_time->[0] > $config->slowdown_below_layer_time->[0]) {
            $msg .= sprintf "\nIf estimated layer time is greater, but still below ~%ds, fan will run at a proportionally decreasing speed between %d%% and %d%%.",
                $config->fan_below_layer_time->[0], $config->max_fan_speed->[0], $config->min_fan_speed->[0];
        }
        $msg .= "\nDuring the other layers, fan $fan_other_layers"
    } else {
        $msg = "Fan $fan_other_layers";
    }
    $self->{description_line}->SetText($msg);
}

package Slic3r::GUI::Tab::Printer;
use base 'Slic3r::GUI::Tab';
use Wx qw(wxTheApp :sizer :button :bitmap :misc :id :icon :dialog);
use Wx::Event qw(EVT_BUTTON);

sub name { 'printer' }
sub title { 'Printer Settings' }

sub build {
    my $self = shift;
    my (%params) = @_;
    
    $self->{config}->apply(wxTheApp->{preset_bundle}->printers->default_preset->config);
    
    my $bed_shape_widget = sub {
        my ($parent) = @_;
        
        my $btn = Wx::Button->new($parent, -1, "Set…", wxDefaultPosition, wxDefaultSize,
            wxBU_LEFT | wxBU_EXACTFIT);
        $btn->SetFont($Slic3r::GUI::small_font);
        $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("cog.png"), wxBITMAP_TYPE_PNG));
        
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($btn);
        
        EVT_BUTTON($self, $btn, sub {
            my $dlg = Slic3r::GUI::BedShapeDialog->new($self, $self->{config}->bed_shape);
            if ($dlg->ShowModal == wxID_OK) {
                my $value = $dlg->GetValue;
                $self->{config}->set('bed_shape', $value);
                $self->update_dirty;
                $self->_on_value_change('bed_shape', $value);
            }
        });
        
        return $sizer;
    };
    
    $self->{extruders_count} = 1;
    
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
                my ($opt_id) = @_;
                if ($opt_id eq 'extruders_count') {
                    wxTheApp->CallAfter(sub {
                        $self->_extruders_count_changed($optgroup->get_value('extruders_count'));
                    });
                    $self->update_dirty;
                }
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
                
                my $btn = Wx::BitmapButton->new($parent, -1, Wx::Bitmap->new($Slic3r::var->("arrow_rotate_clockwise.png"), wxBITMAP_TYPE_PNG),
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
                $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("wrench.png"), wxBITMAP_TYPE_PNG));
                
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
                $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("zoom.png"), wxBITMAP_TYPE_PNG));
                
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
                        if ($dlg->ShowModal == wxID_OK) {
                            my $value = $dlg->GetValue . ":" . $dlg->GetPort;
                            $self->{config}->set('octoprint_host', $value);
                            $self->update_dirty;
                            $self->_on_value_change('octoprint_host', $value);
                            $self->reload_config;
                        }
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
                $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("wrench.png"), wxBITMAP_TYPE_PNG));
                
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
    
    $self->get_field('serial_port')->set_values([ wxTheApp->scan_serial_ports ]);
}

sub _extruders_count_changed {
    my ($self, $extruders_count) = @_;
    
    $self->{extruders_count} = $extruders_count;
    $self->_build_extruder_pages;
    $self->_on_value_change('extruders_count', $extruders_count);
}

sub _extruder_options { 
    qw(nozzle_diameter min_layer_height max_layer_height extruder_offset 
       retract_length retract_lift retract_lift_above retract_lift_below retract_speed deretract_speed 
       retract_before_wipe retract_restart_extra retract_before_travel wipe
       retract_layer_change retract_length_toolchange retract_restart_extra_toolchange extruder_colour) }

sub _build_extruder_pages {
    my $self = shift;
    
    my $default_config = Slic3r::Config::Full->new;
    
    foreach my $extruder_idx (@{$self->{extruder_pages}} .. $self->{extruders_count}-1) {
        # extend options
        foreach my $opt_key ($self->_extruder_options) {
            my $values = $self->{config}->get($opt_key);
            if (!defined $values) {
                $values = [ $default_config->get_at($opt_key, 0) ];
            } else {
                # use last extruder's settings for the new one
                my $last_value = $values->[-1];
                $values->[$extruder_idx] //= $last_value;
            }
            $self->{config}->set($opt_key, $values)
                or die "Unable to extend $opt_key";
        }
        
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
    
    # remove extra config values
    foreach my $opt_key ($self->_extruder_options) {
        my $values = $self->{config}->get($opt_key);
        splice @$values, $self->{extruders_count} if $self->{extruders_count} <= $#$values;
        $self->{config}->set($opt_key, $values)
            or die "Unable to truncate $opt_key";
    }
    
    # rebuild page list
    my @pages_without_extruders = (grep $_->{title} !~ /^Extruder \d+/, @{$self->{pages}});
    my $page_notes = pop @pages_without_extruders;
    @{$self->{pages}} = (
        @pages_without_extruders,
        @{$self->{extruder_pages}}[ 0 .. $self->{extruders_count}-1 ],
        $page_notes
    );
    $self->update_tree;
}

# Slic3r::GUI::Tab::Printer::_update is called after a configuration preset is loaded or switched, or when a single option is modifed by the user.
sub _update {
    # $keys_modified is a reference to hash with modified keys set to 1, unmodified keys missing.
    my ($self, $keys_modified) = @_;
    
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
}

# this gets executed after preset is loaded and before GUI fields are updated
sub on_preset_loaded {
    my $self = shift;
    
    # update the extruders count field
    {
        # update the GUI field according to the number of nozzle diameters supplied
        my $extruders_count = scalar @{ $self->{config}->nozzle_diameter };
        $self->set_value('extruders_count', $extruders_count);
        $self->_extruders_count_changed($extruders_count);
    }
}

# Load a config file containing a Print, Filament & Printer preset.
sub load_config_file {
    my $self = shift;
    if ($self->SUPER::load_config_file(@_)) {
        Slic3r::GUI::warning_catcher($self)->(
            "Your configuration was imported. However, Slic3r is currently only able to import settings "
            . "for the first defined filament. We recommend you don't use exported configuration files "
            . "for multi-extruder setups and rely on the built-in preset management system instead.")
            if @{ $self->{config}->nozzle_diameter } > 1;
        return 1;
    }
    return undef;
}

package Slic3r::GUI::Tab::Page;
use Wx qw(wxTheApp :misc :panel :sizer);
use base 'Wx::ScrolledWindow';

sub new {
    my $class = shift;
    my ($parent, $title, $iconID) = @_;
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

    if (($self->{chosen_name} = $self->{combo}->GetValue)) {
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
    my $self = shift;
    return $self->{chosen_name};
}

package Slic3r::GUI::Tab::Preset;
use Moo;
use List::Util qw(any);

# The preset represents a "default" set of properties.
has 'default'   => (is => 'ro', default => sub { 0 });
has 'external'  => (is => 'ro', default => sub { 0 });
has 'name'      => (is => 'rw', required => 1);
has 'file'      => (is => 'rw');

# Load a config file, return a C++ class Slic3r::DynamicPrintConfig with $keys initialized from the config file.
# In case of a "default" config item, return the default values.
sub config {
    my ($self, $keys) = @_;
    
    if ($self->default) {
        # Perl class Slic3r::Config extends the C++ class Slic3r::DynamicPrintConfig
        return Slic3r::Config->new_from_defaults(@$keys);
    } else {
        if (!-e Slic3r::encode_path($self->file)) {
            Slic3r::GUI::show_error(undef, "The selected preset does not exist anymore (" . $self->file . ").");
            return undef;
        }
        
        # apply preset values on top of defaults
        my $config = Slic3r::Config->new_from_defaults(@$keys);
        my $external_config = eval { Slic3r::Config->load($self->file); };
        if ($@) {
            Slic3r::GUI::show_error(undef, $@);
            return undef;
        }
        $config->set($_, $external_config->get($_))
            for grep $external_config->has($_), @$keys;

        if (any { $_ eq 'nozzle_diameter' } @$keys) {
            # Loaded the Printer settings. Verify, that all extruder dependent values have enough values.
            my $nozzle_diameter     = $config->nozzle_diameter;
            my $num_extruders       = scalar(@{$nozzle_diameter});
            foreach my $key (qw(deretract_speed extruder_colour retract_before_wipe)) {
                my $vec = $config->get($key);
                push @{$vec}, ($vec->[0]) x ($num_extruders - @{$vec});
                $config->set($key, $vec);
            }
        }
        
        return $config;
    }
}

1;
