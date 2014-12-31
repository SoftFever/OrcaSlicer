package Slic3r::GUI::Controller::PrinterPanel;
use strict;
use warnings;
use utf8;

use Wx qw(:panel :id :misc :sizer :button :bitmap);
use Wx::Event qw(EVT_BUTTON);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(sender));

sub new {
    my ($class, $parent) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    
    $self->{sizer} = my $sizer = Wx::StaticBoxSizer->new(Wx::StaticBox->new($self, -1, "Printer"), wxVERTICAL);
    
    # connection info
    {
        my $conn_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($conn_sizer, 0, wxEXPAND);
        {
            my $text = Wx::StaticText->new($self, -1, "Port:", wxDefaultPosition, wxDefaultSize);
            $conn_sizer->Add($text, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        }
        {
            $self->{serial_port_combobox} = Wx::ComboBox->new($self, -1, "", wxDefaultPosition, wxDefaultSize, []);
            $self->scan_serial_ports;
            $conn_sizer->Add($self->{serial_port_combobox}, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 1);
        }
        {
            $self->{btn_rescan_serial} = my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/arrow_rotate_clockwise.png", wxBITMAP_TYPE_PNG),
                wxDefaultPosition, wxDefaultSize, &Wx::wxBORDER_NONE);
            $conn_sizer->Add($btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
            EVT_BUTTON($self, $btn, sub { $self->scan_serial_ports });
        }
        {
            my $text = Wx::StaticText->new($self, -1, "Speed:", wxDefaultPosition, wxDefaultSize);
            $conn_sizer->Add($text, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        }
        {
            $self->{serial_speed_combobox} = Wx::ComboBox->new($self, -1, "250000", wxDefaultPosition, wxDefaultSize,
                ["115200", "250000"]);
            $conn_sizer->Add($self->{serial_speed_combobox}, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        }
    }
    
    # buttons
    {
        my $buttons_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($buttons_sizer, 0, wxEXPAND);
        {
            $self->{btn_connect} = my $btn = Wx::Button->new($self, -1, "Connect", wxDefaultPosition, wxDefaultSize);
            $buttons_sizer->Add($btn, 0, wxRIGHT, 5);
            EVT_BUTTON($self, $btn, \&connect);
        }
        {
            $self->{btn_disconnect} = my $btn = Wx::Button->new($self, -1, "Disconnect", wxDefaultPosition, wxDefaultSize);
            $buttons_sizer->Add($btn, 0, wxRIGHT, 5);
            EVT_BUTTON($self, $btn, \&disconnect);
        }
    }
    
    # status
    $self->{status_text} = Wx::StaticText->new($self, -1, "Not connected", wxDefaultPosition, wxDefaultSize);
    $sizer->Add($self->{status_text}, 0, wxEXPAND);
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    
    $self->_update_connection_controls;
    
    return $self;
}

sub _update_connection_controls {
    my ($self) = @_;
    
    if ($self->sender && $self->sender->is_connected) {
        $self->{btn_connect}->Hide;
        $self->{btn_disconnect}->Show;
        $self->{serial_port_combobox}->Disable;
        $self->{serial_speed_combobox}->Disable;
        $self->{btn_rescan_serial}->Disable;
    } else {
        $self->{btn_connect}->Show;
        $self->{btn_disconnect}->Hide;
        $self->{serial_port_combobox}->Enable;
        $self->{serial_speed_combobox}->Enable;
        $self->{btn_rescan_serial}->Enable;
    }
}

sub set_status {
    my ($self, $status) = @_;
    $self->{status_text}->SetLabel($status);
    $self->{status_text}->Refresh;
}

sub connect {
    my ($self) = @_;
    
    return if $self->sender && $self->sender->is_connected;
    
    $self->set_status("Connecting...");
    $self->sender(Slic3r::GCode::Sender->new);
    my $res = $self->sender->connect(
        $self->{serial_port_combobox}->GetValue,
        $self->{serial_speed_combobox}->GetValue,
    );
    if (!$res) {
        $self->set_status("Connection failed");
    }
    1 until $self->sender->is_connected;
    $self->set_status("Connected");
    $self->_update_connection_controls;
}

sub disconnect {
    my ($self) = @_;
    
    return if !$self->sender || !$self->sender->is_connected;
    
    $self->sender->disconnect;
    $self->set_status("Not connected");
    $self->_update_connection_controls;
}

sub scan_serial_ports {
    my ($self) = @_;
    
    $self->{serial_port_combobox}->Clear;
    
    # TODO: Windows ports
    
    # UNIX and OS X
    $self->{serial_port_combobox}->Append($_)
        for glob '/dev/{ttyUSB,ttyACM,tty.,cu.,rfcomm}*';
}

1;
