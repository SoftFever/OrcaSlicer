package Slic3r::GUI::OptionsGroup;
use strict;
use warnings;

use Wx qw(:sizer);
use Wx::Event qw(EVT_TEXT EVT_CHECKBOX);
use base 'Wx::StaticBoxSizer';

sub new {
    my $class = shift;
    my ($parent, %p) = @_;
    
    my $box = Wx::StaticBox->new($parent, -1, $p{title});
    my $self = $class->SUPER::new($box, wxVERTICAL);
    
    my $grid_sizer = Wx::FlexGridSizer->new(scalar(@{$p{options}}), 2, 2, 0);
    
    foreach my $opt (@{$p{options}}) {
        my $label = Wx::StaticText->new($parent, -1, "$opt->{label}:", Wx::wxDefaultPosition, [180,-1]);
        my $field;
        if ($opt->{type} =~ /^(i|f)$/) {
            $field = Wx::TextCtrl->new($parent, -1, ${$opt->{value}});
            EVT_TEXT($parent, $field, sub { ${$opt->{value}} = $field->GetValue });
        } elsif ($opt->{type} eq 'bool') {
            $field = Wx::CheckBox->new($parent, -1, "");
            $field->SetValue(${$opt->{value}});
            EVT_TEXT($parent, $field, sub { ${$opt->{value}} = $field->GetValue });
        } elsif ($opt->{type} eq 'point') {
            $field = Wx::BoxSizer->new(wxHORIZONTAL);
            my $field_size = Wx::Size->new(40, -1);
            $field->Add($_) for (
                Wx::StaticText->new($parent, -1, "x:"),
                my $x_field = Wx::TextCtrl->new($parent, -1, ${$opt->{value}}->[0], Wx::wxDefaultPosition, $field_size),
                Wx::StaticText->new($parent, -1, "  y:"),
                my $y_field = Wx::TextCtrl->new($parent, -1, ${$opt->{value}}->[1], Wx::wxDefaultPosition, $field_size),
            );
            EVT_TEXT($parent, $x_field, sub { ${$opt->{value}}->[0] = $x_field->GetValue });
            EVT_TEXT($parent, $y_field, sub { ${$opt->{value}}->[1] = $y_field->GetValue });
        } else {
            die "Unsupported option type: " . $opt->{type};
        }
        $grid_sizer->Add($_) for $label, $field;
    }
    
    $self->Add($grid_sizer, 0, wxEXPAND);
    
    return $self;
}

1;
