# Preferences dialog, opens from Menu: File->Preferences

package Slic3r::GUI::Preferences;
use Wx qw(:dialog :id :misc :sizer :systemsettings wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my ($class, $parent) = @_;
    my $self = $class->SUPER::new($parent, -1, "Preferences", wxDefaultPosition, wxDefaultSize);
    $self->{values} = {};
    
    my $app_config = wxTheApp->{app_config};
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
#    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
#        opt_id      => 'version_check',
#        type        => 'bool',
#        label       => 'Check for updates',
#        tooltip     => 'If this is enabled, Slic3r will check for updates daily and display a reminder if a newer version is available.',
#        default     => $app_config->get("version_check") // 1,
#        readonly    => !wxTheApp->have_version_check,
#    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'remember_output_path',
        type        => 'bool',
        label       => 'Remember output directory',
        tooltip     => 'If this is enabled, Slic3r will prompt the last output directory instead of the one containing the input files.',
        default     => $app_config->get("remember_output_path"),
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'autocenter',
        type        => 'bool',
        label       => 'Auto-center parts',
        tooltip     => 'If this is enabled, Slic3r will auto-center objects around the print bed center.',
        default     => $app_config->get("autocenter"),
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'background_processing',
        type        => 'bool',
        label       => 'Background processing',
        tooltip     => 'If this is enabled, Slic3r will pre-process objects as soon as they\'re loaded in order to save time when exporting G-code.',
        default     => $app_config->get("background_processing"),
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'no_controller',
        type        => 'bool',
        label       => 'Disable USB/serial connection',
        tooltip     => 'Disable communication with the printer over a serial / USB cable. This simplifies the user interface in case the printer is never attached to the computer.',
        default     => $app_config->get("no_controller"),
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'no_defaults',
        type        => 'bool',
        label       => 'Suppress "- default -" presets',
        tooltip     => 'Suppress "- default -" presets in the Print / Filament / Printer selections once there are any other valid presets available.',
        default     => $app_config->get("no_defaults"),
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'show_incompatible_presets',
        type        => 'bool',
        label       => 'Show incompatible print and filament presets',
        tooltip     => 'When checked, the print and filament presets are shown in the preset editor even ' .
                       'if they are marked as incompatible with the active printer',
        default     => $app_config->get("show_incompatible_presets"),
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'use_legacy_opengl',
        type        => 'bool',
        label       => 'Use legacy OpenGL 1.1 rendering',
        tooltip     => 'If you have rendering issues caused by a buggy OpenGL 2.0 driver, you may try to check this checkbox. This will disable the layer height editing and anti aliasing, so it is likely better to upgrade your graphics driver.',
        default     => $app_config->get("use_legacy_opengl"),
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
    my ($self) = @_;
    
    if (defined($self->{values}{no_controller}) ||
        defined($self->{values}{no_defaults}) ||
        defined($self->{values}{use_legacy_opengl})) {
        Slic3r::GUI::warning_catcher($self)->("You need to restart Slic3r to make the changes effective.");
    }
    
    my $app_config = wxTheApp->{app_config};
    $app_config->set($_, $self->{values}{$_}) for keys %{$self->{values}};
    
    $self->EndModal(wxID_OK);
    $self->Close;  # needed on Linux

    # Nothify the UI to update itself from the ini file.
    wxTheApp->update_ui_from_settings;
}

1;
