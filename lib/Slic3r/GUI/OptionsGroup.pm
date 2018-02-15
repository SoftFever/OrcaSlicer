# A dialog group object. Used by the Tab, Preferences dialog, ManualControlDialog etc.

package Slic3r::GUI::OptionsGroup;
use Moo;

use List::Util qw(first);
use Wx qw(:combobox :font :misc :sizer :systemsettings :textctrl wxTheApp);
use Wx::Event qw(EVT_CHECKBOX EVT_COMBOBOX EVT_SPINCTRL EVT_TEXT EVT_KILL_FOCUS EVT_SLIDER);

has 'parent'        => (is => 'ro', required => 1);
has 'title'         => (is => 'ro', required => 1);
has 'on_change'     => (is => 'rw', default => sub { sub {} });
has 'staticbox'     => (is => 'ro', default => sub { 1 });
has 'label_width'   => (is => 'rw', default => sub { 180 });
has 'extra_column'  => (is => 'rw', default => sub { undef });
has 'label_font'    => (is => 'rw');
has 'sidetext_font' => (is => 'rw', default => sub { Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) });
has 'sizer'         => (is => 'rw');
has '_disabled'     => (is => 'rw', default => sub { 0 });
has '_grid_sizer'   => (is => 'rw');
has '_options'      => (is => 'ro', default => sub { {} });
has '_fields'       => (is => 'ro', default => sub { {} });

sub BUILD {
    my $self = shift;
    
    if ($self->staticbox) {
        my $box = Wx::StaticBox->new($self->parent, -1, $self->title);
        $self->sizer(Wx::StaticBoxSizer->new($box, wxVERTICAL));
    } else {
        $self->sizer(Wx::BoxSizer->new(wxVERTICAL));
    }
    
    my $num_columns = 1;
    ++$num_columns if $self->label_width != 0;
    ++$num_columns if $self->extra_column;
    $self->_grid_sizer(Wx::FlexGridSizer->new(0, $num_columns, 0, 0));
    $self->_grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    $self->_grid_sizer->AddGrowableCol($self->label_width != 0);
    
    # TODO: border size may be related to wxWidgets 2.8.x vs. 2.9.x instead of wxMAC specific
    $self->sizer->Add($self->_grid_sizer, 0, wxEXPAND | wxALL, &Wx::wxMAC ? 0 : 5);
}

# this method accepts a Slic3r::GUI::OptionsGroup::Line object
sub append_line {
    my ($self, $line) = @_;
    
    if ($line->sizer || ($line->widget && $line->full_width)) {
        # full-width widgets are appended *after* the grid sizer, so after all the non-full-width lines
        my $sizer = $line->sizer // $line->widget->($self->parent);
        $self->sizer->Add($sizer, 0, wxEXPAND | wxALL, &Wx::wxMAC ? 0 : 15);
        return;
    }
    
    my $grid_sizer = $self->_grid_sizer;
    
    # if we have an extra column, build it
    if ($self->extra_column) {
        if (defined (my $item = $self->extra_column->($line))) {
            $grid_sizer->Add($item, 0, wxALIGN_CENTER_VERTICAL, 0);
        } else {
            # if the callback provides no sizer for the extra cell, put a spacer
            $grid_sizer->AddSpacer(1);
        }
    }
    
    # build label if we have it
    my $label;
    if ($self->label_width != 0) {
        $label = Wx::StaticText->new($self->parent, -1, $line->label ? $line->label . ":" : "", wxDefaultPosition, [$self->label_width, -1]);
        $label->SetFont($self->label_font) if $self->label_font;
        $label->Wrap($self->label_width) ;  # needed to avoid Linux/GTK bug
        $grid_sizer->Add($label, 0, wxALIGN_CENTER_VERTICAL, 0);
        $label->SetToolTipString($line->label_tooltip) if $line->label_tooltip;
    }
    
    # if we have a widget, add it to the sizer
    if ($line->widget) {
        my $widget_sizer = $line->widget->($self->parent);
        $grid_sizer->Add($widget_sizer, 0, wxEXPAND | wxALL, &Wx::wxMAC ? 0 : 15);
        return;
    }
    
    # if we have a single option with no sidetext just add it directly to the grid sizer
    my @options = @{$line->get_options};
    $self->_options->{$_->opt_id} = $_ for @options;
    if (@options == 1 && !$options[0]->sidetext && !$options[0]->side_widget && !@{$line->get_extra_widgets}) {
        my $option = $options[0];
        my $field = $self->_build_field($option);
        $grid_sizer->Add($field, 0, ($option->full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
        return;
    }
    
    # if we're here, we have more than one option or a single option with sidetext
    # so we need a horizontal sizer to arrange these things
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $grid_sizer->Add($sizer, 0, 0, 0);
    
    foreach my $i (0..$#options) {
        my $option = $options[$i];
        
        # add label if any
        if ($option->label) {
            my $field_label = Wx::StaticText->new($self->parent, -1, $option->label . ":", wxDefaultPosition, wxDefaultSize);
            $field_label->SetFont($self->sidetext_font);
            $sizer->Add($field_label, 0, wxALIGN_CENTER_VERTICAL, 0);
        }
        
        # add field
        my $field = $self->_build_field($option);
        $sizer->Add($field, 0, wxALIGN_CENTER_VERTICAL, 0);
        
        # add sidetext if any
        if ($option->sidetext) {
            my $sidetext = Wx::StaticText->new($self->parent, -1, $option->sidetext, wxDefaultPosition, wxDefaultSize);
            $sidetext->SetFont($self->sidetext_font);
            $sizer->Add($sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
        }
        
        # add side widget if any
        if ($option->side_widget) {
            $sizer->Add($option->side_widget->($self->parent), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1);
        }
        
        if ($option != $#options) {
            $sizer->AddSpacer(4);
        }
    }
        
    # add extra sizers if any
    foreach my $extra_widget (@{$line->get_extra_widgets}) {
        $sizer->Add($extra_widget->($self->parent), 0, wxLEFT | wxALIGN_CENTER_VERTICAL , 4);
    }
}

sub create_single_option_line {
    my ($self, $option) = @_;
    
    my $line = Slic3r::GUI::OptionsGroup::Line->new(
        label           => $option->label,
        label_tooltip   => $option->tooltip,
    );
    $option->label("");
    $line->append_option($option);
    
    return $line;
}

sub append_single_option_line {
    my ($self, $option) = @_;
    return $self->append_line($self->create_single_option_line($option));
}

sub _build_field {
    my $self = shift;
    my ($opt) = @_;
    
    my $opt_id = $opt->opt_id;
    my $on_change = sub {
        #! This function will be called from Field.
        my ($opt_id, $value) = @_;
        #! Call OptionGroup._on_change(...)
        $self->_on_change($opt_id, $value)
            unless $self->_disabled;
    };
    my $on_kill_focus = sub {
        my ($opt_id) = @_;
        $self->_on_kill_focus($opt_id);
    };
    
    my $type = $opt->{gui_type} || $opt->{type};
    
    my $field;
    if ($type eq 'bool') {
        $field = Slic3r::GUI::OptionsGroup::Field::Checkbox->new(
            parent => $self->parent,
            option => $opt,
        );
    } elsif ($type eq 'i') {
        $field = Slic3r::GUI::OptionsGroup::Field::SpinCtrl->new(
            parent => $self->parent,
            option => $opt,
        );
    } elsif ($type eq 'color') {
        $field = Slic3r::GUI::OptionsGroup::Field::ColourPicker->new(
            parent => $self->parent,
            option => $opt,
        );
    } elsif ($type =~ /^(f|s|s@|percent)$/) {
        $field = Slic3r::GUI::OptionsGroup::Field::TextCtrl->new(
            parent => $self->parent,
            option => $opt,
        );
    } elsif ($type eq 'select' || $type eq 'select_open') {
        $field = Slic3r::GUI::OptionsGroup::Field::Choice->new(
            parent => $self->parent,
            option => $opt,
        );
    } elsif ($type eq 'f_enum_open' || $type eq 'i_enum_open' || $type eq 'i_enum_closed') {
        $field = Slic3r::GUI::OptionsGroup::Field::NumericChoice->new(
            parent => $self->parent,
            option => $opt,
        );
    } elsif ($type eq 'point') {
        $field = Slic3r::GUI::OptionsGroup::Field::Point->new(
            parent => $self->parent,
            option => $opt,
        );
    } elsif ($type eq 'slider') {
        $field = Slic3r::GUI::OptionsGroup::Field::Slider->new(
            parent => $self->parent,
            option => $opt,
        );
    }
    return undef if !$field;
    
    #! setting up a function that will be triggered when the field changes
    #! think of it as $field->on_change = ($on_change)
    $field->on_change($on_change);
    $field->on_kill_focus($on_kill_focus);
    $self->_fields->{$opt_id} = $field;
    
    return $field->isa('Slic3r::GUI::OptionsGroup::Field::wxWindow')
        ? $field->wxWindow
        : $field->wxSizer;
}

sub get_option {
    my ($self, $opt_id) = @_;
    return undef if !exists $self->_options->{$opt_id};
    return $self->_options->{$opt_id};
}

sub get_field {
    my ($self, $opt_id) = @_;
    return undef if !exists $self->_fields->{$opt_id};
    return $self->_fields->{$opt_id};
}

sub get_value {
    my ($self, $opt_id) = @_;
    
    return if !exists $self->_fields->{$opt_id};
    return $self->_fields->{$opt_id}->get_value;
}

sub set_value {
    my ($self, $opt_id, $value) = @_;
    
    return if !exists $self->_fields->{$opt_id};
    $self->_fields->{$opt_id}->set_value($value);
}

sub _on_change {
    my ($self, $opt_id, $value) = @_;
    $self->on_change->($opt_id, $value);
}

sub enable {
    my ($self) = @_;
    
    $_->enable for values %{$self->_fields};
}

sub disable {
    my ($self) = @_;
    
    $_->disable for values %{$self->_fields};
}

sub _on_kill_focus {
    my ($self, $opt_id) = @_;
    # nothing
}


package Slic3r::GUI::OptionsGroup::Line;
use Moo;

has 'label'         => (is => 'rw', default => sub { "" });
has 'full_width'    => (is => 'rw', default => sub { 0 });
has 'label_tooltip' => (is => 'rw', default => sub { "" });
has 'sizer'         => (is => 'rw');
has 'widget'        => (is => 'rw');
has '_options'      => (is => 'ro', default => sub { [] });
# Extra UI components after the label and the edit widget of the option.
has '_extra_widgets' => (is => 'ro', default => sub { [] });

# this method accepts a Slic3r::GUI::OptionsGroup::Option object
sub append_option {
    my ($self, $option) = @_;
    push @{$self->_options}, $option;
}

sub append_widget {
    my ($self, $widget) = @_;
    push @{$self->_extra_widgets}, $widget;
}

sub get_options {
    my ($self) = @_;
    return [ @{$self->_options} ];
}

sub get_extra_widgets {
    my ($self) = @_;
    return [ @{$self->_extra_widgets} ];
}


# Configuration of an option.
# This very much reflects the content of the C++ ConfigOptionDef class.
package Slic3r::GUI::OptionsGroup::Option;
use Moo;

has 'opt_id'        => (is => 'rw', required => 1);
has 'type'          => (is => 'rw', required => 1);
has 'default'       => (is => 'rw', required => 1);
has 'gui_type'      => (is => 'rw', default => sub { undef });
has 'gui_flags'     => (is => 'rw', default => sub { "" });
has 'label'         => (is => 'rw', default => sub { "" });
has 'sidetext'      => (is => 'rw', default => sub { "" });
has 'tooltip'       => (is => 'rw', default => sub { "" });
has 'multiline'     => (is => 'rw', default => sub { 0 });
has 'full_width'    => (is => 'rw', default => sub { 0 });
has 'width'         => (is => 'rw', default => sub { undef });
has 'height'        => (is => 'rw', default => sub { undef });
has 'min'           => (is => 'rw', default => sub { undef });
has 'max'           => (is => 'rw', default => sub { undef });
has 'labels'        => (is => 'rw', default => sub { [] });
has 'values'        => (is => 'rw', default => sub { [] });
has 'readonly'      => (is => 'rw', default => sub { 0 });
has 'side_widget'   => (is => 'rw', default => sub { undef });


package Slic3r::GUI::ConfigOptionsGroup;
use Moo;

use List::Util qw(first);

extends 'Slic3r::GUI::OptionsGroup';
has 'config'        => (is => 'ro', required => 1);
has 'full_labels'   => (is => 'ro', default => sub { 0 });
has '_opt_map'      => (is => 'ro', default => sub { {} });

sub get_option {
    my ($self, $opt_key, $opt_index) = @_;
    
    $opt_index //= -1;
    
    if (!$self->config->has($opt_key)) {
        die "No $opt_key in ConfigOptionsGroup config";
    }
    
    my $opt_id = ($opt_index == -1 ? $opt_key : "${opt_key}#${opt_index}");
    $self->_opt_map->{$opt_id} = [ $opt_key, $opt_index ];
    
    # Slic3r::Config::Options is a C++ Slic3r::PrintConfigDef exported as a Perl hash of hashes.
    # The C++ counterpart is a constant singleton.
    my $optdef = $Slic3r::Config::Options->{$opt_key};    # we should access this from $self->config
    my $default_value = $self->_get_config_value($opt_key, $opt_index, $optdef->{gui_flags} =~ /\bserialized\b/);
    
    return Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => $opt_id,
        type        => $optdef->{type},
        default     => $default_value,
        gui_type    => $optdef->{gui_type},
        gui_flags   => $optdef->{gui_flags},
        label       => ($self->full_labels && defined $optdef->{full_label}) ? $optdef->{full_label} : $optdef->{label},
        sidetext    => $optdef->{sidetext},
        # calling serialize() ensures we get a stringified value
        tooltip     => $optdef->{tooltip} . " (default: " . $self->config->serialize($opt_key) . ")",
        multiline   => $optdef->{multiline},
        width       => $optdef->{width},
        min         => $optdef->{min},
        max         => $optdef->{max},
        labels      => $optdef->{labels},
        values      => $optdef->{values},
        readonly    => $optdef->{readonly},
    );
}

sub create_single_option_line {
    my ($self, $opt_key, $opt_index) = @_;
    
    my $option;
    if (ref($opt_key)) {
        $option = $opt_key;
    } else {
        $option = $self->get_option($opt_key, $opt_index);
    }
    return $self->SUPER::create_single_option_line($option);
}

sub append_single_option_line {
    my ($self, $option, $opt_index) = @_;
    return $self->append_line($self->create_single_option_line($option, $opt_index));
}

# Initialize UI components with the config values.
sub reload_config {
    my ($self) = @_;
    
    foreach my $opt_id (keys %{ $self->_opt_map }) {
        my ($opt_key, $opt_index) = @{ $self->_opt_map->{$opt_id} };
        my $option = $self->_options->{$opt_id};
        $self->set_value($opt_id, $self->_get_config_value($opt_key, $opt_index, $option->gui_flags =~ /\bserialized\b/));
    }
}

sub get_fieldc {
    my ($self, $opt_key, $opt_index) = @_;
    
    $opt_index //= -1;
    my $opt_id = first { $self->_opt_map->{$_}[0] eq $opt_key && $self->_opt_map->{$_}[1] == $opt_index }
        keys %{$self->_opt_map};
    return defined($opt_id) ? $self->get_field($opt_id) : undef;
}

sub _get_config_value {
    my ($self, $opt_key, $opt_index, $deserialize) = @_;
    
    if ($deserialize) {
        # Want to edit a vector value (currently only multi-strings) in a single edit box.
        # Aggregate the strings the old way.
        # Currently used for the post_process config value only.
        die "Can't deserialize option indexed value" if $opt_index != -1;
        return join(';', @{$self->config->get($opt_key)});
    } else {
        return $opt_index == -1
            ? $self->config->get($opt_key)
            : $self->config->get_at($opt_key, $opt_index);
    }
}

sub _on_change {
    my ($self, $opt_id, $value) = @_;
    
    if (exists $self->_opt_map->{$opt_id}) {
        my ($opt_key, $opt_index) = @{ $self->_opt_map->{$opt_id} };
        my $option = $self->_options->{$opt_id};
        
        # get value
        my $field_value = $self->get_value($opt_id);
        if ($option->gui_flags =~ /\bserialized\b/) {
            die "Can't set serialized option indexed value" if $opt_index != -1;
            # Split a string to multiple strings by a semi-colon. This is the old way of storing multi-string values.
            # Currently used for the post_process config value only.
            my @values = split /;/, $field_value;
            $self->config->set($opt_key, \@values);
        } else {
            if ($opt_index == -1) {
                $self->config->set($opt_key, $field_value);
            } else {
                my $value = $self->config->get($opt_key);
                $value->[$opt_index] = $field_value;
                $self->config->set($opt_key, $value);
            }
        }
    }
    
    $self->SUPER::_on_change($opt_id, $value);
}

sub _on_kill_focus {
    my ($self, $opt_id) = @_;
    
    # when a field loses focus, reapply the config value to it
    # (thus discarding any invalid input and reverting to the last
    # accepted value)
    $self->reload_config;
}

# Static text shown among the options.
# Currently used for the filament cooling legend only.
package Slic3r::GUI::OptionsGroup::StaticText;
use Wx qw(:misc :systemsettings);
use base 'Wx::StaticText';

sub new {
    my ($class, $parent) = @_;
    
    my $self = $class->SUPER::new($parent, -1, "", wxDefaultPosition, wxDefaultSize);
    my $font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $self->SetFont($font);
    return $self;
}

sub SetText {
    my ($self, $value) = @_;
    
    $self->SetLabel($value);
    $self->Wrap(400);
    $self->GetParent->Layout;
}

1;
