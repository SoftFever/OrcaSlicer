# A printer "Controller" -> "ManualControlDialog" subtab, opened per 3D printer connected?

package Slic3r::GUI::Controller::ManualControlDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:dialog :id :misc :sizer :choicebook :button :bitmap
    wxBORDER_NONE wxTAB_TRAVERSAL);
use Wx::Event qw(EVT_CLOSE EVT_BUTTON);
use base qw(Wx::Dialog Class::Accessor);

__PACKAGE__->mk_accessors(qw(sender config2 x_homed y_homed));

sub new {
    my ($class, $parent, $config, $sender) = @_;
    
    my $self = $class->SUPER::new($parent, -1, "Manual Control", wxDefaultPosition,
        [500,380], wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    $self->sender($sender);
    
    $self->config2({
        xy_travel_speed     => 130,
        z_travel_speed      => 10,
    });
    
    my $bed_sizer = Wx::FlexGridSizer->new(2, 3, 1, 1);
    $bed_sizer->AddGrowableCol(1, 1);
    $bed_sizer->AddGrowableRow(0, 1);
    
    my $move_button = sub {
        my ($sizer, $label, $icon, $bold, $pos, $handler) = @_;
        
        my $btn = Wx::Button->new($self, -1, $label, wxDefaultPosition, wxDefaultSize,
            wxBU_LEFT | wxBU_EXACTFIT);
        $btn->SetFont($bold ? $Slic3r::GUI::small_bold_font : $Slic3r::GUI::small_font);
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("$icon.png"), wxBITMAP_TYPE_PNG));
        $btn->SetBitmapPosition($pos);
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
            
            if (!($self->x_homed && $self->y_homed)) {
                Slic3r::GUI::show_error($self, "Please home both X and Y before moving.");
                return ;
            }
            
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
    
    # XYZ home button
    $move_button->($bed_sizer, 'XYZ', 'house', 1, wxTOP, sub { $self->home(undef) });
    
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
    
    my $optgroup = Slic3r::GUI::OptionsGroup->new(
        parent      => $self,
        title       => 'Settings',
        on_change   => sub {
            my ($opt_id, $value) = @_;
            $self->config2->{$opt_id} = $value;
        },
    );
    {
        my $line = Slic3r::GUI::OptionsGroup::Line->new(
            label => 'Speed (mm/s)',
        );
        $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id      => 'xy_travel_speed',
            type        => 'f',
            label       => 'X/Y',
            tooltip     => '',
            default     => $self->config2->{xy_travel_speed},
        ));
        $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id      => 'z_travel_speed',
            type        => 'f',
            label       => 'Z',
            tooltip     => '',
            default     => $self->config2->{z_travel_speed},
        ));
        $optgroup->append_line($line);
    }
    
    my $main_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $main_sizer->Add($bed_sizer, 1, wxEXPAND | wxALL, 10);
    $main_sizer->Add($optgroup->sizer, 0, wxEXPAND | wxALL, 10);
    #$main_sizer->Add($self->CreateButtonSizer(wxCLOSE), 0, wxEXPAND);
    #EVT_BUTTON($self, wxID_CLOSE, sub { $self->Close });
    
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
    $self->sender->send(sprintf("G1 X%.1f Y%.1f F%d", @$pos, $self->config2->{xy_travel_speed}*60), 1);
    $self->{canvas}->set_pos($pos);
}

sub rel_move {
    my ($self, $axis, $distance) = @_;
    
    my $speed = ($axis eq 'Z') ? $self->config2->{z_travel_speed} : $self->config2->{xy_travel_speed};
    $self->sender->send("G91", 1); # set relative positioning
    $self->sender->send(sprintf("G1 %s%.1f F%d", $axis, $distance, $speed*60), 1);
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
    
    $axis //= '';
    $self->sender->send(sprintf("G28 %s", $axis), 1);
    $self->{canvas}->set_pos(undef);
    $self->x_homed(1) if $axis eq 'X';
    $self->y_homed(1) if $axis eq 'Y';
}

1;
