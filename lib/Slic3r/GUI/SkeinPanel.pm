package Slic3r::GUI::SkeinPanel;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use Wx qw(:sizer :progressdialog wxOK wxICON_INFORMATION wxICON_ERROR wxID_OK wxFD_OPEN);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    my %panels = (
        printer => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Printer',
            options => [
                {
                    label   => 'Nozzle diameter',
                    value   => \$Slic3r::nozzle_diameter,
                    type    => 'f',
                },
                {
                    label   => 'Print center',
                    value   => \$Slic3r::print_center,
                    type    => 'point',
                },
                {
                    label   => 'Use relative E distances',
                    value   => \$Slic3r::use_relative_e_distances,
                    type    => 'bool',
                },
                {
                    label   => 'Z offset',
                    value   => \$Slic3r::z_offset,
                    type    => 'f',
                },
            ],
        ),
        
        filament => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Filament',
            options => [
                {
                    label   => 'Diameter (mm)',
                    value   => \$Slic3r::filament_diameter,
                    type    => 'f',
                },
                {
                    label   => 'Packing density (mm)',
                    value   => \$Slic3r::filament_packing_density,
                    type    => 'f',
                },
            ],
        ),
        
        speed => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Speed',
            options => [
                {
                    label   => 'Print feed rate (mm/s)',
                    value   => \$Slic3r::print_feed_rate,
                    type    => 'f',
                },
                {
                    label   => 'Travel feed rate (mm/s)',
                    value   => \$Slic3r::travel_feed_rate,
                    type    => 'f',
                },
                {
                    label   => 'Perimeter feed rate (mm/s)',
                    value   => \$Slic3r::perimeter_feed_rate,
                    type    => 'f',
                },
                {
                    label   => 'Bottom layer ratio',
                    value   => \$Slic3r::bottom_layer_speed_ratio,
                    type    => 'f',
                },
            ],
        ),
        
        accuracy => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Accuracy',
            options => [
                {
                    label   => 'Layer height (mm)',
                    value   => \$Slic3r::layer_height,
                    type    => 'f',
                },
            ],
        ),
        
        print => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Print settings',
            options => [
                {
                    label   => 'Perimeters',
                    value   => \$Slic3r::perimeter_offsets,
                    type    => 'i',
                },
                {
                    label   => 'Solid layers',
                    value   => \$Slic3r::solid_layers,
                    type    => 'i',
                },
                {
                    label   => 'Fill density',
                    value   => \$Slic3r::fill_density,
                    type    => 'f',
                },
                {
                    label   => 'Fill angle (°)',
                    value   => \$Slic3r::fill_angle,
                    type    => 'i',
                },
                {
                    label   => 'Temperature (°C)',
                    value   => \$Slic3r::temperature,
                    type    => 'i',
                },
            ],
        ),
        
        retract => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Retraction',
            options => [
                {
                    label   => 'Length (mm)',
                    value   => \$Slic3r::retract_length,
                    type    => 'f',
                },
                {
                    label   => 'Speed (mm/s)',
                    value   => \$Slic3r::retract_speed,
                    type    => 'i',
                },
                {
                    label   => 'Extra length on restart (mm)',
                    value   => \$Slic3r::retract_restart_extra,
                    type    => 'f',
                },
                {
                    label   => 'Minimum travel after retraction (mm)',
                    value   => \$Slic3r::retract_before_travel,
                    type    => 'f',
                },
            ],
        ),
        
        skirt => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Skirt',
            options => [
                {
                    label   => 'Loops',
                    value   => \$Slic3r::skirts,
                    type    => 'i',
                },
                {
                    label   => 'Distance from object (mm)',
                    value   => \$Slic3r::skirt_distance,
                    type    => 'i',
                },
            ],
        ),
        
        transform => Slic3r::GUI::OptionsGroup->new($self,
            title => 'Transform',
            options => [
                {
                    label   => 'Scale',
                    value   => \$Slic3r::scale,
                    type    => 'f',
                },
                {
                    label   => 'Rotate (°)',
                    value   => \$Slic3r::rotate,
                    type    => 'i',
                },
                {
                    label   => 'Multiply along X',
                    value   => \$Slic3r::multiply_x,
                    type    => 'i',
                },
                {
                    label   => 'Multiply along Y',
                    value   => \$Slic3r::multiply_y,
                    type    => 'i',
                },
                {
                    label   => 'Multiply distance',
                    value   => \$Slic3r::multiply_distance,
                    type    => 'i',
                },
            ],
        ),
    );
    
    $panels{slice} = Wx::BoxSizer->new(wxVERTICAL);
    my $slice_button = Wx::Button->new($self, -1, "Slice...");
    $panels{slice}->Add($slice_button, 0, wxALIGN_CENTER);
    EVT_BUTTON($self, $slice_button, \&do_slice);
    
    my @cols = (
        [qw(printer filament speed transform)], [qw(accuracy print retract skirt slice)],
    );
    
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    foreach my $col (@cols) {
        my $vertical_sizer = Wx::BoxSizer->new(wxVERTICAL);
        $vertical_sizer->Add($panels{$_}, 0, wxEXPAND | wxALL, 10) for @$col;
        $sizer->Add($vertical_sizer);
    }
    
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
        );
        $skein->go;
        $process_dialog->Destroy;
        undef $process_dialog;
        
        Wx::MessageDialog->new($self, "$input_file_basename was successfully sliced.", 'Done!', 
            wxOK | wxICON_INFORMATION)->ShowModal;
    };
    
    if (my $err = $@) {
        $process_dialog->Destroy if $process_dialog;
        Wx::MessageDialog->new($self, $err, 'Error', wxOK | wxICON_ERROR)->ShowModal;
    }
}

1;
