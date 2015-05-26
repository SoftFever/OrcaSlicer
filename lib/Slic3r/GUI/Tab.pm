package Slic3r::GUI::Tab;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use List::Util qw(first);
use Wx qw(:bookctrl :dialog :keycode :icon :id :misc :panel :sizer :treectrl :window
    :button wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_CHOICE EVT_KEY_DOWN EVT_TREE_SEL_CHANGED);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(current_preset));

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    
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
        $self->{btn_save_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/disk.png", wxBITMAP_TYPE_PNG), 
            wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        $self->{btn_delete_preset} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG), 
            wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
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
        $self->_on_presets_changed;
    });
    
    EVT_BUTTON($self, $self->{btn_save_preset}, sub { $self->save_preset });
    
    EVT_BUTTON($self, $self->{btn_delete_preset}, sub {
        my $i = $self->current_preset;
        return if $i == 0;  # this shouldn't happen but let's trap it anyway
        my $res = Wx::MessageDialog->new($self, "Are you sure you want to delete the selected preset?", 'Delete Preset', wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION)->ShowModal;
        return unless $res == wxID_YES;
        if (-e $self->{presets}[$i]->file) {
            unlink $self->{presets}[$i]->file;
        }
        splice @{$self->{presets}}, $i, 1;
        $self->{presets_choice}->Delete($i);
        $self->current_preset(undef);
        $self->select_preset(0);
        $self->_on_presets_changed;
    });
    
    $self->{config} = Slic3r::Config->new;
    $self->build;
    $self->_update;
    if ($self->hidden_options) {
        $self->{config}->apply(Slic3r::Config->new_from_defaults($self->hidden_options));
    }
    
    return $self;
}

sub get_current_preset {
    my $self = shift;
    return $self->get_preset($self->current_preset);
}

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
        $default_name =~ s/\.ini$//i;
    
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

# This method is supposed to be called whenever new values are loaded
# or changed by user (so also when a preset is loaded).
# propagate event to the parent
sub _on_value_change {
    my $self = shift;
    
    $self->{on_value_change}->(@_) if $self->{on_value_change};
    $self->_update;
}

sub _update {}

sub _on_presets_changed {
    my $self = shift;
    
    $self->{on_presets_changed}->($self->{presets}, $self->{presets_choice}->GetSelection)
        if $self->{on_presets_changed};
}

sub on_preset_loaded {}
sub hidden_options {}
sub config { $_[0]->{config}->clone }

sub select_default_preset {
    my $self = shift;
    $self->select_preset(0);
}

sub select_preset {
    my $self = shift;
    $self->{presets_choice}->SetSelection($_[0]);
    $self->on_select_preset;
}

sub select_preset_by_name {
    my ($self, $name) = @_;
    $self->select_preset(first { $self->{presets}[$_]->name eq $name } 0 .. $#{$self->{presets}});
}

sub on_select_preset {
    my $self = shift;
    
    if ($self->is_dirty) {
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
            $self->{presets_choice}->SetSelection($self->current_preset);
            return;
        }
    }
    
    $self->current_preset($self->{presets_choice}->GetSelection);
    my $preset = $self->get_current_preset;
    my $preset_config = $self->get_preset_config($preset);
    eval {
        local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
        foreach my $opt_key (@{$self->{config}->get_keys}) {
            $self->{config}->set($opt_key, $preset_config->get($opt_key))
                if $preset_config->has($opt_key);
        }
        ($preset->default || $preset->external)
            ? $self->{btn_delete_preset}->Disable
            : $self->{btn_delete_preset}->Enable;
        
        $self->_update;
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
        my $bitmap = Wx::Bitmap->new("$Slic3r::var/$icon", wxBITMAP_TYPE_PNG);
        $self->{icons}->Add($bitmap);
        $self->{iconcount}++;
    }
    
    my $page = Slic3r::GUI::Tab::Page->new($self, $title, $self->{iconcount});
    $page->Hide;
    $self->{sizer}->Add($page, 1, wxEXPAND | wxLEFT, 5);
    push @{$self->{pages}}, $page;
    $self->update_tree;
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
            $self->{treectrl}->SelectItem($itemId);
            $have_selection = 1;
        }
    }
    
    if (!$have_selection) {
        $self->{treectrl}->SelectItem($self->{treectrl}->GetFirstChild($rootItem));
    }
}

sub update_dirty {
    my $self = shift;
    
    foreach my $i (0..$#{$self->{presets}}) {
        my $preset = $self->get_preset($i);
        if ($i == $self->current_preset && $self->is_dirty) {
            $self->{presets_choice}->SetString($i, $preset->name . " (modified)");
        } else {
            $self->{presets_choice}->SetString($i, $preset->name);
        }
    }
    $self->{presets_choice}->SetSelection($self->current_preset);  # http://trac.wxwidgets.org/ticket/13769
    $self->_on_presets_changed;
}

sub is_dirty {
    my $self = shift;
    return @{$self->dirty_options} > 0;
}

sub dirty_options {
    my $self = shift;
    
    return [] if !defined $self->current_preset;  # happens during initialization
    return $self->get_preset_config($self->get_current_preset)->diff($self->{config});
}

sub load_presets {
    my $self = shift;
    
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
    $self->{presets_choice}->Clear;
    $self->{presets_choice}->Append($_->name) for @{$self->{presets}};
    {
        # load last used preset
        my $i = first { basename($self->{presets}[$_]->file) eq ($Slic3r::GUI::Settings->{presets}{$self->name} || '') } 1 .. $#{$self->{presets}};
        $self->select_preset($i || 0);
    }
    $self->_on_presets_changed;
}

sub load_config_file {
    my $self = shift;
    my ($file) = @_;
    
    # look for the loaded config among the existing menu items
    my $i = first { $self->{presets}[$_]{file} eq $file && $self->{presets}[$_]{external} } 1..$#{$self->{presets}};
    if (!$i) {
        my $preset_name = basename($file);  # keep the .ini suffix
        push @{$self->{presets}}, Slic3r::GUI::Tab::Preset->new(
            file        => $file,
            name        => $preset_name,
            external    => 1,
        );
        $self->{presets_choice}->Append($preset_name);
        $i = $#{$self->{presets}};
    }
    $self->{presets_choice}->SetSelection($i);
    $self->on_select_preset;
    $self->_on_presets_changed;
}

sub load_config {
    my $self = shift;
    my ($config) = @_;
    
    foreach my $opt_key (@{$self->{config}->diff($config)}) {
        $self->{config}->set($opt_key, $config->get($opt_key));
        $self->update_dirty;
    }
    $self->reload_config;
    $self->_update;
}

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

use Wx qw(:icon :dialog :id);

sub name { 'print' }
sub title { 'Print Settings' }

sub build {
    my $self = shift;
    
    $self->init_config_options(qw(
        layer_height first_layer_height
        perimeters spiral_vase
        top_solid_layers bottom_solid_layers
        extra_perimeters avoid_crossing_perimeters thin_walls overhangs
        seam_position external_perimeters_first
        fill_density fill_pattern external_fill_pattern
        infill_every_layers infill_only_where_needed
        solid_infill_every_layers fill_angle solid_infill_below_area 
        only_retract_when_crossing_perimeters infill_first
        perimeter_speed small_perimeter_speed external_perimeter_speed infill_speed 
        solid_infill_speed top_solid_infill_speed support_material_speed 
        support_material_interface_speed bridge_speed gap_fill_speed
        travel_speed
        first_layer_speed
        perimeter_acceleration infill_acceleration bridge_acceleration 
        first_layer_acceleration default_acceleration
        skirts skirt_distance skirt_height min_skirt_length
        brim_width
        support_material support_material_threshold support_material_enforce_layers
        raft_layers
        support_material_pattern support_material_spacing support_material_angle
        support_material_interface_layers support_material_interface_spacing
        support_material_contact_distance dont_support_bridges
        notes
        complete_objects extruder_clearance_radius extruder_clearance_height
        gcode_comments output_filename_format
        post_process
        perimeter_extruder infill_extruder solid_infill_extruder
        support_material_extruder support_material_interface_extruder
        ooze_prevention standby_temperature_delta
        interface_shells
        extrusion_width first_layer_extrusion_width perimeter_extrusion_width 
        external_perimeter_extrusion_width infill_extrusion_width solid_infill_extrusion_width 
        top_infill_extrusion_width support_material_extrusion_width
        infill_overlap bridge_flow_ratio
        xy_size_compensation threads resolution
    ));
    
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
        my $page = $self->add_options_page('Infill', 'shading.png');
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
        }
        {
            my $optgroup = $page->new_optgroup('Options for support material and raft');
            $optgroup->append_single_option_line('support_material_contact_distance');
            $optgroup->append_single_option_line('support_material_pattern');
            $optgroup->append_single_option_line('support_material_spacing');
            $optgroup->append_single_option_line('support_material_angle');
            $optgroup->append_single_option_line('support_material_interface_layers');
            $optgroup->append_single_option_line('support_material_interface_spacing');
            $optgroup->append_single_option_line('dont_support_bridges');
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
            $optgroup->append_single_option_line('xy_size_compensation');
            $optgroup->append_single_option_line('threads') if $Slic3r::have_threads;
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

sub _update {
    my ($self) = @_;
    
    my $config = $self->{config};
    
    if ($config->spiral_vase && !($config->perimeters == 1 && $config->top_solid_layers == 0 && $config->fill_density == 0)) {
        my $dialog = Wx::MessageDialog->new($self,
            "The Spiral Vase mode requires:\n"
            . "- one perimeter\n"
            . "- no top solid layers\n"
            . "- 0% fill density\n"
            . "- no support material\n"
            . "\nShall I adjust those settings in order to enable Spiral Vase?",
            'Spiral Vase', wxICON_WARNING | wxYES | wxNO);
        if ($dialog->ShowModal() == wxID_YES) {
            my $new_conf = Slic3r::Config->new;
            $new_conf->set("perimeters", 1);
            $new_conf->set("top_solid_layers", 0);
            $new_conf->set("fill_density", 0);
            $new_conf->set("support_material", 0);
            $self->load_config($new_conf);
        } else {
            my $new_conf = Slic3r::Config->new;
            $new_conf->set("spiral_vase", 0);
            $self->load_config($new_conf);
        }
    }
    
    my $have_perimeters = $config->perimeters > 0;
    $self->get_field($_)->toggle($have_perimeters)
        for qw(extra_perimeters thin_walls overhangs seam_position external_perimeters_first
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
        for qw(fill_angle infill_extrusion_width infill_speed bridge_speed);
    
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
    
    my $have_support_material = $config->support_material || $config->raft_layers > 0;
    my $have_support_interface = $config->support_material_interface_layers > 0;
    $self->get_field($_)->toggle($have_support_material)
        for qw(support_material_threshold support_material_enforce_layers
            support_material_pattern support_material_spacing support_material_angle
            support_material_interface_layers dont_support_bridges
            support_material_extrusion_width support_material_contact_distance);
    $self->get_field($_)->toggle($have_support_material && $have_support_interface)
        for qw(support_material_interface_spacing support_material_interface_extruder
            support_material_interface_speed);
    
    $self->get_field('perimeter_extrusion_width')->toggle($have_perimeters || $have_skirt || $have_brim);
    $self->get_field('support_material_extruder')->toggle($have_support_material || $have_skirt);
    $self->get_field('support_material_speed')->toggle($have_support_material || $have_brim || $have_skirt);
    
    my $have_sequential_printing = $config->complete_objects;
    $self->get_field($_)->toggle($have_sequential_printing)
        for qw(extruder_clearance_radius extruder_clearance_height);
    
    my $have_ooze_prevention = $config->ooze_prevention;
    $self->get_field($_)->toggle($have_ooze_prevention)
        for qw(standby_temperature_delta);
}

sub hidden_options { !$Slic3r::have_threads ? qw(threads) : () }

package Slic3r::GUI::Tab::Filament;
use base 'Slic3r::GUI::Tab';

sub name { 'filament' }
sub title { 'Filament Settings' }

sub build {
    my $self = shift;
    
    $self->init_config_options(qw(
        filament_colour filament_diameter extrusion_multiplier
        temperature first_layer_temperature bed_temperature first_layer_bed_temperature
        fan_always_on cooling
        min_fan_speed max_fan_speed bridge_fan_speed disable_fan_first_layers
        fan_below_layer_time slowdown_below_layer_time min_print_speed
    ));
    
    {
        my $page = $self->add_options_page('Filament', 'spool.png');
        {
            my $optgroup = $page->new_optgroup('Filament');
            $optgroup->append_single_option_line('filament_colour', 0);
            $optgroup->append_single_option_line('filament_diameter', 0);
            $optgroup->append_single_option_line('extrusion_multiplier', 0);
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
                $line->append_option($optgroup->get_option('first_layer_bed_temperature'));
                $line->append_option($optgroup->get_option('bed_temperature'));
                $optgroup->append_line($line);
            }
        }
    }
    
    {
        my $page = $self->add_options_page('Cooling', 'hourglass.png');
        {
            my $optgroup = $page->new_optgroup('Enable');
            $optgroup->append_single_option_line('fan_always_on');
            $optgroup->append_single_option_line('cooling');
            
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
                $line->append_option($optgroup->get_option('min_fan_speed'));
                $line->append_option($optgroup->get_option('max_fan_speed'));
                $optgroup->append_line($line);
            }
            
            $optgroup->append_single_option_line('bridge_fan_speed');
            $optgroup->append_single_option_line('disable_fan_first_layers');
        }
        {
            my $optgroup = $page->new_optgroup('Cooling thresholds',
                label_width => 250,
            );
            $optgroup->append_single_option_line('fan_below_layer_time');
            $optgroup->append_single_option_line('slowdown_below_layer_time');
            $optgroup->append_single_option_line('min_print_speed');
        }
    }
}

sub _update {
    my ($self) = @_;
    
    $self->_update_description;
    
    my $cooling = $self->{config}->cooling;
    $self->get_field($_)->toggle($cooling)
        for qw(max_fan_speed fan_below_layer_time slowdown_below_layer_time min_print_speed);
    $self->get_field($_)->toggle($cooling || $self->{config}->fan_always_on)
        for qw(min_fan_speed disable_fan_first_layers);
}

sub _update_description {
    my $self = shift;
    
    my $config = $self->config;
    
    my $msg = "";
    my $fan_other_layers = $config->fan_always_on
        ? sprintf "will always run at %d%%%s.", $config->min_fan_speed,
                ($config->disable_fan_first_layers > 1
                    ? " except for the first " . $config->disable_fan_first_layers . " layers"
                    : $config->disable_fan_first_layers == 1
                        ? " except for the first layer"
                        : "")
        : "will be turned off.";
    
    if ($config->cooling) {
        $msg = sprintf "If estimated layer time is below ~%ds, fan will run at %d%% and print speed will be reduced so that no less than %ds are spent on that layer (however, speed will never be reduced below %dmm/s).",
            $config->slowdown_below_layer_time, $config->max_fan_speed, $config->slowdown_below_layer_time, $config->min_print_speed;
        if ($config->fan_below_layer_time > $config->slowdown_below_layer_time) {
            $msg .= sprintf "\nIf estimated layer time is greater, but still below ~%ds, fan will run at a proportionally decreasing speed between %d%% and %d%%.",
                $config->fan_below_layer_time, $config->max_fan_speed, $config->min_fan_speed;
        }
        $msg .= "\nDuring the other layers, fan $fan_other_layers"
    } else {
        $msg = "Fan $fan_other_layers";
    }
    $self->{description_line}->SetText($msg);
}

package Slic3r::GUI::Tab::Printer;
use base 'Slic3r::GUI::Tab';
use Wx qw(:sizer :button :bitmap :misc :id);
use Wx::Event qw(EVT_BUTTON);

sub name { 'printer' }
sub title { 'Printer Settings' }

sub build {
    my $self = shift;
    
    $self->init_config_options(qw(
        bed_shape z_offset
        gcode_flavor use_relative_e_distances
        octoprint_host octoprint_apikey
        use_firmware_retraction pressure_advance vibration_limit
        use_volumetric_e
        start_gcode end_gcode before_layer_gcode layer_gcode toolchange_gcode
        nozzle_diameter extruder_offset
        retract_length retract_lift retract_speed retract_restart_extra retract_before_travel retract_layer_change wipe
        retract_length_toolchange retract_restart_extra_toolchange
    ));
    
    my $bed_shape_widget = sub {
        my ($parent) = @_;
        
        my $btn = Wx::Button->new($parent, -1, "Set…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
        $btn->SetFont($Slic3r::GUI::small_font);
        if ($Slic3r::GUI::have_button_icons) {
            $btn->SetBitmap(Wx::Bitmap->new("$Slic3r::var/cog.png", wxBITMAP_TYPE_PNG));
        }
        
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
            }
            $optgroup->on_change(sub {
                my ($opt_id) = @_;
                if ($opt_id eq 'extruders_count') {
                    $self->update_dirty;
                    $self->_extruders_count_changed($optgroup->get_value('extruders_count'));
                }
            });
        }
        {
            my $optgroup = $page->new_optgroup('OctoPrint upload');
            
            # append two buttons to the Host line
            my $octoprint_host_browse = sub {
                my ($parent) = @_;
                
                my $btn = Wx::Button->new($parent, -1, "Browse…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
                $btn->SetFont($Slic3r::GUI::small_font);
                if ($Slic3r::GUI::have_button_icons) {
                    $btn->SetBitmap(Wx::Bitmap->new("$Slic3r::var/zoom.png", wxBITMAP_TYPE_PNG));
                }
                
                if (!eval "use Net::Bonjour; 1") {
                    $btn->Disable;
                }
                
                EVT_BUTTON($self, $btn, sub {
                    my $dlg = Slic3r::GUI::BonjourBrowser->new($self);
                    if ($dlg->ShowModal == wxID_OK) {
                        my $value = $dlg->GetValue . ":" . $dlg->GetPort;
                        $self->{config}->set('octoprint_host', $value);
                        $self->update_dirty;
                        $self->_on_value_change('octoprint_host', $value);
                        $self->reload_config;
                    }
                });
                
                return $btn;
            };
            my $octoprint_host_test = sub {
                my ($parent) = @_;
                
                my $btn = $self->{octoprint_host_test_btn} = Wx::Button->new($parent, -1, "Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
                $btn->SetFont($Slic3r::GUI::small_font);
                if ($Slic3r::GUI::have_button_icons) {
                    $btn->SetBitmap(Wx::Bitmap->new("$Slic3r::var/wrench.png", wxBITMAP_TYPE_PNG));
                }
                
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
            $optgroup->append_single_option_line('pressure_advance');
            $optgroup->append_single_option_line('vibration_limit');
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
    
    $self->{extruder_pages} = [];
    $self->_build_extruder_pages;
}

sub _extruders_count_changed {
    my ($self, $extruders_count) = @_;
    
    $self->{extruders_count} = $extruders_count;
    $self->_build_extruder_pages;
    $self->_on_value_change('extruders_count', $extruders_count);
    $self->_update;
}

sub _extruder_options { qw(nozzle_diameter extruder_offset retract_length retract_lift retract_speed retract_restart_extra retract_before_travel wipe
    retract_layer_change retract_length_toolchange retract_restart_extra_toolchange) }

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
            my $optgroup = $page->new_optgroup('Position (for multi-extruder printers)');
            $optgroup->append_single_option_line('extruder_offset', $extruder_idx);
        }
        {
            my $optgroup = $page->new_optgroup('Retraction');
            $optgroup->append_single_option_line($_, $extruder_idx)
                for qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel retract_layer_change wipe);
        }
        {
            my $optgroup = $page->new_optgroup('Retraction when tool is disabled (advanced settings for multi-extruder setups)');
            $optgroup->append_single_option_line($_, $extruder_idx)
                for qw(retract_length_toolchange retract_restart_extra_toolchange);
        }
        
        $self->{extruder_pages}[$extruder_idx]{disabled} = 0;
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
    @{$self->{pages}} = (
        (grep $_->{title} !~ /^Extruder \d+/, @{$self->{pages}}),
        @{$self->{extruder_pages}}[ 0 .. $self->{extruders_count}-1 ],
    );
    $self->update_tree;
}

sub _update {
    my ($self) = @_;
    
    my $config = $self->{config};
    
    if ($config->get('octoprint_host') && eval "use LWP::UserAgent; 1") {
        $self->{octoprint_host_test_btn}->Enable;
    } else {
        $self->{octoprint_host_test_btn}->Disable;
    }
    $self->get_field('octoprint_apikey')->toggle($config->get('octoprint_host'));
    
    my $have_multiple_extruders = $self->{extruders_count} > 1;
    $self->get_field('toolchange_gcode')->toggle($have_multiple_extruders);
    
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
        
        # some options only apply when not using firmware retraction
        $self->get_field($_, $i)->toggle($retraction && !$config->use_firmware_retraction)
            for qw(retract_speed retract_restart_extra wipe);
        
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

sub load_config_file {
    my $self = shift;
    $self->SUPER::load_config_file(@_);
    
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
            $self->GetParent->update_dirty;
            $self->GetParent->_on_value_change(@_);
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
        if ($self->{chosen_name} !~ /^[^<>:\/\\|?*\"]+$/i) {
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

has 'default'   => (is => 'ro', default => sub { 0 });
has 'external'  => (is => 'ro', default => sub { 0 });
has 'name'      => (is => 'rw', required => 1);
has 'file'      => (is => 'rw');

sub config {
    my ($self, $keys) = @_;
    
    if ($self->default) {
        return Slic3r::Config->new_from_defaults(@$keys);
    } else {
        if (!-e $self->file) {
            Slic3r::GUI::show_error(undef, "The selected preset does not exist anymore (" . $self->file . ").");
            return undef;
        }
        
        # apply preset values on top of defaults
        my $config = Slic3r::Config->new_from_defaults(@$keys);
        my $external_config = Slic3r::Config->load($self->file);
        $config->set($_, $external_config->get($_))
            for grep $external_config->has($_), @$keys;
        
        return $config;
    }
}

1;
