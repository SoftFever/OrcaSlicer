package Slic3r::GUI::SkeinPanel;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename dirname);
use Slic3r::Geometry qw(X Y);
use Wx qw(:sizer :progressdialog wxOK wxICON_INFORMATION wxICON_WARNING wxICON_ERROR wxICON_QUESTION
    wxOK wxCANCEL wxID_OK wxFD_OPEN wxFD_SAVE wxDEFAULT wxNORMAL);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Panel';

my $last_skein_dir;
my $last_config_dir;
my $last_input_file;
my $last_output_file;
our $last_config;

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    my %panels = (
        printer => {
            title => 'Printer',
            options => [qw(nozzle_diameter print_center z_offset gcode_flavor use_relative_e_distances)],
        },
        filament => {
            title => 'Filament',
            options => [qw(filament_diameter extrusion_multiplier temperature first_layer_temperature bed_temperature first_layer_bed_temperature)],
        },
        print_speed => {
            title => 'Print speed',
            options => [qw(perimeter_speed small_perimeter_speed infill_speed solid_infill_speed bridge_speed)],
        },
        speed => {
            title => 'Other speed settings',
            options => [qw(travel_speed bottom_layer_speed_ratio)],
        },
        accuracy => {
            title => 'Accuracy',
            options => [qw(layer_height first_layer_height_ratio infill_every_layers)],
        },
        print => {
            title => 'Print settings',
            options => [qw(perimeters solid_layers fill_density fill_angle fill_pattern solid_fill_pattern support_material support_material_tool)],
        },
        retract => {
            title => 'Retraction',
            options => [qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel)],
        },
        cooling => {
            title => 'Cooling',
            options => [qw(cooling min_fan_speed max_fan_speed bridge_fan_speed fan_below_layer_time slowdown_below_layer_time min_print_speed disable_fan_first_layers fan_always_on)],
            label_width => 300,
        },
        skirt => {
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height)],
        },
        transform => {
            title => 'Transform',
            options => [qw(scale rotate duplicate_mode duplicate bed_size duplicate_grid duplicate_distance)],
        },
        gcode => {
            title => 'Custom G-code',
            options => [qw(start_gcode end_gcode layer_gcode gcode_comments post_process)],
        },
        extrusion => {
            title => 'Extrusion',
            options => [qw(extrusion_width_ratio bridge_flow_ratio)],
        },
        output => {
            title => 'Output',
            options => [qw(output_filename_format)],
        },
        notes => {
            title => 'Notes',
            options => [qw(notes)],
        },
    );
    $self->{panels} = \%panels;

    if (eval "use Growl::GNTP; 1") {
        # register growl notifications
        eval {
            $self->{growler} = Growl::GNTP->new(AppName => 'Slic3r', AppIcon => "$FindBin::Bin/var/Slic3r.png");
            $self->{growler}->register([{Name => 'SKEIN_DONE', DisplayName => 'Slicing Done'}]);
        };
    }
    
    my $tabpanel = Wx::Notebook->new($self, -1, Wx::wxDefaultPosition, Wx::wxDefaultSize, &Wx::wxNB_TOP);
    my $make_tab = sub {
        my @cols = @_;
        
        my $tab = Wx::Panel->new($tabpanel, -1);
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        foreach my $col (@cols) {
            my $vertical_sizer = Wx::BoxSizer->new(wxVERTICAL);
            for my $optgroup (@$col) {
                my $optpanel = Slic3r::GUI::OptionsGroup->new($tab, %{$panels{$optgroup}});
                $vertical_sizer->Add($optpanel, 0, wxEXPAND | wxALL, 10);
            }
            $sizer->Add($vertical_sizer);
        }
        
        $tab->SetSizer($sizer);
        return $tab;
    };
    
    my @tabs = (
        $make_tab->([qw(transform accuracy skirt)], [qw(print retract)]),
        $make_tab->([qw(cooling)]),
        $make_tab->([qw(printer filament)], [qw(print_speed speed)]),
        $make_tab->([qw(gcode)]),
        $make_tab->([qw(notes)]),
        $make_tab->([qw(extrusion)], [qw(output)]),
    );
    
    $tabpanel->AddPage($tabs[0], "Print Settings");
    $tabpanel->AddPage($tabs[1], "Cooling");
    $tabpanel->AddPage($tabs[2], "Printer and Filament");
    $tabpanel->AddPage($tabs[3], "Custom G-code");
    $tabpanel->AddPage($tabs[4], "Notes");
    $tabpanel->AddPage($tabs[5], "Advanced");
        
    my $buttons_sizer;
    {
        $buttons_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        
        my $slice_button = Wx::Button->new($self, -1, "Slice...");
        $slice_button->SetDefault();
        $buttons_sizer->Add($slice_button, 0, wxRIGHT, 20);
        EVT_BUTTON($self, $slice_button, sub { $self->do_slice });
        
        my $save_button = Wx::Button->new($self, -1, "Save config...");
        $buttons_sizer->Add($save_button, 0);
        EVT_BUTTON($self, $save_button, sub { $self->save_config });
        
        my $load_button = Wx::Button->new($self, -1, "Load config...");
        $buttons_sizer->Add($load_button, 0);
        EVT_BUTTON($self, $load_button, sub { $self->load_config });
        
        my $text = Wx::StaticText->new($self, -1, "Remember to check for updates at http://slic3r.org/\nVersion: $Slic3r::VERSION", Wx::wxDefaultPosition, Wx::wxDefaultSize, wxALIGN_RIGHT);
        my $font = Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL);
        $text->SetFont($font);
        $buttons_sizer->Add($text, 1, wxEXPAND | wxALIGN_RIGHT);
    }
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($buttons_sizer, 0, wxEXPAND | wxALL, 10);
    $sizer->Add($tabpanel);
    
    $sizer->SetSizeHints($self);
    $self->SetSizer($sizer);
    $self->Layout;
    
    $_->() for @Slic3r::GUI::OptionsGroup::reload_callbacks;
    
    return $self;
}

my $model_wildcard = "STL files (*.stl)|*.stl;*.STL|AMF files (*.amf)|*.amf;*.AMF;*.xml;*.XML";
my $ini_wildcard = "INI files *.ini|*.ini;*.INI";
my $gcode_wildcard = "G-code files *.gcode|*.gcode;*.GCODE";

sub do_slice {
    my $self = shift;
    my %params = @_;
    
    my $process_dialog;
    eval {
        # validate configuration
        Slic3r::Config->validate;

        # confirm slicing of more than one copies
        my $copies = $Slic3r::duplicate_grid->[X] * $Slic3r::duplicate_grid->[Y];
        $copies = $Slic3r::duplicate if $Slic3r::duplicate > 1;
        if ($copies > 1) {
            my $confirmation = Wx::MessageDialog->new($self, "Are you sure you want to slice $copies copies?",
                                                      'Confirm', wxICON_QUESTION | wxOK | wxCANCEL);
            return unless $confirmation->ShowModal == wxID_OK;
        }
        
        # select input file
        my $dir = $last_skein_dir || $last_config_dir || "";

        my $input_file;
        if (!$params{reslice}) {
            my $dialog = Wx::FileDialog->new($self, 'Choose a STL or AMF file to slice:', $dir, "", $model_wildcard, wxFD_OPEN);
            if ($dialog->ShowModal != wxID_OK) {
                $dialog->Destroy;
                return;
            }
            $input_file = $dialog->GetPaths;
            $dialog->Destroy;
            $last_input_file = $input_file;
        } else {
            if (!defined $last_input_file) {
                Wx::MessageDialog->new($self, "No previously sliced file",
                                       'Confirm', wxICON_ERROR | wxOK)->ShowModal();
                return;
            }
            if (! -e $last_input_file) {
                Wx::MessageDialog->new($self, "Cannot find previously sliced file!",
                                       'Confirm', wxICON_ERROR | wxOK)->ShowModal();
                return;
            }
            $input_file = $last_input_file;
        }
        my $input_file_basename = basename($input_file);
        $last_skein_dir = dirname($input_file);
        
        my $skein = Slic3r::Skein->new(
            input_file  => $input_file,
            output_file => $main::opt{output},
            status_cb   => sub {
                my ($percent, $message) = @_;
                if (&Wx::wxVERSION_STRING =~ / 2\.(8\.|9\.[2-9])/) {
                    $process_dialog->Update($percent, "$message...");
                }
            },
        );

        # select output file
        if ($params{reslice}) {
            if (defined $last_output_file) {
                $skein->output_file($last_output_file);
            }
        } elsif ($params{save_as}) {
            my $output_file = $skein->expanded_output_filepath;
            $output_file =~ s/\.gcode$/.svg/i if $params{export_svg};
            my $dlg = Wx::FileDialog->new($self, 'Save ' . ($params{export_svg} ? 'SVG' : 'G-code') . ' file as:', dirname($output_file),
                basename($output_file), $gcode_wildcard, wxFD_SAVE);
            if ($dlg->ShowModal != wxID_OK) {
                $dlg->Destroy;
                return;
            }
            $skein->output_file($dlg->GetPath);
            $last_output_file = $dlg->GetPath;
            $dlg->Destroy;
        }
        
        # show processbar dialog
        $process_dialog = Wx::ProgressDialog->new('Slicing...', "Processing $input_file_basename...", 
            100, $self, 0);
        $process_dialog->Pulse;
        
        {
            my @warnings = ();
            local $SIG{__WARN__} = sub { push @warnings, $_[0] };
            if ($params{export_svg}) {
                $skein->export_svg;
            } else {
                $skein->go;
            }
            $self->catch_warning->($_) for @warnings;
        }
        $process_dialog->Destroy;
        undef $process_dialog;
        
        my $message = "$input_file_basename was successfully sliced";
        $message .= sprintf " in %d minutes and %.3f seconds",
            int($skein->processing_time/60),
            $skein->processing_time - int($skein->processing_time/60)*60
            if $skein->processing_time;
        $message .= ".";
        eval {
            $self->{growler}->notify(Event => 'SKEIN_DONE', Title => 'Slicing Done!', Message => $message)
                if ($self->{growler});
        };
        Wx::MessageDialog->new($self, $message, 'Done!', 
            wxOK | wxICON_INFORMATION)->ShowModal;
    };
    $self->catch_error(sub { $process_dialog->Destroy if $process_dialog });
}

sub save_config {
    my $self = shift;
    
    my $process_dialog;
    eval {
        # validate configuration
        Slic3r::Config->validate;
    };
    $self->catch_error(sub { $process_dialog->Destroy if $process_dialog }) and return;
    
    my $dir = $last_config ? dirname($last_config) : $last_config_dir || $last_skein_dir || "";
    my $filename = $last_config ? basename($last_config) : "config.ini";
    my $dlg = Wx::FileDialog->new($self, 'Save configuration as:', $dir, $filename, 
        $ini_wildcard, wxFD_SAVE);
    if ($dlg->ShowModal == wxID_OK) {
        my $file = $dlg->GetPath;
        $last_config_dir = dirname($file);
        $last_config = $file;
        Slic3r::Config->save($file);
    }
    $dlg->Destroy;
}

sub load_config {
    my $self = shift;
    
    my $dir = $last_config ? dirname($last_config) : $last_config_dir || $last_skein_dir || "";
    my $dlg = Wx::FileDialog->new($self, 'Select configuration to load:', $dir, "config.ini", 
        $ini_wildcard, wxFD_OPEN);
    if ($dlg->ShowModal == wxID_OK) {
        my ($file) = $dlg->GetPaths;
        $last_config_dir = dirname($file);
        $last_config = $file;
        eval {
            local $SIG{__WARN__} = $self->catch_warning;
            Slic3r::Config->load($file);
        };
        $self->catch_error();
        $_->() for @Slic3r::GUI::OptionsGroup::reload_callbacks;
    }
    $dlg->Destroy;
}

sub catch_error {
    my ($self, $cb) = @_;
    if (my $err = $@) {
        $cb->() if $cb;
        Wx::MessageDialog->new($self, $err, 'Error', wxOK | wxICON_ERROR)->ShowModal;
        return 1;
    }
    return 0;
}

sub catch_warning {
    my ($self) = @_;
    return sub {
        my $message = shift;
        Wx::MessageDialog->new($self, $message, 'Warning', wxOK | wxICON_WARNING)->ShowModal;
    };
};

1;
