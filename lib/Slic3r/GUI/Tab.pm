package Slic3r::GUI::Tab;
use strict;
use warnings;
use utf8;

use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Wx::Treebook';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1, [-1,-1], [-1,-1], &Wx::wxBK_LEFT);

    return $self;
}

sub AddOptionsPage {
    my $self = shift;
    my $title = shift;
    my $page = Slic3r::GUI::Tab::Page->new($self, @_);
    $self->AddPage($page, $title);
}

package Slic3r::GUI::Tab::Print;

use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Slic3r::GUI::Tab';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->AddOptionsPage('Layers and perimeters', optgroups => [
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
    
    $self->AddOptionsPage('Infill', optgroups => [
        {
            title => 'Infill',
            options => [qw(fill_density fill_angle fill_pattern solid_fill_pattern)],
        },
    ]);
    
    $self->AddOptionsPage('Speed', optgroups => [
        {
            title => 'Speed for print moves',
            options => [qw(perimeter_speed small_perimeter_speed infill_speed solid_infill_speed top_solid_infill_speed bridge_speed)],
        },
        {
            title => 'Speed for non-print moves',
            options => [qw(travel_speed)],
        },
        {
            title => 'Advanced',
            options => [qw(first_layer_speed)],
        },
    ]);
    
    $self->AddOptionsPage('Skirt', optgroups => [
        {
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height)],
        },
    ]);
    
    $self->AddOptionsPage('Cooling', optgroups => [
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
            options => [qw(fan_below_layer_time slowdown_below_layer_time min_print_speed)],
        },
    ]);
    
    $self->AddOptionsPage('Support material', optgroups => [
        {
            title => 'Support material',
            options => [qw(support_material support_material_tool)],
        },
    ]);
    
    $self->AddOptionsPage('Notes', optgroups => [
        {
            title => 'Notes',
            options => [qw(notes)],
        },
    ]);
    
    $self->AddOptionsPage('Output options', optgroups => [
        {
            title => 'Sequential printing',
            options => [qw(complete_objects extruder_clearance_radius extruder_clearance_height)],
        },
        {
            title => 'Output file',
            options => [qw(gcode_comments output_filename_format)],
        },
        {
            title => 'Advanced',
            options => [qw(post_process duplicate_distance)],  # this is not the right place for duplicate_distance
        },
    ]);
    
    $self->AddOptionsPage('Advanced', optgroups => [
        {
            title => 'Extrusion width',
            options => [qw(extrusion_width first_layer_extrusion_width perimeters_extrusion_width infill_extrusion_width)],
        },
        {
            title => 'Flow',
            options => [qw(bridge_flow_ratio)],
        },
    ]);
    
    
    
    
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
    
    $self->AddOptionsPage('General', optgroups => [
        {
            title => 'Size and coordinates',
            options => [qw(bed_size print_center z_offset)],
        },
        {
            title => 'Firmware',
            options => [qw(gcode_flavor use_relative_e_distances)],
        },
    ]);
    
    $self->AddOptionsPage('Extruder and filament', optgroups => [
        {
            title => 'Size',
            options => [qw(nozzle_diameter)],
        },
        {
            title => 'Filament',
            options => [qw(filament_diameter extrusion_multiplier)],
        },
        {
            title => 'Temperature',
            options => [qw(temperature first_layer_temperature bed_temperature first_layer_bed_temperature)],
        },
    ]);
    
    $self->AddOptionsPage('Custom G-code', optgroups => [
        {
            title => 'Custom G-code',
            options => [qw(start_gcode end_gcode layer_gcode)],
        },
    ]);
    
    $self->AddOptionsPage('Retraction', optgroups => [
        {
            title => 'Retraction',
            options => [qw(retract_length retract_lift retract_speed)],
        },
        {
            title => 'Advanced',
            options => [qw(retract_restart_extra retract_before_travel)],
        },
    ]);
    
    
    return $self;
}

package Slic3r::GUI::Tab::Page;

use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->{vsizer} = Wx::BoxSizer->new(&Wx::wxVERTICAL);
    $self->SetSizer($self->{vsizer});
    
    if ($params{optgroups}) {
        $self->append_optgroup(%$_) for @{$params{optgroups}};
    }
    
    return $self;
}

sub append_optgroup {
    my $self = shift;
    
    my $optgroup = Slic3r::GUI::OptionsGroup->new($self, @_, label_width => 300);
    $self->{vsizer}->Add($optgroup, 0, wxEXPAND | wxALL, 5);
}

1;
