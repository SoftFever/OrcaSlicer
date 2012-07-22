package Slic3r::GUI::OptionsGroup;
use strict;
use warnings;

use Wx qw(:combobox :font :misc :sizer :systemsettings :textctrl);
use Wx::Event qw(EVT_CHECKBOX EVT_COMBOBOX EVT_SPINCTRL EVT_TEXT);
use base 'Wx::StaticBoxSizer';


# not very elegant, but this solution is temporary waiting for a better GUI
our %reload_callbacks = (); # key => $cb

sub new {
    my $class = shift;
    my ($parent, %p) = @_;
    
    my $box = Wx::StaticBox->new($parent, -1, $p{title});
    my $self = $class->SUPER::new($box, wxVERTICAL);
    
    my $grid_sizer = Wx::FlexGridSizer->new(scalar(@{$p{options}}), 2, ($p{no_labels} ? 1 : 2), 0);
    $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    $grid_sizer->AddGrowableCol($p{no_labels} ? 0 : 1);
    
    my $sidetext_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    
    my $onChange = $p{on_change} || sub {};
    my $make_cb = sub {
        my $cb = shift;
        return sub {
            $cb->(@_) if !$parent->{disabled};
        };
    };
    
    foreach my $original_opt_key (@{$p{options}}) {
        my $index;
        my $opt_key = $original_opt_key;  # leave original one untouched
        $opt_key =~ s/#(\d+)$// and $index = $1;
        
        my $opt = $Slic3r::Config::Options->{$opt_key};
        my $label;
        if (!$p{no_labels}) {
            $label = Wx::StaticText->new($parent, -1, "$opt->{label}:", wxDefaultPosition, [$p{label_width} || 180, -1]);
            $label->Wrap($p{label_width} || 180) ;  # needed to avoid Linux/GTK bug
            $grid_sizer->Add($label);
        }
        
        my $field;
        if ($opt->{type} =~ /^(i|f|s|s@)$/) {
            my $style = 0;
            $style = wxTE_MULTILINE if $opt->{multiline};
            my $size = Wx::Size->new($opt->{width} || -1, $opt->{height} || -1);
            
            # if it's an array type but no index was specified, use the serialized version
            my ($get_m, $set_m) = $opt->{type} =~ /\@$/ && !defined $index
                ? qw(serialize deserialize)
                : qw(get_raw set);
            
            my $get = sub {
                my $val = Slic3r::Config->$get_m($opt_key);
                if (defined $index) {
                    $val = $val->[$index]; #/
                }
                return $val;
            };
            $field = $opt->{type} eq 'i'
                ? Wx::SpinCtrl->new($parent, -1, $get->(), wxDefaultPosition, $size, $style, $opt->{min} || 0, $opt->{max} || 100, $get->())
                : Wx::TextCtrl->new($parent, -1, $get->(), wxDefaultPosition, $size, $style);
            $reload_callbacks{$opt_key} = $make_cb->(sub { $field->SetValue($get->()) });
            
            my $set = sub {
                my $val = $field->GetValue;
                if (defined $index) {
                    Slic3r::Config->$get_m($opt_key)->[$index] = $val;
                } else {
                    Slic3r::Config->$set_m($opt_key, $val);
                }
                $onChange->($opt_key);
            };
            $opt->{type} eq 'i'
                ? EVT_SPINCTRL($parent, $field, $set)
                : EVT_TEXT($parent, $field, $set);
        } elsif ($opt->{type} eq 'bool') {
            $field = Wx::CheckBox->new($parent, -1, "");
            $field->SetValue(Slic3r::Config->get_raw($opt_key));
            EVT_CHECKBOX($parent, $field, $make_cb->(sub { Slic3r::Config->set($opt_key, $field->GetValue); $onChange->($opt_key) }));
            $reload_callbacks{$opt_key} = $make_cb->(sub { $field->SetValue(Slic3r::Config->get_raw($opt_key)) });
        } elsif ($opt->{type} eq 'point') {
            $field = Wx::BoxSizer->new(wxHORIZONTAL);
            my $field_size = Wx::Size->new(40, -1);
            my $value = Slic3r::Config->get_raw($opt_key);
            my @items = (
                Wx::StaticText->new($parent, -1, "x:"),
                my $x_field = Wx::TextCtrl->new($parent, -1, $value->[0], wxDefaultPosition, $field_size),
                Wx::StaticText->new($parent, -1, "  y:"),
                my $y_field = Wx::TextCtrl->new($parent, -1, $value->[1], wxDefaultPosition, $field_size),
            );
            $field->Add($_) for @items;
            if ($opt->{tooltip}) {
                $_->SetToolTipString($opt->{tooltip}) for @items;
            }
            my $set_value = sub {
                my ($i, $value) = @_;
                my $val = Slic3r::Config->get_raw($opt_key);
                $val->[$i] = $value;
                Slic3r::Config->set($opt_key, $val);
            };
            EVT_TEXT($parent, $x_field, $make_cb->(sub { $set_value->(0, $x_field->GetValue); $onChange->($opt_key) }));
            EVT_TEXT($parent, $y_field, $make_cb->(sub { $set_value->(1, $y_field->GetValue); $onChange->($opt_key) }));
            $reload_callbacks{$opt_key} = $make_cb->(sub {
                my $value = Slic3r::Config->get_raw($opt_key);
                $x_field->SetValue($value->[0]);
                $y_field->SetValue($value->[1]);
            });
        } elsif ($opt->{type} eq 'select') {
            $field = Wx::ComboBox->new($parent, -1, "", wxDefaultPosition, wxDefaultSize, $opt->{labels} || $opt->{values}, wxCB_READONLY);
            EVT_COMBOBOX($parent, $field, $make_cb->(sub {
                Slic3r::Config->set($opt_key, $opt->{values}[$field->GetSelection]);
                $onChange->($opt_key);
            }));
            $reload_callbacks{$opt_key} = $make_cb->(sub {
                my $value = Slic3r::Config->get_raw($opt_key);
                $field->SetSelection(grep $opt->{values}[$_] eq $value, 0..$#{$opt->{values}});
            });
            $reload_callbacks{$opt_key}->();
        } else {
            die "Unsupported option type: " . $opt->{type};
        }
        $label->SetToolTipString($opt->{tooltip}) if $label && $opt->{tooltip};
        $field->SetToolTipString($opt->{tooltip}) if $opt->{tooltip} && $field->can('SetToolTipString');
        if ($opt->{sidetext}) {
            my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
            $sizer->Add($field);
            my $sidetext = Wx::StaticText->new($parent, -1, $opt->{sidetext}, wxDefaultPosition, wxDefaultSize);
            $sidetext->SetFont($sidetext_font);
            $sizer->Add($sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL , 4);
            $grid_sizer->Add($sizer);
        } else {
            $grid_sizer->Add($field, 0, $opt->{full_width} ? wxEXPAND : 0);
        }
    }
    
    $self->Add($grid_sizer, 0, wxEXPAND);
    
    return $self;
}

1;
