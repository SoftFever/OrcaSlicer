package Slic3r::GUI::Controller::ManualControlDialog;
use strict;
use warnings;
use utf8;

use Slic3r::Geometry qw(PI X Y unscale);
use Wx qw(:dialog :id :misc :sizer :choicebook :button :bitmap
    wxBORDER_NONE wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON);
use base qw(Wx::Dialog Class::Accessor);

__PACKAGE__->mk_accessors(qw(sender));

use constant TRAVEL_SPEED => 130*60;  # TODO: make customizable?

sub new {
    my ($class, $parent, $config, $sender) = @_;
    
    my $self = $class->SUPER::new($parent, -1, "Manual Control", wxDefaultPosition,
        [430,380], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->sender($sender);
    
    my $bed_sizer = Wx::FlexGridSizer->new(2, 3, 1, 1);
    $bed_sizer->AddGrowableCol(1, 1);
    $bed_sizer->AddGrowableRow(0, 1);
    
    my $move_button = sub {
        my ($sizer, $label, $icon, $bold, $pos, $handler) = @_;
        
        my $btn = Wx::Button->new($self, -1, $label, wxDefaultPosition, wxDefaultSize,
            wxBU_LEFT | wxBU_EXACTFIT);
        $btn->SetFont($bold ? $Slic3r::GUI::small_bold_font : $Slic3r::GUI::small_font);
        if ($Slic3r::GUI::have_button_icons) {
            $btn->SetBitmap(Wx::Bitmap->new("$Slic3r::var/$icon.png", wxBITMAP_TYPE_PNG));
            $btn->SetBitmapPosition($pos);
        }
        EVT_BUTTON($self, $btn, $handler);
        $sizer->Add($btn, 1, wxEXPAND | wxALL, 0);
    };
    
    # Y buttons
    {
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        for my $d (qw(+10 +1 +0.1)) {
            $move_button->($sizer, $d, 'arrow_up', 0, wxLEFT, sub { $self->rel_move('Y', $d) });
        }
        $move_button->($sizer, 'Y', 'house', 1, wxLEFT, sub { $self->home('Y') });
        for my $d (qw(-0.1 -1 -10)) {
            $move_button->($sizer, $d, 'arrow_down', 0, wxLEFT, sub { $self->rel_move('Y', $d) });
        };
        $bed_sizer->Add($sizer, 1, wxEXPAND, 0);
    }
    
    # Bed canvas
    {
        my $bed_shape = $config->bed_shape;
        $self->{canvas} = my $canvas = Slic3r::GUI::2DBed->new($self, $bed_shape);
        $canvas->interactive(1);
        $canvas->on_move(sub {
            my ($pos) = @_;
            
            # delete any pending commands to get a smoother movement
            $self->sender->purge_queue(1);
            $self->abs_xy_move($pos);
        });
        $bed_sizer->Add($canvas, 0, wxEXPAND | wxRIGHT, 3);
    }
    
    # Z buttons
    {
        my $sizer = Wx::BoxSizer->new(wxVERTICAL);
        for my $d (qw(+10 +1 +0.1)) {
            $move_button->($sizer, $d, 'arrow_up', 0, wxLEFT, sub { $self->rel_move('Z', $d) });
        }
        $move_button->($sizer, 'Z', 'house', 1, wxLEFT, sub { $self->home('Z') });
        for my $d (qw(-0.1 -1 -10)) {
            $move_button->($sizer, $d, 'arrow_down', 0, wxLEFT, sub { $self->rel_move('Z', $d) });
        };
        $bed_sizer->Add($sizer, 1, wxEXPAND, 0);
    }
    
    $bed_sizer->AddSpacer(0);
    
    # X buttons
    {
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        for my $d (qw(-10 -1 -0.1)) {
            $move_button->($sizer, $d, 'arrow_left', 0, wxTOP, sub { $self->rel_move('X', $d) });
        }
        $move_button->($sizer, 'X', 'house', 1, wxTOP, sub { $self->home('X') });
        for my $d (qw(+0.1 +1 +10)) {
            $move_button->($sizer, $d, 'arrow_right', 0, wxTOP, sub { $self->rel_move('X', $d) });
        }
        $bed_sizer->Add($sizer, 1, wxEXPAND, 0);
    }
    
    my $main_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $main_sizer->Add($bed_sizer, 1, wxEXPAND | wxALL, 10);
    $main_sizer->Add($self->CreateButtonSizer(wxCLOSE), 0, wxEXPAND);
    EVT_BUTTON($self, wxID_CLOSE, sub { $self->Close });
    
    $self->SetSizer($main_sizer);
    $self->SetMinSize($self->GetSize);
    #$main_sizer->SetSizeHints($self);
    $self->Layout;
    
    # needed to actually free memory
    EVT_CLOSE($self, sub {
        $self->EndModal(wxID_OK);
        $self->Destroy;
    });
    
    return $self;
}

sub abs_xy_move {
    my ($self, $pos) = @_;
    
    $self->sender->send("G90", 1); # set absolute positioning
    $self->sender->send(sprintf("G1 X%.1f Y%.1f F%d", @$pos, TRAVEL_SPEED), 1);
    $self->{canvas}->set_pos($pos);
}

sub rel_move {
    my ($self, $axis, $distance) = @_;
    
    $self->sender->send("G91", 1); # set relative positioning
    $self->sender->send(sprintf("G1 %s%.1f F%d", $axis, $distance, TRAVEL_SPEED), 1);
    $self->sender->send("G90", 1); # set absolute positioning
    
    if (my $pos = $self->{canvas}->pos) {
        if ($axis eq 'X') {
            $pos->translate($distance, 0);
        } elsif ($axis eq 'Y') {
            $pos->translate(0, $distance);
        }
        $self->{canvas}->set_pos($pos);
    }
}

sub home {
    my ($self, $axis) = @_;
    
    $self->sender->send(sprintf("G28 %s", $axis), 1);
    $self->{canvas}->set_pos(undef);
}

1;
