package Slic3r::GUI::OptionsGroup;
use Moo;

use List::Util qw(first);
use Wx qw(:combobox :font :misc :sizer :systemsettings :textctrl);
use Wx::Event qw(EVT_CHECKBOX EVT_COMBOBOX EVT_SPINCTRL EVT_TEXT EVT_KILL_FOCUS EVT_SLIDER);

=head1 NAME

Slic3r::GUI::OptionsGroup - pre-filled Wx::StaticBoxSizer wrapper containing one or more options

=head1 SYNOPSIS

    my $optgroup = Slic3r::GUI::OptionsGroup->new(
        parent  => $self->parent,
        title   => 'Layers',
        options => [
            {
                opt_key     => 'layer_height',  # mandatory
                type        => 'f',             # mandatory
                label       => 'Layer height',
                tooltip     => 'This setting controls the height (and thus the total number) of the slices/layers.',
                sidetext    => 'mm',
                width       => 200,
                full_width  => 0,
                height      => 50,
                min         => 0,
                max         => 100,
                labels      => [],
                values      => [],
                default     => 0.4,             # mandatory
                readonly    => 0,
                on_change   => sub { print "new value is $_[0]\n" },
            },
        ],
        on_change   => sub { print "new value for $_[0] is $_[1]\n" },
        no_labels   => 0,
        label_width => 180,
        extra_column => sub { ... },
    );
    $sizer->Add($optgroup->sizer);

=cut

has 'parent'        => (is => 'ro', required => 1);
has 'title'         => (is => 'ro', required => 1);
has 'options'       => (is => 'ro', required => 1, trigger => 1);
has 'lines'         => (is => 'lazy');
has 'on_change'     => (is => 'ro', default => sub { sub {} });
has 'no_labels'     => (is => 'ro', default => sub { 0 });
has 'label_width'   => (is => 'ro', default => sub { 180 });
has 'extra_column'  => (is => 'ro');
has 'label_font'    => (is => 'ro');
has 'sidetext_font' => (is => 'ro', default => sub { Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) });
has 'ignore_on_change_return' => (is => 'ro', default => sub { 1 });

has 'sizer'         => (is => 'rw');
has '_triggers'     => (is => 'ro', default => sub { {} });
has '_setters'      => (is => 'ro', default => sub { {} });

sub _trigger_options {}

sub BUILD {
    my $self = shift;
    
    {
        my $box = Wx::StaticBox->new($self->parent, -1, $self->title);
        $self->sizer(Wx::StaticBoxSizer->new($box, wxVERTICAL));
    }
    
    my $num_columns = $self->extra_column ? 3 : 2;
    my $grid_sizer = Wx::FlexGridSizer->new(scalar(@{$self->options}), $num_columns, 0, 0);
    $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    $grid_sizer->AddGrowableCol($self->no_labels ? 0 : 1);
    
    # TODO: border size may be related to wxWidgets 2.8.x vs. 2.9.x instead of wxMAC specific
    $self->sizer->Add($grid_sizer, 0, wxEXPAND | wxALL, &Wx::wxMAC ? 0 : 5);
    
    foreach my $line (@{$self->lines}) {
        if ($line->{sizer}) {
            $self->sizer->Add($line->{sizer}, 0, wxEXPAND | wxALL, &Wx::wxMAC ? 0 : 15);
        } elsif ($line->{widget}) {
            my $window = $line->{widget}->GetWindow($self->parent);
            $self->sizer->Add($window, 0, wxEXPAND | wxALL, &Wx::wxMAC ? 0 : 15);
        } else {
            $self->_build_line($line, $grid_sizer);
        }
    }
}

# default behavior: one option per line
sub _build_lines {
    my $self = shift;
    
    my $lines = [];
    foreach my $opt (@{$self->options}) {
        push @$lines, {
            label       => $opt->{label},
            sidetext    => $opt->{sidetext},
            full_width  => $opt->{full_width},
            options     => [$opt->{opt_key}],
        };
    }
    return $lines;
}

sub single_option_line {
    my $class = shift;
    my ($opt_key) = @_;
    
    return {
        label       => $Slic3r::Config::Options->{$opt_key}{label},
        sidetext    => $Slic3r::Config::Options->{$opt_key}{sidetext},
        options     => [$opt_key],
    };
}

sub _build_line {
    my $self = shift;
    my ($line, $grid_sizer) = @_;
    
    if ($self->extra_column) {
        if (defined (my $item = $self->extra_column->($line))) {
            $grid_sizer->Add($item, 0, wxALIGN_CENTER_VERTICAL, 0);
        } else {
            $grid_sizer->AddSpacer(1);
        }
    }
    
    my $label;
    if (!$self->no_labels) {
        $label = Wx::StaticText->new($self->parent, -1, $line->{label} ? "$line->{label}:" : "", wxDefaultPosition, [$self->label_width, -1]);
        $label->SetFont($self->label_font) if $self->label_font;
        $label->Wrap($self->label_width) ;  # needed to avoid Linux/GTK bug
        $grid_sizer->Add($label, 0, wxALIGN_CENTER_VERTICAL, 0);
        $label->SetToolTipString($line->{tooltip}) if $line->{tooltip};
    }
    
    my @fields = ();
    my @field_labels = ();
    foreach my $opt_key (@{$line->{options}}) {
        my $opt = first { $_->{opt_key} eq $opt_key } @{$self->options};
        push @fields, $self->_build_field($opt);
        push @field_labels, $opt->{label};
    }
    if (@fields > 1 || $line->{sidetext}) {
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        for my $i (0 .. $#fields) {
            if (@fields > 1 && $field_labels[$i]) {
                my $field_label = Wx::StaticText->new($self->parent, -1, "$field_labels[$i]:", wxDefaultPosition, wxDefaultSize);
                $field_label->SetFont($self->sidetext_font);
                $sizer->Add($field_label, 0, wxALIGN_CENTER_VERTICAL, 0);
            }
            $sizer->Add($fields[$i], 0, wxALIGN_CENTER_VERTICAL, 0);
        }
        if ($line->{sidetext}) {
            my $sidetext = Wx::StaticText->new($self->parent, -1, $line->{sidetext}, wxDefaultPosition, wxDefaultSize);
            $sidetext->SetFont($self->sidetext_font);
            $sizer->Add($sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL , 4);
        }
        $grid_sizer->Add($sizer);
    } else {
        $grid_sizer->Add($fields[0], 0, ($line->{full_width} ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
    }
}

sub _build_field {
    my $self = shift;
    my ($opt) = @_;
    
    my $opt_key = $opt->{opt_key};
    $self->_triggers->{$opt_key} = $opt->{on_change} || sub { return 1 };
    
    my $on_kill_focus = sub {
        my ($s, $event) = @_;
        
        # Without this, there will be nasty focus bugs on Windows.
        # Also, docs for wxEvent::Skip() say "In general, it is recommended to skip all 
        # non-command events to allow the default handling to take place."
        $event->Skip(1);
        
        $self->on_kill_focus($opt_key);
    };
    
    my $field;
    my $tooltip = $opt->{tooltip};
    if ($opt->{type} =~ /^(i|f|s|s@|percent|slider)$/) {
        my $style = 0;
        $style = wxTE_MULTILINE if $opt->{multiline};
        # default width on Windows is too large
        my $size = Wx::Size->new($opt->{width} || 60, $opt->{height} || -1);
        
        my $on_change = sub {
            my $value = $field->GetValue;
            $value ||= 0 if $opt->{type} =~ /^(i|f|percent)$/; # prevent crash trying to pass empty strings to Config
            $self->_on_change($opt_key, $value);
        };
        if ($opt->{type} eq 'i') {
            $field = Wx::SpinCtrl->new($self->parent, -1, $opt->{default}, wxDefaultPosition, $size, $style, $opt->{min} || 0, $opt->{max} || 2147483647, $opt->{default});
            $self->_setters->{$opt_key} = sub { $field->SetValue($_[0]) };
            EVT_SPINCTRL ($self->parent, $field, $on_change);
            EVT_TEXT ($self->parent, $field, $on_change);
            EVT_KILL_FOCUS($field, $on_kill_focus);
        } elsif ($opt->{values}) {
            $field = Wx::ComboBox->new($self->parent, -1, $opt->{default}, wxDefaultPosition, $size, $opt->{labels} || $opt->{values});
            $self->_setters->{$opt_key} = sub {
                $field->SetValue($_[0]);
            };
            EVT_COMBOBOX($self->parent, $field, sub {
                # Without CallAfter, the field text is not populated on Windows.
                Slic3r::GUI->CallAfter(sub {
                    $field->SetValue($opt->{values}[ $field->GetSelection ]);  # set the text field to the selected value
                    $self->_on_change($opt_key, $on_change);
                });
            });
            EVT_TEXT($self->parent, $field, $on_change);
            EVT_KILL_FOCUS($field, $on_kill_focus);
        } elsif ($opt->{type} eq 'slider') {
            my $scale = 10;
            $field = Wx::BoxSizer->new(wxHORIZONTAL);
            my $slider = Wx::Slider->new($self->parent, -1, ($opt->{default} // $opt->{min})*$scale, ($opt->{min} // 0)*$scale, ($opt->{max} // 100)*$scale, wxDefaultPosition, $size);
            my $statictext = Wx::StaticText->new($self->parent, -1, $slider->GetValue/$scale);
            $field->Add($_, 0, wxALIGN_CENTER_VERTICAL, 0) for $slider, $statictext;
            $self->_setters->{$opt_key} = sub {
                $field->SetValue($_[0]*$scale);
            };
            EVT_SLIDER($self->parent, $slider, sub {
                my $value = $slider->GetValue/$scale;
                $statictext->SetLabel($value);
                $self->_on_change($opt_key, $value);
            });
        } else {
            $field = Wx::TextCtrl->new($self->parent, -1, $opt->{default}, wxDefaultPosition, $size, $style);
            # value supplied to the setter callback might be undef in case user loads a config
            # that has empty string for multi-value options like 'wipe'
            $self->_setters->{$opt_key} = sub { $field->ChangeValue($_[0]) if defined $_[0] };
            EVT_TEXT($self->parent, $field, $on_change);
            EVT_KILL_FOCUS($field, $on_kill_focus);
        }
        $field->Disable if $opt->{readonly};
        $tooltip .= " (default: " . $opt->{default} .  ")" if ($opt->{default});
    } elsif ($opt->{type} eq 'bool') {
        $field = Wx::CheckBox->new($self->parent, -1, "");
        $field->SetValue($opt->{default});
        $field->Disable if $opt->{readonly};
        EVT_CHECKBOX($self->parent, $field, sub { $self->_on_change($opt_key, $field->GetValue); });
        $self->_setters->{$opt_key} = sub { $field->SetValue($_[0]) };
        $tooltip .= " (default: " . ($opt->{default} ? 'yes' : 'no') .  ")" if defined($opt->{default});
    } elsif ($opt->{type} eq 'point') {
        $field = Wx::BoxSizer->new(wxHORIZONTAL);
        my $field_size = Wx::Size->new(40, -1);
        my @items = (
            Wx::StaticText->new($self->parent, -1, "x:"),
                my $x_field = Wx::TextCtrl->new($self->parent, -1, $opt->{default}->[0], wxDefaultPosition, $field_size),
            Wx::StaticText->new($self->parent, -1, "  y:"),
                my $y_field = Wx::TextCtrl->new($self->parent, -1, $opt->{default}->[1], wxDefaultPosition, $field_size),
        );
        $field->Add($_, 0, wxALIGN_CENTER_VERTICAL, 0) for @items;
        if ($tooltip) {
            $_->SetToolTipString(
                $tooltip . " (default: " .  join(",", @{$opt->{default}}) .  ")"
            ) for @items;
        }
        foreach my $field ($x_field, $y_field) {
            EVT_TEXT($self->parent, $field, sub { $self->_on_change($opt_key, [ $x_field->GetValue, $y_field->GetValue ]) });
            EVT_KILL_FOCUS($field, $on_kill_focus);
        }
        $self->_setters->{$opt_key} = sub {
            $x_field->SetValue($_[0][0]);
            $y_field->SetValue($_[0][1]);
        };
    } elsif ($opt->{type} eq 'select') {
        $field = Wx::ComboBox->new($self->parent, -1, "", wxDefaultPosition, wxDefaultSize, $opt->{labels} || $opt->{values}, wxCB_READONLY);
        EVT_COMBOBOX($self->parent, $field, sub {
            $self->_on_change($opt_key, $opt->{values}[$field->GetSelection]);
        });
        $self->_setters->{$opt_key} = sub {
            $field->SetSelection(grep $opt->{values}[$_] eq $_[0], 0..$#{$opt->{values}});
        };
        $self->_setters->{$opt_key}->($opt->{default});

        $tooltip .= " (default: " 
                 . $opt->{labels}[ first { $opt->{values}[$_] eq $opt->{default} } 0..$#{$opt->{values}} ] 
                 . ")" if ($opt->{default});
    } else {
        die "Unsupported option type: " . $opt->{type};
    }
    if ($tooltip && $field->can('SetToolTipString')) {
        $field->SetToolTipString($tooltip);
    }
    return $field;
}

sub _option {
    my $self = shift;
    my ($opt_key) = @_;
    
    return first { $_->{opt_key} eq $opt_key } @{$self->options};
}

sub _on_change {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    return if $self->sizer->GetStaticBox->GetParent->{disabled};
    $self->_triggers->{$opt_key}->($value) or $self->ignore_on_change_return or return;
    $self->on_change->($opt_key, $value);
}

=head2 set_value

This method accepts an option key and a value. If this option group contains the supplied
option key, its field will be updated with the new value and the method will return a true
value, otherwise it will return false.

=cut

sub set_value {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    if ($self->_setters->{$opt_key}) {
        $self->_setters->{$opt_key}->($value);
        $self->_on_change($opt_key, $value);
        return 1;
    }
    
    return 0;
}

sub on_kill_focus {}

package Slic3r::GUI::ConfigOptionsGroup;
use Moo;

extends 'Slic3r::GUI::OptionsGroup';

=head1 NAME

Slic3r::GUI::ConfigOptionsGroup - pre-filled Wx::StaticBoxSizer wrapper containing one or more config options

=head1 SYNOPSIS

    my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
        parent      => $self->parent,
        title       => 'Layers',
        config      => $config,
        options     => ['layer_height'],
        on_change   => sub { print "new value for $_[0] is $_[1]\n" },
        no_labels   => 0,
        label_width => 180,
    );
    $sizer->Add($optgroup->sizer);

=cut

use List::Util qw(first);

has 'config' => (is => 'ro', required => 1);
has 'full_labels' => (is => 'ro', default => sub {0});
has '+ignore_on_change_return' => (is => 'ro', default => sub { 0 });

sub _trigger_options {
    my $self = shift;
    
    @{$self->options} = map {
        my $opt = $_;
        if (ref $opt ne 'HASH') {
            my $full_key = $opt;
            my ($opt_key, $index) = $self->_split_key($full_key);
            my $config_opt = $Slic3r::Config::Options->{$opt_key};
            
            $opt = {
                opt_key     => $full_key,
                config      => 1,
                label       => ($self->full_labels && defined $config_opt->{full_label}) ? $config_opt->{full_label} : $config_opt->{label},
                (map { $_   => $config_opt->{$_} } qw(type tooltip sidetext width height full_width min max labels values multiline readonly)),
                default     => $self->_get_config($opt_key, $index),
                on_change   => sub { return $self->_set_config($opt_key, $index, $_[0]) },
            };
        }
        $opt;
    } @{$self->options};
}

sub _option {
    my $self = shift;
    my ($opt_key) = @_;
    
    return first { $_->{opt_key} =~ /^\Q$opt_key\E(#.+)?$/ } @{$self->options};
}

sub set_value {
    my $self = shift;
    my ($opt_key, $value) = @_;
    
    my $opt = $self->_option($opt_key) or return 0; 
    
    # if user is setting a non-config option, forward the call to the parent
    if (!$opt->{config}) {
        return $self->SUPER::set_value($opt_key, $value);
    }
    
    my $changed = 0;
    foreach my $full_key (keys %{$self->_setters}) {
        my ($key, $index) = $self->_split_key($full_key);
        
        if ($key eq $opt_key) {
            $self->config->set($key, $value);
            $self->SUPER::set_value($full_key, $self->_get_config($key, $index));
            $changed = 1;
        }
    }
    return $changed;
}

sub on_kill_focus {
    my ($self, $full_key) = @_;
    
    # when a field loses focus, reapply the config value to it
    # (thus discarding any invalid input and reverting to the last
    # accepted value)
    my ($key, $index) = $self->_split_key($full_key);
    $self->SUPER::set_value($full_key, $self->_get_config($key, $index));
}

sub _split_key {
    my $self = shift;
    my ($opt_key) = @_;
    
    my $index;
    $opt_key =~ s/#(\d+)$// and $index = $1;
    return ($opt_key, $index);
}

sub _get_config {
    my $self = shift;
    my ($opt_key, $index, $config) = @_;
    
    my ($get_m, $serialized) = $self->_config_methods($opt_key, $index);
    my $value = ($config // $self->config)->$get_m($opt_key);
    if (defined $index) {
        $value->[$index] //= $value->[0]; #/
        $value = $value->[$index];
    }
    return $value;
}

sub _set_config {
    my $self = shift;
    my ($opt_key, $index, $value) = @_;
    
    my ($get_m, $serialized) = $self->_config_methods($opt_key, $index);
    if (defined $index) {
        my $values = $self->config->$get_m($opt_key);
        $values->[$index] = $value;
        
        # ignore set() return value
        $self->config->set($opt_key, $values);
    } else {
        if ($serialized) {        
            # ignore set_deserialize() return value
            return $self->config->set_deserialize($opt_key, $value);
        } else {        
            # ignore set() return value
            return $self->config->set($opt_key, $value);
        }
    }
}

sub _config_methods {
    my $self = shift;
    my ($opt_key, $index) = @_;
    
    # if it's an array type but no index was specified, use the serialized version
    return ($Slic3r::Config::Options->{$opt_key}{type} =~ /\@$/ && !defined $index)
        ? qw(serialize 1)
        : qw(get 0);
}

package Slic3r::GUI::OptionsGroup::StaticTextLine;
use Moo;
use Wx qw(:misc :systemsettings);

sub GetWindow {
    my $self = shift;
    my ($parent) = @_;
    
    $self->{statictext} = Wx::StaticText->new($parent, -1, "foo", wxDefaultPosition, wxDefaultSize);
    my $font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $self->{statictext}->SetFont($font);
    return $self->{statictext};
}

sub SetText {
    my $self = shift;
    my ($value) = @_;
    
    $self->{statictext}->SetLabel($value);
    $self->{statictext}->Wrap(400);
    $self->{statictext}->GetParent->Layout;
}

1;
