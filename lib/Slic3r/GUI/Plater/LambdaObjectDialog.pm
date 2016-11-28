# Generate an anonymous or "lambda" 3D object. This gets used with the Add Generic option in Settings.
# 

package Slic3r::GUI::Plater::LambdaObjectDialog;
use strict;
use warnings;
use utf8;

use Slic3r::Geometry qw(PI X);
use Wx qw(wxTheApp :dialog :id :misc :sizer wxTAB_TRAVERSAL wxCB_READONLY wxTE_PROCESS_TAB);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON EVT_COMBOBOX EVT_TEXT);
use Scalar::Util qw(looks_like_number);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, "Lambda Object", wxDefaultPosition, [500,500], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    # Note whether the window was already closed, so a pending update is not executed.
    $self->{already_closed} = 0;
    $self->{object_parameters} = { 
        type => "box",
        dim => [1, 1, 1],
    };

    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    my $button_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    my $button_ok = $self->CreateStdDialogButtonSizer(wxOK);
    my $button_cancel = $self->CreateStdDialogButtonSizer(wxCANCEL);
    $button_sizer->Add($button_ok);
    $button_sizer->Add($button_cancel);
    EVT_BUTTON($self, wxID_OK, sub {
        # validate user input
        return if !$self->CanClose;
        
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    EVT_BUTTON($self, wxID_CANCEL, sub {
        # validate user input
        return if !$self->CanClose;
        
        $self->EndModal(wxID_CANCEL);
        $self->Destroy;
    });
    
    my $optgroup;
    $optgroup = $self->{optgroup} = Slic3r::GUI::OptionsGroup->new(
        parent      => $self,
        title       => 'Add Generic...',
        on_change   => sub {
            # Do validation
            my ($opt_id) = @_;
            if ($opt_id == 0 || $opt_id == 1 || $opt_id == 2) {
                if (!looks_like_number($optgroup->get_value($opt_id))) {
                    return 0;
                }
            }
            $self->{object_parameters}->{dim}[$opt_id] = $optgroup->get_value($opt_id);
        },
        label_width => 100,
    );
    my @options = ("box", "cylinder");
    $self->{type} = Wx::ComboBox->new($self, 1, "box", wxDefaultPosition, wxDefaultSize, \@options, wxCB_READONLY);

    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id  =>  0,
        label   =>  'L',
        type    =>  'f',
        default =>  '1',
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id  =>  1,
        label   =>  'W',
        type    =>  'f',
        default =>  '1',
    ));
    $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id  =>  2,
        label   =>  'H',
        type    =>  'f',
        default =>  '1',
    ));
    EVT_COMBOBOX($self, 1, sub{ 
        $self->{object_parameters}->{type} = $self->{type}->GetValue();
    });


    $optgroup->sizer->Add($self->{type}, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    $self->{sizer}->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    $self->{sizer}->Add($button_sizer,0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);

    $self->SetSizer($self->{sizer});
    $self->{sizer}->Fit($self);
    $self->{sizer}->SetSizeHints($self);

    
    return $self;
}
sub CanClose {
    return 1;
}
sub ObjectParameter {
    my ($self) = @_;
    return $self->{object_parameters};
}
1;
