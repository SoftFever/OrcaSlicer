package Slic3r::GUI::Preferences;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Preferences", wxDefaultPosition, [500,200]);
    $self->{values};
    
    my $optgroup = Slic3r::GUI::OptionsGroup->new(
        parent  => $self,
        title   => 'General',
        options => [
            {
                opt_key     => 'mode',
                type        => 'select',
                label       => 'Mode',
                tooltip     => 'Choose between a simpler, basic mode and an expert mode with more options and more complicated interface.',
                labels      => ['Simple','Expert'],
                values      => ['simple','expert'],
                default     => $Slic3r::GUI::Settings->{_}{mode},
            },
        ],
        on_change => sub { $self->{values}{$_[0]} = $_[1] },
        label_width => 100,
    );
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    EVT_BUTTON($self, wxID_OK, sub { $self->_accept });
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

sub _accept {
    my $self = shift;
    $self->EndModal(wxID_OK);
    
    if ($self->{values}{mode}) {
        Slic3r::GUI::warning_catcher($self)->("You need to restart Slic3r to make the changes effective.");
    }
    
    $Slic3r::GUI::Settings->{_}{$_} = $self->{values}{$_} for keys %{$self->{values}};
    Slic3r::GUI->save_settings;
}

1;
