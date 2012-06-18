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
    
    my $grid_sizer = Wx::FlexGridSizer->new(scalar(@{$p{options}}), 2, ($p{no_labels} ? 1 : 2), 0);

    # grab the default font, to fix Windows font issues/keep things consistent
    my $bold_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $bold_font->SetWeight(&Wx::wxFONTWEIGHT_BOLD);
    my $sidetext_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $sidetext_font->SetPointSize(12);
    
    foreach my $opt_key (@{$p{options}}) {
        my $opt = $Slic3r::Config::Options->{$opt_key};
        my $label;
        if (!$p{no_labels}) {
            $label = Wx::StaticText->new($parent, -1, "$opt->{label}:", Wx::wxDefaultPosition, [$p{label_width} || 180, -1]);
            $label->Wrap($p{label_width} || 180) ;  # needed to avoid Linux/GTK bug
            $grid_sizer->Add($label);
            
            # set the bold font point size to the same size as all the other labels (for consistency)
            $bold_font->SetPointSize($label->GetFont()->GetPointSize());
            $label->SetFont($bold_font) if $opt->{important};
        }
        
        my $field;
        if ($opt->{type} =~ /^(i|f|s|s@)$/) {
            my $style = 0;
            $style = &Wx::wxTE_MULTILINE if $opt->{multiline};
            my $size = Wx::Size->new($opt->{width} || -1, $opt->{height} || -1);
            
            my ($get, $set) = $opt->{type} eq 's@' ? qw(serialize deserialize) : qw(get set);
            
            $field = Wx::TextCtrl->new($parent, -1, Slic3r::Config->$get($opt_key),
                Wx::wxDefaultPosition, $size, $style);
            EVT_TEXT($parent, $field, sub { Slic3r::Config->$set($opt_key, $field->GetValue) });
            push @reload_callbacks, sub { $field->SetValue(Slic3r::Config->$get($opt_key)) };
        } elsif ($opt->{type} eq 'bool') {
            $field = Wx::CheckBox->new($parent, -1, "");
            $field->SetValue(Slic3r::Config->get($opt_key));
            EVT_CHECKBOX($parent, $field, sub { Slic3r::Config->set($opt_key, $field->GetValue) });
            push @reload_callbacks, sub { $field->SetValue(Slic3r::Config->get($opt_key)) };
        } elsif ($opt->{type} eq 'point') {
            $field = Wx::BoxSizer->new(wxHORIZONTAL);
            my $field_size = Wx::Size->new(40, -1);
            my $value = Slic3r::Config->get($opt_key);
            $field->Add($_) for (
                Wx::StaticText->new($parent, -1, "x:"),
                my $x_field = Wx::TextCtrl->new($parent, -1, $value->[0], Wx::wxDefaultPosition, $field_size),
                Wx::StaticText->new($parent, -1, "  y:"),
                my $y_field = Wx::TextCtrl->new($parent, -1, $value->[1], Wx::wxDefaultPosition, $field_size),
            );
            my $set_value = sub {
                my ($i, $value) = @_;
                my $val = Slic3r::Config->get($opt_key);
                $val->[$i] = $value;
                Slic3r::Config->set($opt_key, $val);
            };
            EVT_TEXT($parent, $x_field, sub { $set_value->(0, $x_field->GetValue) });
            EVT_TEXT($parent, $y_field, sub { $set_value->(1, $y_field->GetValue) });
            push @reload_callbacks, sub {
                my $value = Slic3r::Config->get($opt_key);
                $x_field->SetValue($value->[0]);
                $y_field->SetValue($value->[1]);
            };
            $fields{$opt_key} = [$x_field, $y_field];
        } elsif ($opt->{type} eq 'select') {
            $field = Wx::ComboBox->new($parent, -1, "", Wx::wxDefaultPosition, Wx::wxDefaultSize, $opt->{labels} || $opt->{values}, &Wx::wxCB_READONLY);
            EVT_CHOICE($parent, $field, sub {
                Slic3r::Config->set($opt_key, $opt->{values}[$field->GetSelection]);
            });
            push @reload_callbacks, sub {
                my $value = Slic3r::Config->get($opt_key);
                $field->SetSelection(grep $opt->{values}[$_] eq $value, 0..$#{$opt->{values}});
            };
            $reload_callbacks[-1]->();
        } else {
            die "Unsupported option type: " . $opt->{type};
        }
        if ($opt->{sidetext}) {
            my $sizer = Wx::BoxSizer->new(&Wx::wxHORIZONTAL);
            $sizer->Add($field);
            my $sidetext = Wx::StaticText->new($parent, -1, $opt->{sidetext}, Wx::wxDefaultPosition, [-1, -1]);
            $sidetext->SetFont($sidetext_font);
            $sizer->Add($sidetext, 0, &Wx::wxLEFT, 4);
            $grid_sizer->Add($sizer);
        } else {
            $grid_sizer->Add($field);
        }
        $fields{$opt_key} ||= [$field];
    }
    
    $self->Add($grid_sizer, 0, wxEXPAND);
    
    return $self;
}

1;
