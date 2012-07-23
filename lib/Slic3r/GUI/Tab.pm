package Slic3r::GUI::Tab;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use List::Util qw(first);
use Wx qw(:bookctrl :dialog :icon :id :misc :sizer :treectrl :window);
use Wx::Event qw(EVT_BUTTON EVT_CHOICE EVT_TREE_SEL_CHANGED);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxBK_LEFT);
    
    $self->{sync_presets_with} = $params{sync_presets_with};
    EVT_CHOICE($parent, $self->{sync_presets_with}, sub {
        $self->{presets_choice}->SetSelection($self->{sync_presets_with}->GetSelection);
        $self->on_select_preset;
    });
    
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
    $self->{treectrl} = Wx::TreeCtrl->new($self, -1, wxDefaultPosition, [$left_col_width, -1], wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN);
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
        $self->{sizer}->Detach(1);
        $page->Show;
        $self->{sizer}->Add($page, 1, wxEXPAND | wxLEFT, 5);
        $self->{sizer}->Layout;
        $self->Refresh;
    });
    
    EVT_CHOICE($parent, $self->{presets_choice}, sub {
        $self->on_select_preset;
        $self->sync_presets;
    });
    
    EVT_BUTTON($self, $self->{btn_save_preset}, sub {
        my $preset = $self->current_preset;
        my $default_name = $preset->{default} ? 'Untitled' : basename($preset->{name});
        $default_name =~ s/\.ini$//i;
        
        my $dlg = Slic3r::GUI::SavePresetWindow->new($self,
            title   => lc($self->title),
            default => $default_name,
            values  => [ map { my $name = $_->{name}; $name =~ s/\.ini$//i; $name } @{$self->{presets}} ],
        );
        return unless $dlg->ShowModal == wxID_OK;
        
        my $file = sprintf "$Slic3r::GUI::datadir/$self->{presets_group}/%s.ini", $dlg->get_name;
        Slic3r::Config->save($file, $self->{presets_group});
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
        $self->{presets_choice}->Delete($i);
        $self->{presets_choice}->SetSelection(0);
        $self->on_select_preset;
        $self->sync_presets;
    });
    
    $self->build;
    
    return $self;
}

sub current_preset {
    my $self = shift;
    return $self->{presets}[ $self->{presets_choice}->GetSelection ];
}

sub on_value_change {}
sub on_preset_loaded {}

sub on_select_preset {
    my $self = shift;
    
    if (defined $self->{dirty}) {
        # TODO: prompt user?
        $self->set_dirty(0);
    }
    
    my $preset = $self->current_preset;
    if ($preset->{default}) {
        # default settings: disable the delete button
        Slic3r::Config->load_hash($Slic3r::Defaults, $self->{presets_group}, 1);
        $self->{btn_delete_preset}->Disable;
    } else {
        if (!-e $preset->{file}) {
            Slic3r::GUI::show_error($self, "The selected preset does not exist anymore ($preset->{file}).");
            return;
        }
        eval {
            local $SIG{__WARN__} = Slic3r::GUI::warning_catcher($self);
            Slic3r::Config->load($preset->{file}, $self->{presets_group});
        };
        Slic3r::GUI::catch_error($self);
        $preset->{external}
            ? $self->{btn_delete_preset}->Disable
            : $self->{btn_delete_preset}->Enable;
    }
    $self->on_preset_loaded;
    $_->() for @Slic3r::GUI::OptionsGroup::reload_callbacks{@{$Slic3r::Config::Groups{$self->{presets_group}}}};
    $self->set_dirty(0);
    $Slic3r::Settings->{presets}{$self->{presets_group}} = $preset->{file} ? basename($preset->{file}) : '';
    Slic3r::Config->save_settings("$Slic3r::GUI::datadir/slic3r.ini");
}

sub add_options_page {
    my $self = shift;
    my ($title, $icon, %params) = @_;
    
    if ($icon) {
        my $bitmap = Wx::Bitmap->new("$Slic3r::var/$icon", wxBITMAP_TYPE_PNG);
        $self->{icons}->Add($bitmap);
        $self->{iconcount}++;
    }
    
    my $page = Slic3r::GUI::Tab::Page->new($self, $title, $self->{iconcount}, %params, on_change => sub {
        $self->on_value_change(@_);
        $self->set_dirty(1);
        $self->sync_presets;
    });
    $page->Hide;
    push @{$self->{pages}}, $page;
    $self->update_tree;
    return $page;
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
    
    my $i = $self->{dirty} // $self->{presets_choice}->GetSelection; #/
    my $text = $self->{presets_choice}->GetString($i);
    
    if ($dirty) {
        $self->{dirty} = $i;
        if ($text !~ / \(modified\)$/) {
            $self->{presets_choice}->SetString($i, "$text (modified)");
            $self->{presets_choice}->SetSelection($i);  # wxMSW needs this after every SetString()
        }
    } else {
        $self->{dirty} = undef;
        $text =~ s/ \(modified\)$//;
        $self->{presets_choice}->SetString($i, $text);
        $self->{presets_choice}->SetSelection($i);  # wxMSW needs this after every SetString()
    }
    $self->sync_presets;
}

sub is_dirty {
    my $self = shift;
    return (defined $self->{dirty});
}

sub load_presets {
    my $self = shift;
    my ($group) = @_;
    
    $self->{presets_group} ||= $group;
    $self->{presets} = [{
        default => 1,
        name    => '- default -',
    }];
    
    opendir my $dh, "$Slic3r::GUI::datadir/$self->{presets_group}" or die "Failed to read directory $Slic3r::GUI::datadir/$self->{presets_group} (errno: $!)\n";
    foreach my $file (sort grep /\.ini$/i, readdir $dh) {
        my $name = basename($file);
        $name =~ s/\.ini$//;
        push @{$self->{presets}}, {
            file => "$Slic3r::GUI::datadir/$self->{presets_group}/$file",
            name => $name,
        };
    }
    closedir $dh;
    
    $self->{presets_choice}->Clear;
    $self->{presets_choice}->Append($_->{name}) for @{$self->{presets}};
    {
        # load last used preset
        my $i = first { basename($self->{presets}[$_]{file}) eq ($Slic3r::Settings->{presets}{$self->{presets_group}} || '') } 1 .. $#{$self->{presets}};
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
    return unless $self->{sync_presets_with};
    $self->{sync_presets_with}->Clear;
    foreach my $item ($self->{presets_choice}->GetStrings) {
        $self->{sync_presets_with}->Append($item);
    }
    $self->{sync_presets_with}->SetSelection($self->{presets_choice}->GetSelection);
}

package Slic3r::GUI::Tab::Print;
use base 'Slic3r::GUI::Tab';

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
            options => [qw(solid_layers)],
        },
    ]);
    
    $self->add_options_page('Infill', 'shading.png', optgroups => [
        {
            title => 'Infill',
            options => [qw(fill_density fill_angle fill_pattern solid_fill_pattern infill_every_layers)],
        },
    ]);
    
    $self->add_options_page('Speed', 'time.png', optgroups => [
        {
            title => 'Speed for print moves',
            options => [qw(perimeter_speed small_perimeter_speed external_perimeter_speed infill_speed solid_infill_speed top_solid_infill_speed bridge_speed)],
        },
        {
            title => 'Speed for non-print moves',
            options => [qw(travel_speed)],
        },
        {
            title => 'Modifiers',
            options => [qw(first_layer_speed)],
        },
    ]);
    
    $self->add_options_page('Skirt and brim', 'box.png', optgroups => [
        {
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height)],
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
        {
            title => 'Other',
            options => [qw(duplicate_distance), ($Slic3r::have_threads ? qw(threads) : ())],
        },
    ]);
    
    $self->load_presets('print');
}

package Slic3r::GUI::Tab::Filament;
use base 'Slic3r::GUI::Tab';

sub title { 'Filament Settings' }

sub build {
    my $self = shift;
    
    $self->add_options_page('Filament', 'spool.png', optgroups => [
        {
            title => 'Filament',
            options => ['filament_diameter#0', 'extrusion_multiplier#0'],
        },
        {
            title => 'Temperature',
            options => ['temperature#0', 'first_layer_temperature#0', qw(bed_temperature first_layer_bed_temperature)],
        },
    ]);
    
    $self->add_options_page('Cooling', 'hourglass.png', optgroups => [
        {
            title => 'Enable',
            options => [qw(cooling)],
        },
        {
            title => 'Fan settings',
            options => [qw(min_fan_speed max_fan_speed bridge_fan_speed disable_fan_first_layers fan_always_on)],
        },
        {
            title => 'Cooling thresholds',
            label_width => 250,
            options => [qw(fan_below_layer_time slowdown_below_layer_time min_print_speed)],
        },
    ]);
    
    $self->load_presets('filament');
}

package Slic3r::GUI::Tab::Printer;
use base 'Slic3r::GUI::Tab';

sub title { 'Printer Settings' }

sub build {
    my $self = shift;
    
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
            options => [qw(extruders_count)],
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
    ]);
    
    $self->{extruder_pages} = [];
    $self->build_extruder_pages;
    
    $self->load_presets('printer');
}

sub extruder_options { qw(nozzle_diameter) }

sub build_extruder_pages {
    my $self = shift;
    
    foreach my $extruder_idx (0 .. $Slic3r::extruders_count-1) {
        # set default values
        for my $opt_key ($self->extruder_options) {
            Slic3r::Config->get_raw($opt_key)->[$extruder_idx] //= Slic3r::Config->get_raw($opt_key)->[0]; #/
        }
        
        # build page if it doesn't exist
        $self->{extruder_pages}[$extruder_idx] ||= $self->add_options_page("Extruder " . ($extruder_idx + 1), 'funnel.png', optgroups => [
            {
                title => 'Size',
                options => ['nozzle_diameter#' . $extruder_idx],
            },
            {
                title => 'Retraction',
                options => [qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel)],
            },
        ]);
        $self->{extruder_pages}[$extruder_idx]{disabled} = 0;
    }
    
    # rebuild page list
    @{$self->{pages}} = (
        (grep $_->{title} !~ /^Extruder \d+/, @{$self->{pages}}),
        @{$self->{extruder_pages}}[ 0 .. $Slic3r::extruders_count-1 ],
    );
}

sub on_value_change {
    my $self = shift;
    my ($opt_key) = @_;
    $self->SUPER::on_value_change(@_);
    
    if ($opt_key eq 'extruders_count') {
        # remove unused pages from list
        my @unused_pages = @{ $self->{extruder_pages} }[$Slic3r::extruders_count .. $#{$self->{extruder_pages}}];
        for my $page (@unused_pages) {
            @{$self->{pages}} = grep $_ ne $page, @{$self->{pages}};
            $page->{disabled} = 1;
        }
        
        # delete values for unused extruders
        for my $opt_key ($self->extruder_options) {
            my $values = Slic3r::Config->get_raw($opt_key);
            splice @$values, $Slic3r::extruders_count if $Slic3r::extruders_count <= $#$values;
        }
        
        # add extra pages
        $self->build_extruder_pages;
        
        # update page list and select first page (General)
        $self->update_tree(0);
    }
}

# this gets executed after preset is loaded in repository and before GUI fields are updated
sub on_preset_loaded {
    my $self = shift;
    
    # update the extruders count field
    {
        # set value in repository according to the number of nozzle diameters supplied
        Slic3r::Config->set('extruders_count', scalar @{ Slic3r::Config->get_raw('nozzle_diameter') });
        
        # update the GUI field
        $Slic3r::GUI::OptionsGroup::reload_callbacks{extruders_count}->();
        
        # update extruder page list
        $self->on_value_change('extruders_count');
    }
}

package Slic3r::GUI::Tab::Page;
use Wx qw(:sizer);
use base 'Wx::ScrolledWindow';

sub new {
    my $class = shift;
    my ($parent, $title, $iconID, %params) = @_;
    my $self = $class->SUPER::new($parent, -1);
    $self->{opt_keys}  = [];
    $self->{title}      = $title;
    $self->{iconID}     = $iconID;
    
    $self->SetScrollbars(1, 1, 1, 1);
    
    $self->{vsizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->SetSizer($self->{vsizer});
    
    if ($params{optgroups}) {
        $self->append_optgroup(%$_, on_change => $params{on_change}) for @{$params{optgroups}};
    }
    
    return $self;
}

sub append_optgroup {
    my $self = shift;
    my %params = @_;
    
    my $optgroup = Slic3r::GUI::OptionsGroup->new($self, label_width => 200, %params);
    $self->{vsizer}->Add($optgroup, 0, wxEXPAND | wxALL, 5);
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
        if ($self->{chosen_name} =~ /^[a-z0-9 _-]+$/i) {
            $self->EndModal(wxID_OK);
        } else {
            Slic3r::GUI::show_error($self, "The supplied name is not valid.");
        }
    }
}

sub get_name {
    my $self = shift;
    return $self->{chosen_name};
}

1;
