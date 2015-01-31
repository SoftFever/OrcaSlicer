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
    $self->_update;
    
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
    $self->_update;
}

sub load_presets {}

sub is_dirty { 0 }
sub config { $_[0]->{config}->clone }
sub _update {}

sub on_value_change {
    my ($self, $cb) = @_;
    $self->{on_value_change} = $cb;
}

sub on_presets_changed {}

# propagate event to the parent
sub _on_value_change {
    my $self = shift;
    
    $self->{on_value_change}->(@_) if $self->{on_value_change};
    $self->_update;
}

sub get_field {
    my ($self, $opt_key, $opt_index) = @_;
    
    foreach my $optgroup (@{ $self->{optgroups} }) {
        my $field = $optgroup->get_fieldc($opt_key, $opt_index);
        return $field if defined $field;
    }
    return undef;
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
        fill_density fill_pattern external_fill_pattern
        support_material support_material_spacing raft_layers
        support_material_contact_distance dont_support_bridges
        perimeter_speed infill_speed travel_speed
        brim_width
        xy_size_compensation
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
        $optgroup->append_single_option_line('external_fill_pattern');
    }
    
    {
        my $optgroup = $self->new_optgroup('Support material');
        $optgroup->append_single_option_line('support_material');
        $optgroup->append_single_option_line('support_material_spacing');
        $optgroup->append_single_option_line('support_material_contact_distance');
        $optgroup->append_single_option_line('dont_support_bridges');
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
        my $optgroup = $self->new_optgroup('Other');
        $optgroup->append_single_option_line('xy_size_compensation');
    }
}

sub _update {
    my ($self) = @_;
    
    my $config = $self->{config};
    
    my $have_perimeters = $config->perimeters > 0;
    $self->get_field($_)->toggle($have_perimeters)
        for qw(perimeter_speed);
    
    my $have_infill = $config->fill_density > 0;
    my $have_solid_infill = $config->top_solid_layers > 0 || $config->bottom_solid_layers > 0;
    $self->get_field($_)->toggle($have_infill)
        for qw(fill_pattern);
    $self->get_field($_)->toggle($have_solid_infill)
        for qw(external_fill_pattern);
    $self->get_field($_)->toggle($have_infill || $have_solid_infill)
        for qw(infill_speed);
    
    my $have_support_material = $config->support_material || $config->raft_layers > 0;
    $self->get_field($_)->toggle($have_support_material)
        for qw(support_material_spacing dont_support_bridges
            support_material_contact_distance);
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
use Wx qw(:sizer :button :bitmap :misc :id);
use Wx::Event qw(EVT_BUTTON);

sub name { 'printer' }
sub title { 'Printer Settings' }

sub build {
    my $self = shift;
    
    $self->init_config_options(qw(
        bed_shape
        z_offset
        gcode_flavor
        nozzle_diameter
        retract_length retract_lift wipe
        start_gcode
        end_gcode
    ));
    
    {
        my $bed_shape_widget = sub {
            my ($parent) = @_;
        
            my $btn = Wx::Button->new($parent, -1, "Set…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
            $btn->SetFont($Slic3r::GUI::small_font);
            if ($Slic3r::GUI::have_button_icons) {
                $btn->SetBitmap(Wx::Bitmap->new("$Slic3r::var/cog.png", wxBITMAP_TYPE_PNG));
            }
        
            my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
            $sizer->Add($btn);
        
            EVT_BUTTON($self, $btn, sub {
                my $dlg = Slic3r::GUI::BedShapeDialog->new($self, $self->{config}->bed_shape);
                if ($dlg->ShowModal == wxID_OK) {
                    my $value = $dlg->GetValue;
                    $self->{config}->set('bed_shape', $value);
                    $self->_on_value_change('bed_shape', $value);
                }
            });
        
            return $sizer;
        };
    
        my $optgroup = $self->new_optgroup('Size and coordinates');
        my $line = Slic3r::GUI::OptionsGroup::Line->new(
            label       => 'Bed shape',
            widget      => $bed_shape_widget,
        );
        $optgroup->append_line($line);
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
        $optgroup->append_single_option_line('wipe', 0);
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

sub _update {
    my ($self) = @_;
    
    my $config = $self->{config};
    
    my $have_retraction = $config->retract_length->[0] > 0;
    $self->get_field($_, 0)->toggle($have_retraction)
        for qw(retract_lift wipe);
}

1;
