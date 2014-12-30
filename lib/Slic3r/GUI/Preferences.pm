package Slic3r::GUI::Preferences;
use Wx qw(:dialog :id :misc :sizer :systemsettings wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my ($class, $parent) = @_;
    my $self = $class->SUPER::new($parent, -1, "Preferences", wxDefaultPosition, wxDefaultSize);
    $self->{values} = {};
    
    my $optgroup;
    $optgroup = Slic3r::GUI::OptionsGroup->new(
        parent  => $self,
        title   => 'General',
        on_change => sub {
            my ($opt_id) = @_;
            $self->{values}{$opt_id} = $optgroup->get_value($opt_id);
        },
        label_width => 200,
    );
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'mode',
        type        => 'select',
        label       => 'Mode',
        tooltip     => 'Choose between a simpler, basic mode and an expert mode with more options and more complicated interface.',
        labels      => ['Simple','Expert'],
        values      => ['simple','expert'],
        default     => $Slic3r::GUI::Settings->{_}{mode},
        width       => 100,
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'version_check',
        type        => 'bool',
        label       => 'Check for updates',
        tooltip     => 'If this is enabled, Slic3r will check for updates daily and display a reminder if a newer version is available.',
        default     => $Slic3r::GUI::Settings->{_}{version_check} // 1,
        readonly    => !wxTheApp->have_version_check,
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'remember_output_path',
        type        => 'bool',
        label       => 'Remember output directory',
        tooltip     => 'If this is enabled, Slic3r will prompt the last output directory instead of the one containing the input files.',
        default     => $Slic3r::GUI::Settings->{_}{remember_output_path},
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'autocenter',
        type        => 'bool',
        label       => 'Auto-center parts',
        tooltip     => 'If this is enabled, Slic3r will auto-center objects around the print bed center.',
        default     => $Slic3r::GUI::Settings->{_}{autocenter},
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'background_processing',
        type        => 'bool',
        label       => 'Background processing',
        tooltip     => 'If this is enabled, Slic3r will pre-process objects as soon as they\'re loaded in order to save time when exporting G-code.',
        default     => $Slic3r::GUI::Settings->{_}{background_processing},
        readonly    => !$Slic3r::have_threads,
    ));
    
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
    wxTheApp->save_settings;
    
    $self->EndModal(wxID_OK);
    $self->Close;  # needed on Linux
}

1;
