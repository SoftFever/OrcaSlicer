package Slic3r::GUI::OptionsGroup;
use strict;
use warnings;

use Wx qw(:combobox :font :misc :sizer :systemsettings :textctrl);
use Wx::Event qw(EVT_CHECKBOX EVT_COMBOBOX EVT_SPINCTRL EVT_TEXT);
use base 'Wx::StaticBoxSizer';


# not very elegant, but this solution is temporary waiting for a better GUI
our %reload_callbacks = (); # key => $cb
our %fields = ();           # $key => [$control]

sub new {
    my $class = shift;
    my ($parent, %p) = @_;
    
    my $box = Wx::StaticBox->new($parent, -1, $p{title});
    my $self = $class->SUPER::new($box, wxVERTICAL);
    
    my $grid_sizer = Wx::FlexGridSizer->new(scalar(@{$p{options}}), 2, ($p{no_labels} ? 1 : 2), 0);
    $grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    $grid_sizer->AddGrowableCol($p{no_labels} ? 0 : 1);
    
    # grab the default font, to fix Windows font issues/keep things consistent
    my $bold_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $bold_font->SetWeight(wxFONTWEIGHT_BOLD);
    my $sidetext_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    
    my $onChange = $p{on_change} || sub {};
    
    foreach my $opt_key (@{$p{options}}) {
        my $opt = $Slic3r::Config::Options->{$opt_key};
        my $label;
        if (!$p{no_labels}) {
            $label = Wx::StaticText->new($parent, -1, "$opt->{label}:", wxDefaultPosition, [$p{label_width} || 180, -1]);
            $label->Wrap($p{label_width} || 180) ;  # needed to avoid Linux/GTK bug
            $grid_sizer->Add($label);
            
            # set the bold font point size to the same size as all the other labels (for consistency)
            $bold_font->SetPointSize($label->GetFont()->GetPointSize());
            $label->SetFont($bold_font) if $opt->{important};
        }
        
        my $field;
        if ($opt->{type} =~ /^(i|f|s|s@)$/) {
            my $style = 0;
            $style = wxTE_MULTILINE if $opt->{multiline};
            my $size = Wx::Size->new($opt->{width} || -1, $opt->{height} || -1);
            
            my ($get, $set) = $opt->{type} eq 's@' ? qw(serialize deserialize) : qw(get_raw set);
            
            if ($opt->{type} eq 'i') {
                my $value = Slic3r::Config->$get($opt_key);
                $field = Wx::SpinCtrl->new($parent, -1, $value, wxDefaultPosition, $size, $style, $opt->{min} || 0, $opt->{max} || 100, $value);
                EVT_SPINCTRL($parent, $field, sub { Slic3r::Config->$set($opt_key, $field->GetValue); $onChange->($opt_key) });
            } else {
                $field = Wx::TextCtrl->new($parent, -1, Slic3r::Config->$get($opt_key), wxDefaultPosition, $size, $style);
                EVT_TEXT($parent, $field, sub { Slic3r::Config->$set($opt_key, $field->GetValue); $onChange->($opt_key) });
            }
            $reload_callbacks{$opt_key} = sub { $field->SetValue(Slic3r::Config->$get($opt_key)) };
        } elsif ($opt->{type} eq 'bool') {
            $field = Wx::CheckBox->new($parent, -1, "");
            $field->SetValue(Slic3r::Config->get_raw($opt_key));
            EVT_CHECKBOX($parent, $field, sub { Slic3r::Config->set($opt_key, $field->GetValue); $onChange->($opt_key) });
            $reload_callbacks{$opt_key} = sub { $field->SetValue(Slic3r::Config->get_raw($opt_key)) };
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
            EVT_TEXT($parent, $x_field, sub { $set_value->(0, $x_field->GetValue); $onChange->($opt_key) });
            EVT_TEXT($parent, $y_field, sub { $set_value->(1, $y_field->GetValue); $onChange->($opt_key) });
            $reload_callbacks{$opt_key} = sub {
                my $value = Slic3r::Config->get_raw($opt_key);
                $x_field->SetValue($value->[0]);
                $y_field->SetValue($value->[1]);
            };
            $fields{$opt_key} = [$x_field, $y_field];
        } elsif ($opt->{type} eq 'select') {
            $field = Wx::ComboBox->new($parent, -1, "", wxDefaultPosition, wxDefaultSize, $opt->{labels} || $opt->{values}, wxCB_READONLY);
            EVT_COMBOBOX($parent, $field, sub {
                Slic3r::Config->set($opt_key, $opt->{values}[$field->GetSelection]);
                $onChange->($opt_key);
            });
            $reload_callbacks{$opt_key} = sub {
                my $value = Slic3r::Config->get_raw($opt_key);
                $field->SetSelection(grep $opt->{values}[$_] eq $value, 0..$#{$opt->{values}});
            };
            $reload_callbacks{$opt_key}->();
        } else {
            die "Unsupported option type: " . $opt->{type};
        }
        $label->SetToolTipString($opt->{tooltip}) if $label && $opt->{tooltip};
        $field->SetToolTipString($opt->{tooltip}) if $opt->{tooltip} && $field->can('SetToolTipString');
        if ($opt->{sidetext}) {
            my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
            $sizer->Add($field);
            my $sidetext = Wx::StaticText->new($parent, -1, $opt->{sidetext}, wxDefaultPosition, [-1, -1]);
            $sidetext->SetFont($sidetext_font);
            $sizer->Add($sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL , 4);
            $grid_sizer->Add($sizer);
        } else {
            $grid_sizer->Add($field, 0, $opt->{full_width} ? wxEXPAND : 0);
        }
        $fields{$opt_key} ||= [$field];
    }
    
    $self->Add($grid_sizer, 0, wxEXPAND);
    
    return $self;
}

1;
