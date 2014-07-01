package Slic3r::GUI::SimpleTab;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use List::Util qw(first);
use Wx qw(:bookctrl :dialog :keycode :icon :id :misc :panel :sizer :window :systemsettings);
use Wx::Event qw(EVT_BUTTON EVT_CHOICE EVT_KEY_DOWN);
use base 'Wx::ScrolledWindow';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    
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

sub init_config_options {
    my ($self, @opt_keys) = @_;
    $self->{config}->apply(Slic3r::Config->new_from_defaults(@opt_keys));
}

sub new_optgroup {
    my ($self, $title, %params) = @_;
    
    my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
        parent          => $self,
        title           => $title,
        config          => $self->{config},
        label_width     => $params{label_width} // 200,
        on_change       => sub { $self->_on_value_change(@_) },
    );
    
    push @{$self->{optgroups}}, $optgroup;
    $self->{vsizer}->Add($optgroup->sizer, 0, wxEXPAND | wxALL, 10);
    
    return $optgroup;
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
    
    foreach my $opt_key (@{$self->{config}->get_keys}) {
        next unless $config->has($opt_key);
        $self->{config}->set($opt_key, $config->get($opt_key));
    }
    $_->reload_config for @{$self->{optgroups}};
}

sub load_presets {}

sub is_dirty { 0 }
sub config { $_[0]->{config}->clone }

sub on_value_change {
    my ($self, $cb) = @_;
    $self->{on_value_change} = $cb;
}

sub on_presets_changed {}

# propagate event to the parent
sub _on_value_change {
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
    
    $self->init_config_options(qw(
        layer_height perimeters top_solid_layers bottom_solid_layers 
        fill_density fill_pattern support_material support_material_spacing raft_layers
        perimeter_speed infill_speed travel_speed
        brim_width
        complete_objects extruder_clearance_radius extruder_clearance_height
    ));
    
    {
        my $optgroup = $self->new_optgroup('General');
        $optgroup->append_single_option_line('layer_height');
        $optgroup->append_single_option_line('perimeters');
        
        my $line = Slic3r::GUI::OptionsGroup::Line->new(
            label => 'Solid layers',
        );
        $line->append_option($optgroup->get_option('top_solid_layers'));
        $line->append_option($optgroup->get_option('bottom_solid_layers'));
        $optgroup->append_line($line);
    }
    
    {
        my $optgroup = $self->new_optgroup('Infill');
        $optgroup->append_single_option_line('fill_density');
        $optgroup->append_single_option_line('fill_pattern');
    }
    
    {
        my $optgroup = $self->new_optgroup('Support material');
        $optgroup->append_single_option_line('support_material');
        $optgroup->append_single_option_line('support_material_spacing');
        $optgroup->append_single_option_line('raft_layers');
    }
    
    {
        my $optgroup = $self->new_optgroup('Speed');
        $optgroup->append_single_option_line('perimeter_speed');
        $optgroup->append_single_option_line('infill_speed');
        $optgroup->append_single_option_line('travel_speed');
    }
    
    {
        my $optgroup = $self->new_optgroup('Brim');
        $optgroup->append_single_option_line('brim_width');
    }
    
    {
        my $optgroup = $self->new_optgroup('Sequential printing');
        $optgroup->append_single_option_line('complete_objects');
        
        my $line = Slic3r::GUI::OptionsGroup::Line->new(
            label => 'Extruder clearance (mm)',
        );
        $line->append_option($optgroup->get_option('extruder_clearance_radius'));
        $line->append_option($optgroup->get_option('extruder_clearance_height'));
        $optgroup->append_line($line);
    }
}

package Slic3r::GUI::SimpleTab::Filament;
use base 'Slic3r::GUI::SimpleTab';

sub name { 'filament' }
sub title { 'Filament Settings' }

sub build {
    my $self = shift;
    
    $self->init_config_options(qw(
        filament_diameter extrusion_multiplier
        temperature first_layer_temperature bed_temperature first_layer_bed_temperature
    ));
    
    {
        my $optgroup = $self->new_optgroup('Filament');
        $optgroup->append_single_option_line('filament_diameter', 0);
        $optgroup->append_single_option_line('extrusion_multiplier', 0);
    }
    
    {
        my $optgroup = $self->new_optgroup('Temperature (°C)');
        
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

package Slic3r::GUI::SimpleTab::Printer;
use base 'Slic3r::GUI::SimpleTab';

sub name { 'printer' }
sub title { 'Printer Settings' }

sub build {
    my $self = shift;
    
    $self->init_config_options(qw(
        z_offset
        gcode_flavor
        nozzle_diameter
        retract_length retract_lift
        start_gcode
        end_gcode
    ));
    
    {
        my $optgroup = $self->new_optgroup('Size and coordinates');
        # TODO: add bed_shape
        $optgroup->append_single_option_line('z_offset');
    }
    
    {
        my $optgroup = $self->new_optgroup('Firmware');
        $optgroup->append_single_option_line('gcode_flavor');
    }
    
    {
        my $optgroup = $self->new_optgroup('Extruder');
        $optgroup->append_single_option_line('nozzle_diameter', 0);
    }
    
    {
        my $optgroup = $self->new_optgroup('Retraction');
        $optgroup->append_single_option_line('retract_length', 0);
        $optgroup->append_single_option_line('retract_lift', 0);
    }
    
    {
        my $optgroup = $self->new_optgroup('Start G-code',
            label_width => 0,
        );
        my $option = $optgroup->get_option('start_gcode');
        $option->full_width(1);
        $option->height(150);
        $optgroup->append_single_option_line($option);
    }
    
    {
        my $optgroup = $self->new_optgroup('End G-code',
            label_width => 0,
        );
        my $option = $optgroup->get_option('end_gcode');
        $option->full_width(1);
        $option->height(150);
        $optgroup->append_single_option_line($option);
    }
}

1;
