package Slic3r::GUI::OptionsGroup;
use strict;
use warnings;

use Wx qw(:sizer);
use Wx::Event qw(EVT_TEXT EVT_CHECKBOX EVT_CHOICE);
use base 'Wx::StaticBoxSizer';

# not very elegant, but this solution is temporary waiting for a better GUI
our @reload_callbacks = ();

sub new {
    my $class = shift;
    my ($parent, %p) = @_;
    
    my $box = Wx::StaticBox->new($parent, -1, $p{title});
    my $self = $class->SUPER::new($box, wxVERTICAL);
    
    my $grid_sizer = Wx::FlexGridSizer->new(scalar(@{$p{options}}), 2, 2, 0);
    
    foreach my $opt_key (@{$p{options}}) {
        my $opt = $Slic3r::Config::Options->{$opt_key};
        my $label = Wx::StaticText->new($parent, -1, "$opt->{label}:", Wx::wxDefaultPosition, [180,-1]);
        $label->Wrap(180);  # needed to avoid Linux/GTK bug
        my $field;
        if ($opt->{type} =~ /^(i|f)$/) {
            $field = Wx::TextCtrl->new($parent, -1, Slic3r::Config->get($opt_key));
            EVT_TEXT($parent, $field, sub { Slic3r::Config->set($opt_key, $field->GetValue) });
            push @reload_callbacks, sub { $field->SetValue(Slic3r::Config->get($opt_key)) };
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
        } elsif ($opt->{type} eq 'select') {
            $field = Wx::Choice->new($parent, -1, Wx::wxDefaultPosition, Wx::wxDefaultSize, $opt->{values});
            EVT_CHOICE($parent, $field, sub { Slic3r::Config->set($opt_key, $opt->{values}[$field->GetSelection]) });
            push @reload_callbacks, sub {
                my $value = Slic3r::Config->get($opt_key);
                $field->SetSelection(grep $opt->{values}[$_] eq $value, 0..$#{$opt->{values}});
            };
            $reload_callbacks[-1]->();
        } else {
            die "Unsupported option type: " . $opt->{type};
        }
        $grid_sizer->Add($_) for $label, $field;
    }
    
    $self->Add($grid_sizer, 0, wxEXPAND);
    
    return $self;
}

1;
