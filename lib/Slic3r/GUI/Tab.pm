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
    
    $self->{images} = Wx::ImageList->new(16, 16, 1);
    $self->AssignImageList($self->{images});
    $self->{imagecount} = -1;
    
    return $self;
}

sub AddOptionsPage {
    my $self = shift;
    my $title = shift;
    my $image = (ref $_[1]) ? undef : shift;
    my $page = Slic3r::GUI::Tab::Page->new($self, @_);
    
    my $bitmap = $image
        ? Wx::Bitmap->new("$Slic3r::var/$image", &Wx::wxBITMAP_TYPE_PNG)
        : undef;
    if ($bitmap) {
        $self->{images}->Add($bitmap);
        $self->{imagecount}++;
    }
    $self->AddPage($page, $title, undef, $self->{imagecount});
}

package Slic3r::GUI::Tab::Print;

use Wx qw(:sizer :progressdialog);
use Wx::Event qw();
use base 'Slic3r::GUI::Tab';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1);
    
    $self->AddOptionsPage('Layers and perimeters', 'layers.png', optgroups => [
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
    
    $self->AddOptionsPage('Infill', 'shading.png', optgroups => [
        {
            title => 'Infill',
            options => [qw(fill_density fill_angle fill_pattern solid_fill_pattern infill_every_layers)],
        },
    ]);
    
    $self->AddOptionsPage('Speed', 'time.png', optgroups => [
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
    
    $self->AddOptionsPage('Skirt', 'box.png', optgroups => [
        {
            title => 'Skirt',
            options => [qw(skirts skirt_distance skirt_height)],
        },
    ]);
    
    $self->AddOptionsPage('Support material', 'building.png', optgroups => [
        {
            title => 'Support material',
            options => [qw(support_material support_material_tool)],
        },
    ]);
    
    $self->AddOptionsPage('Notes', 'note.png', optgroups => [
        {
            title => 'Notes',
            no_labels => 1,
            options => [qw(notes)],
        },
    ]);
    
    $self->AddOptionsPage('Output options', 'page_white_go.png', optgroups => [
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
    
    $self->AddOptionsPage('Advanced', 'wrench.png', optgroups => [
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
    
    $self->AddOptionsPage('Filament', 'spool.png', optgroups => [
        {
            title => 'Filament',
            options => [qw(filament_diameter extrusion_multiplier)],
        },
        {
            title => 'Temperature',
            options => [qw(temperature first_layer_temperature bed_temperature first_layer_bed_temperature)],
        },
    ]);
    
    $self->AddOptionsPage('Cooling', 'hourglass.png', optgroups => [
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
    
    $self->AddOptionsPage('General', 'printer_empty.png', optgroups => [
        {
            title => 'Size and coordinates',
            options => [qw(bed_size print_center z_offset)],
        },
        {
            title => 'Firmware',
            options => [qw(gcode_flavor use_relative_e_distances)],
        },
    ]);
    
    $self->AddOptionsPage('Extruder 1', 'funnel.png', optgroups => [
        {
            title => 'Size',
            options => [qw(nozzle_diameter)],
        },
        {
            title => 'Retraction',
            options => [qw(retract_length retract_lift retract_speed retract_restart_extra retract_before_travel)],
        },
    ]);
    
    $self->AddOptionsPage('Custom G-code', 'cog.png', optgroups => [
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
        $self->append_optgroup(%$_) for @{$params{optgroups}};
    }
    
    return $self;
}

sub append_optgroup {
    my $self = shift;
    
    my $optgroup = Slic3r::GUI::OptionsGroup->new($self, label_width => 200, @_);
    $self->{vsizer}->Add($optgroup, 0, wxEXPAND | wxALL, 5);
}

1;
