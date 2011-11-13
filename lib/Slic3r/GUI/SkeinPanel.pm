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
        printer => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Printer',
            options => [qw(nozzle_diameter print_center use_relative_e_distances no_extrusion z_offset)],
        ),
        filament => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Filament',
            options => [qw(filament_diameter filament_packing_density temperature)],
        ),
        speed => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Speed',
            options => [qw(print_feed_rate travel_feed_rate perimeter_feed_rate bottom_layer_speed_ratio)],
        ),
        accuracy => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Accuracy',
            options => [qw(layer_height infill_every_layers)],
        ),
        print => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Print settings',
            options => [qw(perimeter_offsets solid_layers fill_density fill_angle fill_pattern solid_fill_pattern)],
        ),
        retract => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Retraction',
            options => [qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel)],
        ),
        skirt => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height)],
        ),
        transform => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Transform',
            options => [qw(scale rotate duplicate_x duplicate_y duplicate_distance)],
        ),
    );
    $self->{panels} = \%panels;
    
    $panels{slice} = Wx::BoxSizer->new(wxVERTICAL);
    my $slice_button = Wx::Button->new($self, -1, "Slice...");
    $panels{slice}->Add($slice_button, 0, wxALIGN_CENTER);
    EVT_BUTTON($self, $slice_button, \&do_slice);
    
    my @cols = (
        [qw(printer filament speed transform)], [qw(accuracy print retract skirt slice)],
    );
    
    my $config_buttons_sizer;
    {
        $config_buttons_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        
        my $save_button = Wx::Button->new($self, -1, "Save configuration...");
        $config_buttons_sizer->Add($save_button, 0);
        EVT_BUTTON($self, $save_button, \&save_config);
        
        my $load_button = Wx::Button->new($self, -1, "Load configuration...");
        $config_buttons_sizer->Add($load_button, 0);
        EVT_BUTTON($self, $load_button, \&load_config);
        
        my $text = Wx::StaticText->new($self, -1, "Remember to check for updates at http://slic3r.org/", Wx::wxDefaultPosition, Wx::wxDefaultSize, wxALIGN_RIGHT);
        my $font = Wx::Font->new(10, wxDEFAULT, wxNORMAL, wxNORMAL);
        $text->SetFont($font);
        $config_buttons_sizer->Add($text, 1, wxEXPAND | wxALIGN_RIGHT);
    }
    
    my $skein_options_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    foreach my $col (@cols) {
        my $vertical_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $vertical_sizer->Add($panels{$_}, 0, wxEXPAND | wxALL, 10) for @$col;
        $skein_options_sizer->Add($vertical_sizer);
    }
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($config_buttons_sizer, 0, wxEXPAND | wxALL, 10);
    $sizer->Add($skein_options_sizer);
    
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
        );
        $skein->go;
        $process_dialog->Destroy;
        undef $process_dialog;
        
        if (!$main::opt{close_after_slicing}) {
            Wx::MessageDialog->new($self, "$input_file_basename was successfully sliced.", 'Done!', 
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
