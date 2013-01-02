package Slic3r::GUI::Preferences;
use Wx qw(:dialog :id :misc :sizer :systemsettings);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Preferences", wxDefaultPosition, [500,200]);
    
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
                default     => 'simple',
            },
        ],
        label_width => 100,
    );
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    EVT_BUTTON($self, wxID_OK, sub { $self->EndModal(wxID_OK); });
    $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    return $self;
}

1;
