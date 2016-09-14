# Status bar at the bottom of the main screen.

package Slic3r::GUI::ProgressStatusBar;
use strict;
use warnings;

use Wx qw(:gauge :misc);
use base 'Wx::StatusBar';

sub new {
    my $class = shift;
    my $self = $class->SUPER::new(@_);
    
    $self->{busy} = 0;
    $self->{timer} = Wx::Timer->new($self);
    $self->{prog} = Wx::Gauge->new($self, wxGA_HORIZONTAL, 100, wxDefaultPosition, wxDefaultSize);
    $self->{prog}->Hide;
    $self->{cancelbutton} = Wx::Button->new($self, -1, "Cancel", wxDefaultPosition, wxDefaultSize);
    $self->{cancelbutton}->Hide;
    
    $self->SetFieldsCount(3);
    $self->SetStatusWidths(-1, 150, 155);
    
    Wx::Event::EVT_TIMER($self, \&OnTimer, $self->{timer});
    Wx::Event::EVT_SIZE($self, \&OnSize);
    Wx::Event::EVT_BUTTON($self, $self->{cancelbutton}, sub {
        $self->{cancel_cb}->();
        $self->{cancelbutton}->Hide;
    });
    
    return $self;
}

sub DESTROY {
    my $self = shift;    
    $self->{timer}->Stop if $self->{timer} && $self->{timer}->IsRunning;
}

sub OnSize {
    my ($self, $event) = @_;
    
    my %fields = (
        # 0 is reserved for status text
        1 => $self->{cancelbutton},
        2 => $self->{prog},
    );

    foreach (keys %fields) {
        my $rect = $self->GetFieldRect($_);
        my $offset = &Wx::wxGTK ? 1 : 0; # add a cosmetic 1 pixel offset on wxGTK
        my $pos = [$rect->GetX + $offset, $rect->GetY + $offset];
        $fields{$_}->Move($pos);
        $fields{$_}->SetSize($rect->GetWidth - $offset, $rect->GetHeight);
    }

    $event->Skip;
}

sub OnTimer {
    my ($self, $event) = @_;
    
    if ($self->{prog}->IsShown) {
        $self->{timer}->Stop;
    }
    $self->{prog}->Pulse if $self->{_busy};
}

sub SetCancelCallback {
    my $self = shift;
    my ($cb) = @_;
    $self->{cancel_cb} = $cb;
    $cb ? $self->{cancelbutton}->Show : $self->{cancelbutton}->Hide;
}

sub Run {
    my $self = shift;
    my $rate = shift || 100;
    if (!$self->{timer}->IsRunning) {
        $self->{timer}->Start($rate);
    }
}

sub GetProgress {
    my $self = shift;
    return $self->{prog}->GetValue;
}

sub SetProgress {
    my $self = shift;
    my ($val) = @_;
    if (!$self->{prog}->IsShown) {
        $self->ShowProgress(1);
    }
    if ($val == $self->{prog}->GetRange) {
        $self->{prog}->SetValue(0);
        $self->ShowProgress(0);
    } else {
        $self->{prog}->SetValue($val);
    }
}

sub SetRange {
    my $self = shift;
    my ($val) = @_;
    
    if ($val != $self->{prog}->GetRange) {
        $self->{prog}->SetRange($val);
    }
}

sub ShowProgress {
    my $self = shift;
    my ($show) = @_;
    
    $self->{prog}->Show($show);
    $self->{prog}->Pulse;
}

sub StartBusy {
    my $self = shift;
    my $rate = shift || 100;
    
    $self->{_busy} = 1;
    $self->ShowProgress(1);
    if (!$self->{timer}->IsRunning) {
        $self->{timer}->Start($rate);
    }
}

sub StopBusy {
    my $self = shift;
    
    $self->{timer}->Stop;
    $self->ShowProgress(0);
    $self->{prog}->SetValue(0);
    $self->{_busy} = 0;
}

sub IsBusy {
    my $self = shift;
    return $self->{_busy};
}

1;
