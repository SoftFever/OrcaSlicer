package Slic3r::GUI::BonjourBrowser;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :choicebook wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE);
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1, "Device Browser", wxDefaultPosition, [350,700], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    
    # look for devices
    eval "use Net::Bonjour; 1";
    my $res = Net::Bonjour->new('http');
    $res->discover;
    $self->{devices} = [ $res->entries ];
    
    # label
    my $text = Wx::StaticText->new($self, -1, "Choose an OctoPrint device in your network:", wxDefaultPosition, wxDefaultSize);
    
    # selector
    $self->{choice} = my $choice = Wx::Choice->new($self, -1, wxDefaultPosition, wxDefaultSize,
        [ map $_->name, @{$self->{devices}} ]);
    
    my $main_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $main_sizer->Add($text, 1, wxEXPAND | wxALL, 10);
    $main_sizer->Add($choice, 1, wxEXPAND | wxALL, 10);
    $main_sizer->Add($self->CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
    
    $self->SetSizer($main_sizer);
    $self->SetMinSize($self->GetSize);
    $main_sizer->SetSizeHints($self);
    
    # needed to actually free memory
    EVT_CLOSE($self, sub {
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    return $self;
}

sub GetValue {
    my ($self) = @_;
    return $self->{devices}[ $self->{choice}->GetSelection ]->address;
}
sub GetPort {
    my ($self) = @_;
    return $self->{devices}[ $self->{choice}->GetSelection ]->port;
}

1;
