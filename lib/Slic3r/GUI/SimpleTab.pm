package Slic3r::GUI::SimpleTab;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use List::Util qw(first);
use Wx qw(:bookctrl :dialog :keycode :icon :id :misc :panel :sizer :window :systemsettings);
use Wx::Event qw(EVT_BUTTON EVT_CHOICE EVT_KEY_DOWN EVT_TREE_SEL_CHANGED);
use base 'Wx::ScrolledWindow';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    $self->{options} = []; # array of option names handled by this tab
    $self->{$_} = $params{$_} for qw(on_value_change);
    
    $self->SetScrollbars(1, 1, 1, 1);
    
    $self->{config} = Slic3r::Config->new;
    $self->{optgroups} = [];
    
    $self->{vsizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->SetSizer($self->{vsizer});
    $self->build;
    
    {
        my $label = Wx::StaticText->new($self, -1, "Want more options? Switch to the Expert Mode.", wxDefaultPosition, wxDefaultSize);
        $label->SetFont(Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
        $self->{vsizer}->Add($label, 0, wxEXPAND | wxALL, 10);
    }
    
    return $self;
}

sub append_optgroup {
    my $self = shift;
    my %params = @_;
    
    # apply default values
    {
        my @options = @{$params{options}};
        $_ =~ s/#.+// for @options;
        my $config = Slic3r::Config->new_from_defaults(@options);
        $self->{config}->apply($config);
    }
    
    my $class = $params{class} || 'Slic3r::GUI::ConfigOptionsGroup';
    my $optgroup = $class->new(
        parent      => $self,
        config      => $self->{config},
        label_width => 200,
        on_change   => sub { $self->on_value_change(@_) },
        %params,
    );
    
    push @{$self->{optgroups}}, $optgroup;
    ($params{sizer} || $self->{vsizer})->Add($optgroup->sizer, 0, wxEXPAND | wxALL, 10);
}

sub load_config_file {
    my $self = shift;
    my ($file) = @_;
    
    my $config = Slic3r::Config->load($file);
    $self->load_config($config);
}

sub load_config {
    my $self = shift;
    my ($config) = @_;
    
    foreach my $opt_key (grep $self->{config}->has($_), keys %$config) {
        my $value = $config->get($opt_key);
        $self->{config}->set($opt_key, $value);
        $_->set_value($opt_key, $value) for @{$self->{optgroups}};
    }
}

sub is_dirty { 0 }
sub config { $_[0]->{config}->clone }

# propagate event to the parent
sub on_value_change {
    my $self = shift;
    $self->{on_value_change}->(@_) if $self->{on_value_change};
}

package Slic3r::GUI::SimpleTab::Print;
use base 'Slic3r::GUI::SimpleTab';

use Wx qw(:sizer);

sub name { 'print' }
sub title { 'Print Settings' }

sub build {
    my $self = shift;
    
    $self->append_optgroup(
        title => 'General',
        options => [qw(layer_height perimeters top_solid_layers bottom_solid_layers)],
        lines => [
            Slic3r::GUI::OptionsGroup->single_option_line('layer_height'),
            Slic3r::GUI::OptionsGroup->single_option_line('perimeters'),
            {
                label   => 'Solid layers',
                options => [qw(top_solid_layers bottom_solid_layers)],
            },
        ],
    );
    
    $self->append_optgroup(
        title => 'Infill',
        options => [qw(fill_density fill_pattern)],
    );
    
    $self->append_optgroup(
        title => 'Support material',
        options => [qw(support_material support_material_spacing raft_layers)],
    );
    
    $self->append_optgroup(
        title => 'Speed',
        options => [qw(perimeter_speed infill_speed travel_speed)],
    );
    
    $self->append_optgroup(
        title => 'Brim',
        options => [qw(brim_width)],
    );
    
    $self->append_optgroup(
        title => 'Sequential printing',
        options => [qw(complete_objects extruder_clearance_radius extruder_clearance_height)],
        lines => [
            Slic3r::GUI::OptionsGroup->single_option_line('complete_objects'),
            {
                label   => 'Extruder clearance (mm)',
                options => [qw(extruder_clearance_radius extruder_clearance_height)],
            },
        ],
    );
}

package Slic3r::GUI::SimpleTab::Filament;
use base 'Slic3r::GUI::SimpleTab';

sub name { 'filament' }
sub title { 'Filament Settings' }

sub build {
    my $self = shift;
    
    $self->append_optgroup(
        title => 'Filament',
        options => ['filament_diameter#0', 'extrusion_multiplier#0'],
    );
    
    $self->append_optgroup(
        title => 'Temperature (°C)',
        options => ['temperature#0', 'first_layer_temperature#0', qw(bed_temperature first_layer_bed_temperature)],
        lines => [
            {
                label   => 'Extruder',
                options => ['first_layer_temperature#0', 'temperature#0'],
            },
            {
                label   => 'Bed',
                options => [qw(first_layer_bed_temperature bed_temperature)],
            },
        ],
    );
}

package Slic3r::GUI::SimpleTab::Printer;
use base 'Slic3r::GUI::SimpleTab';

sub name { 'printer' }
sub title { 'Printer Settings' }

sub build {
    my $self = shift;
    
    $self->append_optgroup(
        title => 'Size and coordinates',
        options => [qw(bed_size print_center z_offset)],
    );
    
    $self->append_optgroup(
        title => 'Firmware',
        options => [qw(gcode_flavor)],
    );
    
    $self->append_optgroup(
        title => 'Extruder',
        options => ['nozzle_diameter#0'],
    );
    
    $self->append_optgroup(
        title => 'Retraction',
        options => ['retract_length#0', 'retract_lift#0'],
    );
    
    $self->append_optgroup(
        title => 'Start G-code',
        no_labels => 1,
        options => [qw(start_gcode)],
    );
    
    $self->append_optgroup(
        title => 'End G-code',
        no_labels => 1,
        options => [qw(end_gcode)],
    );
}

1;
