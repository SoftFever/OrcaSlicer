package Slic3r::GUI::OptionsGroup;
use strict;
use warnings;

use Wx qw(:sizer wxSYS_DEFAULT_GUI_FONT);
use Wx::Event qw(EVT_TEXT EVT_CHECKBOX EVT_CHOICE);
use base 'Wx::StaticBoxSizer';


# not very elegant, but this solution is temporary waiting for a better GUI
our @reload_callbacks = ();
our %fields = ();  # $key => [$control]

sub new {
    my $class = shift;
    my ($parent, %p) = @_;
    
    my $box = Wx::StaticBox->new($parent, -1, $p{title});
    my $self = $class->SUPER::new($box, wxVERTICAL);
    
    my $grid_sizer = Wx::FlexGridSizer->new(scalar(@{$p{options}}), 2, 2, 0);

    #grab the default font, to fix Windows font issues/keep things consistent
    my $bold_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $bold_font->SetWeight(&Wx::wxFONTWEIGHT_BOLD);

    
    foreach my $opt_key (@{$p{options}}) {
        my $index;
        $opt_key =~ s/#(\d+)$// and $index = $1;
        
        my $opt = $Slic3r::Config::Options->{$opt_key};
        my $label = Wx::StaticText->new($parent, -1, "$opt->{label}:", Wx::wxDefaultPosition,
            [$p{label_width} || 180, -1]);
        $label->Wrap($p{label_width} || 180);  # needed to avoid Linux/GTK bug
        
        #set the bold font point size to the same size as all the other labels (for consistency)
        $bold_font->SetPointSize($label->GetFont()->GetPointSize());
        $label->SetFont($bold_font) if $opt->{important};
        my $field;
        if ($opt->{type} =~ /^(i|f|s|s@)$/) {
            my $style = 0;
            my $size = Wx::wxDefaultSize;
            
            if ($opt->{multiline}) {
                $style = &Wx::wxTE_MULTILINE;
                $size = Wx::Size->new($opt->{width} || -1, $opt->{height} || -1);
            }
            
            # if it's an array type but no index was specified, use the serialized version
            my ($get_m, $set_m) = $opt->{type} =~ /\@$/ && !defined $index
                ? qw(serialize deserialize)
                : qw(get_raw set);
            
            my $get = sub {
                my $val = Slic3r::Config->$get_m($opt_key);
                $val = $val->[$index] if defined $index;
                return $val;
            };
            $field = Wx::TextCtrl->new($parent, -1, $get->(), Wx::wxDefaultPosition, $size, $style);
            push @reload_callbacks, sub { $field->SetValue($get->()) };
            
            my $set = sub {
                my $val = $field->GetValue;
                if (defined $index) {
                    Slic3r::Config->$get_m($opt_key)->[$index] = $val;
                } else {
                    Slic3r::Config->$set_m($opt_key, $val);
                }
            };
            EVT_TEXT($parent, $field, sub { $set->() });
        } elsif ($opt->{type} eq 'bool') {
            $field = Wx::CheckBox->new($parent, -1, "");
            $field->SetValue(Slic3r::Config->get_raw($opt_key));
            EVT_CHECKBOX($parent, $field, sub { Slic3r::Config->set($opt_key, $field->GetValue) });
            push @reload_callbacks, sub { $field->SetValue(Slic3r::Config->get_raw($opt_key)) };
        } elsif ($opt->{type} eq 'point') {
            $field = Wx::BoxSizer->new(wxHORIZONTAL);
            my $field_size = Wx::Size->new(40, -1);
            my $value = Slic3r::Config->get_raw($opt_key);
            $field->Add($_) for (
                Wx::StaticText->new($parent, -1, "x:"),
                my $x_field = Wx::TextCtrl->new($parent, -1, $value->[0], Wx::wxDefaultPosition, $field_size),
                Wx::StaticText->new($parent, -1, "  y:"),
                my $y_field = Wx::TextCtrl->new($parent, -1, $value->[1], Wx::wxDefaultPosition, $field_size),
            );
            my $set_value = sub {
                my ($i, $value) = @_;
                my $val = Slic3r::Config->get_raw($opt_key);
                $val->[$i] = $value;
                Slic3r::Config->set($opt_key, $val);
            };
            EVT_TEXT($parent, $x_field, sub { $set_value->(0, $x_field->GetValue) });
            EVT_TEXT($parent, $y_field, sub { $set_value->(1, $y_field->GetValue) });
            push @reload_callbacks, sub {
                my $value = Slic3r::Config->get_raw($opt_key);
                $x_field->SetValue($value->[0]);
                $y_field->SetValue($value->[1]);
            };
            $fields{$opt_key} = [$x_field, $y_field];
        } elsif ($opt->{type} eq 'select') {
            $field = Wx::Choice->new($parent, -1, Wx::wxDefaultPosition, Wx::wxDefaultSize, $opt->{labels} || $opt->{values});
            EVT_CHOICE($parent, $field, sub {
                Slic3r::Config->set($opt_key, $opt->{values}[$field->GetSelection]);
            });
            push @reload_callbacks, sub {
                my $value = Slic3r::Config->get_raw($opt_key);
                $field->SetSelection(grep $opt->{values}[$_] eq $value, 0..$#{$opt->{values}});
            };
            $reload_callbacks[-1]->();
        } else {
            die "Unsupported option type: " . $opt->{type};
        }
        $grid_sizer->Add($_) for $label, $field;
        $fields{$opt_key} ||= [$field];
    }
    
    $self->Add($grid_sizer, 0, wxEXPAND);
    
    return $self;
}

1;
