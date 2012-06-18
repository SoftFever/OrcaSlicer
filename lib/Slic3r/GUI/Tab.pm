package Slic3r::GUI::Tab;
use strict;
use warnings;
use utf8;

use List::Util qw(first);
use Wx qw(:sizer :progressdialog);
use Wx::Event qw(EVT_TREE_SEL_CHANGED EVT_CHOICE EVT_BUTTON);
use base 'Wx::Panel';

my $small_font = Wx::SystemSettings::GetFont(&Wx::wxSYS_DEFAULT_GUI_FONT);
$small_font->SetPointSize(11);

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1, [-1,-1], [-1,-1], &Wx::wxBK_LEFT);
    
    # horizontal sizer
    $self->{sizer} = Wx::BoxSizer->new(&Wx::wxHORIZONTAL);
    $self->{sizer}->SetSizeHints($self);
    $self->SetSizer($self->{sizer});
    
    # left vertical sizer
    my $left_sizer = Wx::BoxSizer->new(&Wx::wxVERTICAL);
    $self->{sizer}->Add($left_sizer, 0, &Wx::wxEXPAND);
    
    my $left_col_width = 200;
    
    # preset chooser
    {
        my $box = Wx::StaticBox->new($self, -1, "Presets:", [-1, -1], [$left_col_width, 50]);
        $left_sizer->Add($box, 0, &Wx::wxEXPAND | &Wx::wxBOTTOM, 5);
        
        # choice menu
        $self->{presets_choice} = Wx::Choice->new($box, -1, [-1, -1], [-1, -1], []);
        $self->{presets_choice}->SetFont($small_font);
        
        # buttons
        $self->{btn_save_preset} = Wx::BitmapButton->new($box, -1, Wx::Bitmap->new("$Slic3r::var/disk.png", &Wx::wxBITMAP_TYPE_PNG));
        $self->{btn_delete_preset} = Wx::BitmapButton->new($box, -1, Wx::Bitmap->new("$Slic3r::var/delete.png", &Wx::wxBITMAP_TYPE_PNG));
        $self->{btn_save_preset}->SetToolTipString("Save current settings");
        $self->{btn_delete_preset}->SetToolTipString("Delete this preset");
        $self->{btn_save_preset}->Disable;
        $self->{btn_delete_preset}->Disable;
        
        my $hsizer = Wx::BoxSizer->new(&Wx::wxHORIZONTAL);
        $box->SetSizer($hsizer);
        $hsizer->Add($self->{presets_choice}, 1, &Wx::wxRIGHT | &Wx::wxALIGN_CENTER_VERTICAL, 3);
        $hsizer->Add($self->{btn_save_preset}, 0, &Wx::wxALIGN_CENTER_VERTICAL);
        $hsizer->Add($self->{btn_delete_preset}, 0, &Wx::wxALIGN_CENTER_VERTICAL);
    }
    
    # tree
    $self->{treectrl} = Wx::TreeCtrl->new($self, -1, [-1, -1], [$left_col_width, -1], &Wx::wxTR_NO_BUTTONS | &Wx::wxTR_HIDE_ROOT | &Wx::wxTR_SINGLE | &Wx::wxTR_NO_LINES);
    $left_sizer->Add($self->{treectrl}, 1, &Wx::wxEXPAND);
    $self->{icons} = Wx::ImageList->new(16, 16, 1);
    $self->{treectrl}->AssignImageList($self->{icons});
    $self->{iconcount} = -1;
    $self->{treectrl}->AddRoot("root");
    $self->{pages} = {};
    $self->{treectrl}->SetIndent(0);
    EVT_TREE_SEL_CHANGED($parent, $self->{treectrl}, sub {
        $_->Hide for values %{$self->{pages}};
        $self->{sizer}->Remove(1);
        my $page = $self->{pages}->{ $self->{treectrl}->GetItemText($self->{treectrl}->GetSelection) };
        $page->Show;
        $self->{sizer}->Add($page, 1, &Wx::wxEXPAND | &Wx::wxLEFT, 5);
        $self->{sizer}->Layout;
    });
    
    EVT_CHOICE($parent, $self->{presets_choice}, sub {
        if (defined $self->{dirty}) {
            # TODO: prompt user?
            $self->set_dirty(0);
        }
        
        my $i = $self->{presets_choice}->GetSelection;
        if ($i == 0) {
            Slic3r::Config->load_hash($Slic3r::Defaults, $self->{presets_group}, 1);
            $self->{btn_delete_preset}->Disable;
        } else {
            my $file = "$Slic3r::GUI::datadir/$self->{presets_group}/" . $self->{presets}[$i-1];
            if (!-e $file) {
                Slic3r::GUI::show_error($self, "The selected preset does not exist anymore ($file).");
                return;
            }
            Slic3r::Config->load($file, $self->{presets_group});
            $self->{btn_delete_preset}->Enable;
        }
        $_->() for @Slic3r::GUI::OptionsGroup::reload_callbacks{@{$Slic3r::Config::Groups{$self->{presets_group}}}};
        $self->set_dirty(0);
    });
    
    EVT_BUTTON($self, $self->{btn_save_preset}, sub {
        my $i = $self->{presets_choice}->GetSelection;
        my $default = $i == 0 ? 'Untitled' : $self->{presets}[$i-1];
        $default =~ s/\.ini$//i;
        
        my $dlg = Slic3r::GUI::SavePresetWindow->new($self,
            default => $default,
            values  => [ map { $_ =~ s/\.ini$//i; $_ } @{$self->{presets}} ],
        );
        return unless $dlg->ShowModal;
        
        my $file = sprintf "$Slic3r::GUI::datadir/$self->{presets_group}/%s.ini", $dlg->get_name;
        Slic3r::Config->save($file, $self->{presets_group});
        $self->set_dirty(0);
        $self->load_presets;
        $self->{presets_choice}->SetSelection(1 + first { $self->{presets}[$_] eq $dlg->get_name . ".ini" } 0 .. $#{$self->{presets}});
    });
    
    return $self;
}

sub add_options_page {
    my $self = shift;
    my $title = shift;
    my $icon = (ref $_[1]) ? undef : shift;
    my $page = Slic3r::GUI::Tab::Page->new($self, @_, on_change => sub {
        $self->set_dirty(1);
    });
    
    my $bitmap = $icon
        ? Wx::Bitmap->new("$Slic3r::var/$icon", &Wx::wxBITMAP_TYPE_PNG)
        : undef;
    if ($bitmap) {
        $self->{icons}->Add($bitmap);
        $self->{iconcount}++;
    }
    $page->Hide;
    my $itemId = $self->{treectrl}->AppendItem($self->{treectrl}->GetRootItem, $title, $self->{iconcount});
    $self->{pages}{$title} = $page;
    if (keys %{$self->{pages}} == 1) {
        $self->{treectrl}->SelectItem($itemId);
    }
}

sub set_dirty {
    my $self = shift;
    my ($dirty) = @_;
    
    my $i = $self->{dirty} // $self->{presets_choice}->GetSelection; #/
    my $text = $self->{presets_choice}->GetString($i);
        
    if ($dirty) {
        $self->{dirty} = $i;
        $self->{btn_save_preset}->Enable;
        if ($text !~ / \(modified\)$/) {
            $self->{presets_choice}->SetString($i, "$text (modified)");
        }
    } else {
        $self->{dirty} = undef;
        $self->{btn_save_preset}->Disable;
        $text =~ s/ \(modified\)$//;
        $self->{presets_choice}->SetString($i, $text);
    }
}

sub load_presets {
    my $self = shift;
    my ($group) = @_;
    
    $self->{presets_group} ||= $group;
    $self->{presets} = [];
    
    opendir my $dh, "$Slic3r::GUI::datadir/$self->{presets_group}" or die "Failed to read directory $Slic3r::GUI::datadir/$self->{presets_group} (errno: $!)\n";
    my @presets = sort grep /\.ini$/i, readdir $dh;
    closedir $dh;
    
    $self->{presets_choice}->Clear;
    $self->{presets_choice}->Append("- default -");
    foreach my $preset (@presets) {
        push @{$self->{presets}}, $preset;
        $preset =~ s/\.ini$//i;
        $self->{presets_choice}->Append($preset);
    }
}

package Slic3r::GUI::Tab::Print;
use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Slic3r::GUI::Tab';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->add_options_page('Layers and perimeters', 'layers.png', optgroups => [
        {
            title => 'Layer height',
            options => [qw(layer_height first_layer_height)],
        },
        {
            title => 'Vertical shells',
            options => [qw(perimeters randomize_start)],
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
            options => [qw(perimeter_speed small_perimeter_speed infill_speed solid_infill_speed top_solid_infill_speed bridge_speed)],
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
    
    $self->add_options_page('Skirt', 'box.png', optgroups => [
        {
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height)],
        },
    ]);
    
    $self->add_options_page('Support material', 'building.png', optgroups => [
        {
            title => 'Support material',
            options => [qw(support_material support_material_tool)],
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
            options => [qw(extrusion_width first_layer_extrusion_width perimeters_extrusion_width infill_extrusion_width)],
        },
        {
            title => 'Flow',
            options => [qw(bridge_flow_ratio)],
        },
        {
            title => 'Other',
            options => [qw(duplicate_distance)],
        },
    ]);
    
    $self->load_presets('print');
    
    return $self;
}

package Slic3r::GUI::Tab::Filament;
use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Slic3r::GUI::Tab';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->add_options_page('Filament', 'spool.png', optgroups => [
        {
            title => 'Filament',
            options => [qw(filament_diameter extrusion_multiplier)],
        },
        {
            title => 'Temperature',
            options => [qw(temperature first_layer_temperature bed_temperature first_layer_bed_temperature)],
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
    
    return $self;
}

package Slic3r::GUI::Tab::Printer;
use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Slic3r::GUI::Tab';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->add_options_page('General', 'printer_empty.png', optgroups => [
        {
            title => 'Size and coordinates',
            options => [qw(bed_size print_center z_offset)],
        },
        {
            title => 'Firmware',
            options => [qw(gcode_flavor use_relative_e_distances)],
        },
    ]);
    
    $self->add_options_page('Extruder 1', 'funnel.png', optgroups => [
        {
            title => 'Size',
            options => [qw(nozzle_diameter)],
        },
        {
            title => 'Retraction',
            options => [qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel)],
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
    
    $self->load_presets('printer');
    
    return $self;
}

package Slic3r::GUI::Tab::Page;
use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Wx::ScrolledWindow';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->SetScrollbars(1, 1, 1, 1);
    
    $self->{vsizer} = Wx::BoxSizer->new(&Wx::wxVERTICAL);
    $self->SetSizer($self->{vsizer});
    
    if ($params{optgroups}) {
        $self->append_optgroup(%$_, on_change => $params{on_change}) for @{$params{optgroups}};
    }
    
    return $self;
}

sub append_optgroup {
    my $self = shift;
    
    my $optgroup = Slic3r::GUI::OptionsGroup->new($self, label_width => 200, @_);
    $self->{vsizer}->Add($optgroup, 0, wxEXPAND | wxALL, 5);
}

package Slic3r::GUI::SavePresetWindow;
use Wx qw(:sizer);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Save preset", [-1, -1], [-1, -1]);
    
    my $text = Wx::StaticText->new($self, -1, "Save settings as:", [-1, -1], [-1, -1]);
    my $combo = Wx::ComboBox->new($self, -1, $params{default}, [-1, -1], [-1, -1], $params{values});
    my $buttons = $self->CreateStdDialogButtonSizer(&Wx::wxOK | &Wx::wxCANCEL);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($text, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $sizer->Add($combo, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    EVT_BUTTON($self, &Wx::wxID_OK, sub {
        if (($self->{chosen_name} = $combo->GetValue) && $self->{chosen_name} =~ /^[a-z0-9 _-]+$/i) {
            $self->EndModal(1);
        }
    });
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    $self->SetReturnCode(0);
    
    return $self;
}

sub get_name {
    my $self = shift;
    return $self->{chosen_name};
}

1;
