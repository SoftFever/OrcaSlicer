package Slic3r::GUI::SkeinPanel;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use Wx qw(:sizer :progressdialog wxOK wxICON_INFORMATION wxICON_ERROR wxID_OK wxFD_OPEN
    wxFD_SAVE wxDEFAULT wxNORMAL);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    my %panels = (
        printer => {
            title => 'Printer',
            options => [qw(nozzle_diameter print_center use_relative_e_distances no_extrusion z_offset)],
        },
        filament => {
            title => 'Filament',
            options => [qw(filament_diameter extrusion_multiplier temperature)],
        },
        speed => {
            title => 'Speed',
            options => [qw(print_feed_rate travel_feed_rate perimeter_feed_rate bottom_layer_speed_ratio)],
        },
        accuracy => {
            title => 'Accuracy',
            options => [qw(layer_height infill_every_layers)],
        },
        print => {
            title => 'Print settings',
            options => [qw(perimeters solid_layers fill_density fill_angle fill_pattern solid_fill_pattern)],
        },
        retract => {
            title => 'Retraction',
            options => [qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel)],
        },
        skirt => {
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height)],
        },
        transform => {
            title => 'Transform',
            options => [qw(scale rotate duplicate_x duplicate_y duplicate_distance)],
        },
        gcode => {
            title => 'Custom GCODE',
            options => [qw(start_gcode end_gcode)],
        },
        extrusion => {
            title => 'Extrusion',
            options => [qw(extrusion_width_ratio)],
        },
    );
    $self->{panels} = \%panels;
    
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
        $make_tab->([qw(printer filament)], [qw(speed)]),
        $make_tab->([qw(gcode)]),
        $make_tab->([qw(extrusion)]),
    );
    
    $tabpanel->AddPage($tabs[0], "Print Settings");
    $tabpanel->AddPage($tabs[1], "Printer and Filament");
    $tabpanel->AddPage($tabs[2], "Start/End GCODE");
    $tabpanel->AddPage($tabs[3], "Advanced");
        
    my $buttons_sizer;
    {
        $buttons_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        
        my $slice_button = Wx::Button->new($self, -1, "Slice...");
        $buttons_sizer->Add($slice_button, 0);
        EVT_BUTTON($self, $slice_button, \&do_slice);
        
        my $save_button = Wx::Button->new($self, -1, "Save configuration...");
        $buttons_sizer->Add($save_button, 0);
        EVT_BUTTON($self, $save_button, \&save_config);
        
        my $load_button = Wx::Button->new($self, -1, "Load configuration...");
        $buttons_sizer->Add($load_button, 0);
        EVT_BUTTON($self, $load_button, \&load_config);
        
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
    
    return $self;
}

sub do_slice {
    my $self = shift;
    
    my $process_dialog;
    eval {
        # validate configuration
        Slic3r::Config->validate;
        
        # select input file
        my $dialog = Wx::FileDialog->new($self, 'Choose a STL file to slice:', "", "", "STL files *.stl|*.stl;*.STL", wxFD_OPEN);
        return unless $dialog->ShowModal == wxID_OK;
        my ($input_file) = $dialog->GetPaths;
        my $input_file_basename = basename($input_file);
        
        # show processbar dialog
        $process_dialog = Wx::ProgressDialog->new('Slicing...', "Processing $input_file_basename...", 
            100, $self, wxPD_APP_MODAL);
        $process_dialog->Pulse;
        my $skein = Slic3r::Skein->new(
            input_file  => $input_file,
            output_file => $main::opt{output},
            status_cb   => sub {
                my ($percent, $message) = @_;
                $process_dialog->Update($percent, $message);
            },
        );
        $skein->go;
        $process_dialog->Destroy;
        undef $process_dialog;
        
        if (!$main::opt{close_after_slicing}) {
            my $message = sprintf "%s was successfully sliced in %d minutes and %.3f seconds.",
                $input_file_basename, int($skein->processing_time/60),
                $skein->processing_time - int($skein->processing_time/60)*60;
            Wx::MessageDialog->new($self, $message, 'Done!', 
                wxOK | wxICON_INFORMATION)->ShowModal;
        } else {
            $self->GetParent->Destroy();  # quit
        }
    };
    $self->catch_error(sub { $process_dialog->Destroy if $process_dialog });
}

my $ini_wildcard = "INI files *.ini|*.ini;*.INI";

sub save_config {
    my $self = shift;
    
    my $dlg = Wx::FileDialog->new($self, 'Save configuration as:', "", "config.ini", 
        $ini_wildcard, wxFD_SAVE);
    if ($dlg->ShowModal == wxID_OK) {
        Slic3r::Config->save($dlg->GetPath);
    }
}

sub load_config {
    my $self = shift;
    
    my $dlg = Wx::FileDialog->new($self, 'Select configuration to load:', "", "config.ini", 
        $ini_wildcard, wxFD_OPEN);
    if ($dlg->ShowModal == wxID_OK) {
        my ($file) = $dlg->GetPaths;
        eval {
            Slic3r::Config->load($file);
        };
        $self->catch_error();
        $_->() for @Slic3r::GUI::OptionsGroup::reload_callbacks;
    }
}

sub catch_error {
    my ($self, $cb) = @_;
    if (my $err = $@) {
        $cb->() if $cb;
        Wx::MessageDialog->new($self, $err, 'Error', wxOK | wxICON_ERROR)->ShowModal;
    }
}

1;
