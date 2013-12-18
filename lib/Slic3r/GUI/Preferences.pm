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
            {
                opt_key     => 'version_check',
                type        => 'bool',
                label       => 'Check for updates',
                tooltip     => 'If this is enabled, Slic3r will check for updates daily and display a reminder if a newer version is available.',
                default     => $Slic3r::GUI::Settings->{_}{version_check} // 1,
                readonly    => !Slic3r::GUI->have_version_check,
            },
            {
                opt_key     => 'remember_output_path',
                type        => 'bool',
                label       => 'Remember output directory',
                tooltip     => 'If this is enabled, Slic3r will prompt the last output directory instead of the one containing the input files.',
                default     => $Slic3r::GUI::Settings->{_}{remember_output_path},
            },
            {
                opt_key     => 'autocenter',
                type        => 'bool',
                label       => 'Auto-center parts',
                tooltip     => 'If this is enabled, Slic3r will auto-center objects around the configured print center.',
                default     => $Slic3r::GUI::Settings->{_}{autocenter},
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
    
    if ($self->{values}{mode}) {
        Slic3r::GUI::warning_catcher($self)->("You need to restart Slic3r to make the changes effective.");
    }
    
    $Slic3r::GUI::Settings->{_}{$_} = $self->{values}{$_} for keys %{$self->{values}};
    Slic3r::GUI->save_settings;
    
    $self->EndModal(wxID_OK);
    $self->Close;  # needed on Linux
}

1;
