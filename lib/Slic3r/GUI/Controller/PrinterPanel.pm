package Slic3r::GUI::Controller::PrinterPanel;
use strict;
use warnings;
use utf8;

use Wx qw(wxTheApp :panel :id :misc :sizer :button :bitmap :window :gauge :timer);
use Wx::Event qw(EVT_BUTTON EVT_MOUSEWHEEL EVT_TIMER);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(printer_name config sender jobs 
    printing print_status_timer));

use constant PRINT_STATUS_TIMER_INTERVAL => 1000;  # milliseconds

sub new {
    my ($class, $parent, $printer_name, $config) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, [500, 250]);
    
    $self->printer_name($printer_name || 'Printer');
    $self->config($config);
    $self->jobs([]);
    
    {
        my $timer_id = &Wx::NewId();
        $self->print_status_timer(Wx::Timer->new($self, $timer_id));
        EVT_TIMER($self, $timer_id, sub {
            my ($self, $event) = @_;
            
            return if !$self->printing;
            my $queue_size = $self->sender->queue_size;
            printf "queue = %d\n", $queue_size;
            $self->{gauge}->SetValue($self->{gauge}->GetRange - $queue_size);
            if ($queue_size == 0) {
                $self->print_completed;
                return;
            }
            # TODO: get temperature messages
        });
    }
    
    my $box = Wx::StaticBox->new($self, -1, "");
    my $sizer = Wx::StaticBoxSizer->new($box, wxHORIZONTAL);
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    # printer name
    {
        my $text = Wx::StaticText->new($box, -1, $self->printer_name, wxDefaultPosition, [250,-1]);
        my $font = $text->GetFont;
        $font->SetPointSize(20);
        $text->SetFont($font);
        $left_sizer->Add($text, 0, wxEXPAND, 0);
    }
    
    # connection info
    {
        my $conn_sizer = Wx::FlexGridSizer->new(2, 2, 1, 0);
        $conn_sizer->SetFlexibleDirection(wxHORIZONTAL);
        $conn_sizer->AddGrowableCol(1, 1);
        $left_sizer->Add($conn_sizer, 0, wxEXPAND | wxTOP, 5);
        {
            my $text = Wx::StaticText->new($box, -1, "Port:", wxDefaultPosition, wxDefaultSize);
            $text->SetFont($Slic3r::GUI::small_font);
            $conn_sizer->Add($text, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        }
        my $serial_port_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        {
            $self->{serial_port_combobox} = Wx::ComboBox->new($box, -1, $config->serial_port, wxDefaultPosition, wxDefaultSize, []);
            $self->{serial_port_combobox}->SetFont($Slic3r::GUI::small_font);
            $self->update_serial_ports;
            $serial_port_sizer->Add($self->{serial_port_combobox}, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 1);
        }
        {
            $self->{btn_rescan_serial} = my $btn = Wx::BitmapButton->new($box, -1, Wx::Bitmap->new("$Slic3r::var/arrow_rotate_clockwise.png", wxBITMAP_TYPE_PNG),
                wxDefaultPosition, wxDefaultSize, &Wx::wxBORDER_NONE);
            $serial_port_sizer->Add($btn, 0, wxALIGN_CENTER_VERTICAL, 0);
            EVT_BUTTON($self, $btn, sub { $self->update_serial_ports });
        }
        $conn_sizer->Add($serial_port_sizer, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        
        {
            my $text = Wx::StaticText->new($box, -1, "Speed:", wxDefaultPosition, wxDefaultSize);
            $text->SetFont($Slic3r::GUI::small_font);
            $conn_sizer->Add($text, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
        }
        my $serial_speed_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        {
            $self->{serial_speed_combobox} = Wx::ComboBox->new($box, -1, $config->serial_speed, wxDefaultPosition, wxDefaultSize,
                ["115200", "250000"]);
            $self->{serial_speed_combobox}->SetFont($Slic3r::GUI::small_font);
            $serial_speed_sizer->Add($self->{serial_speed_combobox}, 0, wxALIGN_CENTER_VERTICAL, 0);
        }
        {
            $self->{btn_disconnect} = my $btn = Wx::Button->new($box, -1, "Disconnect", wxDefaultPosition, wxDefaultSize);
            $btn->SetFont($Slic3r::GUI::small_font);
            if ($Slic3r::GUI::have_button_icons) {
                $btn->SetBitmap(Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG));
            }
            $serial_speed_sizer->Add($btn, 0, wxLEFT, 5);
            EVT_BUTTON($self, $btn, \&disconnect);
        }
        $conn_sizer->Add($serial_speed_sizer, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
    }
    
    # buttons
    {
        $self->{btn_connect} = my $btn = Wx::Button->new($box, -1, "Connect", wxDefaultPosition, [-1, 40]);
        my $font = $btn->GetFont;
        $font->SetPointSize($font->GetPointSize + 2);
        $btn->SetFont($font);
        if ($Slic3r::GUI::have_button_icons) {
            $btn->SetBitmap(Wx::Bitmap->new("$Slic3r::var/arrow_up.png", wxBITMAP_TYPE_PNG));
        }
        $left_sizer->Add($btn, 0, wxTOP, 15);
        EVT_BUTTON($self, $btn, \&connect);
    }
    
    # print progress bar
    {
        my $gauge = $self->{gauge} = Wx::Gauge->new($self, wxGA_HORIZONTAL, 100, wxDefaultPosition, wxDefaultSize);
        $left_sizer->Add($self->{gauge}, 0, wxEXPAND | wxTOP, 15);
        $gauge->Hide;
    }
    
    # status
    $self->{status_text} = Wx::StaticText->new($box, -1, "", wxDefaultPosition, [250,-1]);
    $left_sizer->Add($self->{status_text}, 0, wxEXPAND | wxTOP, 15);
    
    # print jobs panel
    my $print_jobs_sizer = Wx::BoxSizer->new(wxVERTICAL);
    {
        my $text = Wx::StaticText->new($box, -1, "Queue:", wxDefaultPosition, wxDefaultSize);
        $text->SetFont($Slic3r::GUI::small_font);
        $print_jobs_sizer->Add($text, 0, wxEXPAND, 0);
        
        $self->{jobs_panel} = Wx::ScrolledWindow->new($box, -1, wxDefaultPosition, wxDefaultSize, wxBORDER_SUNKEN);
        $self->{jobs_panel}->SetScrollbars(0, 1, 0, 1);
        $self->{jobs_panel_sizer} = Wx::BoxSizer->new(wxVERTICAL);
        $self->{jobs_panel}->SetSizer($self->{jobs_panel_sizer});
        $print_jobs_sizer->Add($self->{jobs_panel}, 1, wxEXPAND, 0);
    }
    
    $sizer->Add($left_sizer, 0, wxEXPAND | wxALL, 0);
    $sizer->Add($print_jobs_sizer, 1, wxEXPAND | wxALL, 0);
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    $self->_update_connection_controls;
    $self->set_status('Printer is offline. Click the Connect button.');
    
    return $self;
}

sub is_connected {
    my ($self) = @_;
    return $self->sender && $self->sender->is_connected;
}

sub _update_connection_controls {
    my ($self) = @_;
    
    $self->{btn_connect}->Show;
    $self->{btn_disconnect}->Hide;
    $self->{serial_port_combobox}->Enable;
    $self->{serial_speed_combobox}->Enable;
    $self->{btn_rescan_serial}->Enable;
    
    if ($self->is_connected) {
        $self->{btn_connect}->Hide;
        if (!$self->printing) {
            $self->{btn_disconnect}->Show;
        }
        $self->{serial_port_combobox}->Disable;
        $self->{serial_speed_combobox}->Disable;
        $self->{btn_rescan_serial}->Disable;
    }
    
    $self->Layout;
}

sub set_status {
    my ($self, $status) = @_;
    $self->{status_text}->SetLabel($status);
    $self->{status_text}->Wrap($self->{status_text}->GetSize->GetWidth - 30);
    $self->{status_text}->Refresh;
    $self->Layout;
}

sub connect {
    my ($self) = @_;
    
    return if $self->is_connected;
    
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
    $self->set_status("Printer is online. You can now start printing from the queue on the right.");
    $self->_update_connection_controls;
    $self->reload_jobs;
}

sub disconnect {
    my ($self) = @_;
    
    $self->print_status_timer->Stop;
    return if !$self->is_connected;
    
    $self->printing->printing(0) if $self->printing;
    $self->printing(undef);
    $self->{gauge}->Hide;
    $self->sender->disconnect;
    $self->set_status("Not connected");
    $self->_update_connection_controls;
    $self->reload_jobs;
}

sub update_serial_ports {
    my ($self) = @_;
    
    $self->{serial_port_combobox}->Clear;
    $self->{serial_port_combobox}->Append($_)
        for wxTheApp->scan_serial_ports;
}

sub load_print_job {
    my ($self, $gcode_file, $filament_stats) = @_;
    
    push @{$self->jobs}, my $job = Slic3r::GUI::Controller::PrinterPanel::PrintJob->new(
        id              => time() . $gcode_file . rand(1000),
        gcode_file      => $gcode_file,
        filament_stats  => $filament_stats,
    );
    $self->reload_jobs;
    return $job;
}

sub delete_job {
    my ($self, $job) = @_;
    
    $self->jobs([ grep $_->id ne $job->id, @{$self->jobs} ]);
    $self->reload_jobs;
}

sub print_job {
    my ($self, $job) = @_;
    
    $self->printing($job);
    $job->printing(1);
    $self->reload_jobs;
    
    open my $fh, '<', $job->gcode_file;
    my $line_count = 0;
    while (my $row = <$fh>) {
        $self->sender->send($row);
        $line_count++;
    }
    close $fh;
    
    $self->_update_connection_controls;
    $self->{gauge}->SetRange($line_count);
    $self->{gauge}->SetValue(0);
    $self->{gauge}->Enable;
    $self->{gauge}->Show;
    $self->Layout;
    
    $self->print_status_timer->Start(PRINT_STATUS_TIMER_INTERVAL, wxTIMER_CONTINUOUS);
    $self->set_status('Printing...');
}

sub print_completed {
    my ($self) = @_;
    
    my $job = $self->printing;
    $self->printing(undef);
    $job->printing(0);
    $job->printed(1);
    $self->_update_connection_controls;
    $self->{gauge}->Hide;
    $self->Layout;
    $self->print_status_timer->Stop;
    
    $self->set_status('Print completed.');
    
    # reorder jobs
    @{$self->jobs} = sort { $a->printed <=> $b->printed } @{$self->jobs};
    
    $self->reload_jobs;
}

sub reload_jobs {
    my ($self) = @_;
    
    # remove all panels
    foreach my $child ($self->{jobs_panel_sizer}->GetChildren) {
        my $window = $child->GetWindow;
        $self->{jobs_panel_sizer}->Detach($window);
        # now $child does not exist anymore
        $window->Destroy;
    }
    
    # re-add all panels
    foreach my $job (@{$self->jobs}) {
        my $panel = Slic3r::GUI::Controller::PrinterPanel::PrintJobPanel->new($self->{jobs_panel}, $job);
        $self->{jobs_panel_sizer}->Add($panel, 0, wxEXPAND | wxBOTTOM, 5);
        
        $panel->on_delete_job(sub {
            my ($job) = @_;
            $self->delete_job($job);
        });
        $panel->on_print_job(sub {
            my ($job) = @_;
            $self->print_job($job);
        });
        $panel->on_pause_print(sub {
            my ($job) = @_;
            $self->sender->pause_queue;
            $job->paused(1);
            $self->reload_jobs;
            $self->_update_connection_controls;
            $self->{gauge}->Disable;
            $self->set_status('Print is paused. Click on Resume to continue.');
        });
        $panel->on_resume_print(sub {
            my ($job) = @_;
            $self->sender->resume_queue;
            $job->paused(0);
            $self->reload_jobs;
            $self->_update_connection_controls;
            $self->{gauge}->Enable;
            $self->set_status('Printing...');
        });
        $panel->enable_print if $self->is_connected && !$self->printing;
        
        EVT_MOUSEWHEEL($panel, sub {
            my (undef, $event) = @_;
            Wx::PostEvent($self->{jobs_panel}, $event);
            $event->Skip;
        });
    }
    
    $self->{jobs_panel}->Layout;
}

package Slic3r::GUI::Controller::PrinterPanel::PrintJob;
use Moo;

use File::Basename qw(basename);

has 'id'                => (is => 'ro', required => 1);
has 'gcode_file'        => (is => 'ro', required => 1);
has 'filament_stats'    => (is => 'rw');
has 'printing'          => (is => 'rw', default => sub { 0 });
has 'paused'            => (is => 'rw', default => sub { 0 });
has 'printed'           => (is => 'rw', default => sub { 0 });

sub name {
    my ($self) = @_;
    return basename($self->gcode_file);
}

package Slic3r::GUI::Controller::PrinterPanel::PrintJobPanel;
use strict;
use warnings;
use utf8;

use Wx qw(wxTheApp :panel :id :misc :sizer :button :bitmap :font :dialog :icon);
use Wx::Event qw(EVT_BUTTON);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(job on_delete_job on_print_job on_pause_print on_resume_print));

sub new {
    my ($class, $parent, $job) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    
    $self->job($job);
    $self->SetBackgroundColour(Wx::wxWHITE);
    
    my $title_and_buttons_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    {
        my $text = Wx::StaticText->new($self, -1, $job->name, wxDefaultPosition, wxDefaultSize);
        my $font = $text->GetFont;
        $font->SetWeight(wxFONTWEIGHT_BOLD);
        $text->SetFont($font);
        if ($job->printing) {
            $text->SetForegroundColour(Wx::wxGREEN);
        } elsif ($job->printed) {
            $text->SetForegroundColour($Slic3r::GUI::grey);
        }
        $title_and_buttons_sizer->Add($text, 1, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
    }
    {
        my $btn = $self->{btn_delete} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG),
            wxDefaultPosition, wxDefaultSize, Wx::wxBORDER_NONE);
        $btn->SetToolTipString("Delete this job from print queue")
            if $btn->can('SetToolTipString');
        $btn->SetFont($Slic3r::GUI::small_font);
        $title_and_buttons_sizer->Add($btn, 0, wxEXPAND | wxBOTTOM, 0);
        
        EVT_BUTTON($self, $btn, sub {
            my $res = Wx::MessageDialog->new($self, "Are you sure you want to delete this print job?", 'Delete Job', wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION)->ShowModal;
            return unless $res == wxID_YES;
            
            wxTheApp->CallAfter(sub {
                $self->on_delete_job->($job);
            });
        });
    }
    
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    {
        my $filament_stats = join "\n",
            map "$_ (" . sprintf("%.2f", $job->filament_stats->{$_}/100) . "m)",
            sort keys %{$job->filament_stats};
        my $text = Wx::StaticText->new($self, -1, $filament_stats, wxDefaultPosition, wxDefaultSize);
        $text->SetFont($Slic3r::GUI::small_font);
        if ($job->printed) {
            $text->SetForegroundColour($Slic3r::GUI::grey);
        }
        $left_sizer->Add($text, 1, wxEXPAND | wxTOP | wxBOTTOM, 7);
    }
    
    
    my $right_sizer = Wx::BoxSizer->new(wxVERTICAL);
    {
        my $label = $job->printed ? 'Print Again' : 'Print This';
        my $btn = $self->{btn_print} = Wx::Button->new($self, -1, $label, wxDefaultPosition, wxDefaultSize);
        $btn->Hide;
        if ($Slic3r::GUI::have_button_icons) {
            $self->{btn_print}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/arrow_up.png", wxBITMAP_TYPE_PNG));
        }
        $right_sizer->Add($btn, 0, wxEXPAND | wxBOTTOM, 7);
        
        EVT_BUTTON($self, $btn, sub {
            wxTheApp->CallAfter(sub {
                $self->on_print_job->($job);
            });
        });
    }
    {
        my $btn = $self->{btn_pause} = Wx::Button->new($self, -1, "Pause", wxDefaultPosition, wxDefaultSize);
        if (!$job->printing || $job->paused) {
            $btn->Hide;
        }
        if ($Slic3r::GUI::have_button_icons) {
            $self->{btn_print}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/arrow_up.png", wxBITMAP_TYPE_PNG));
        }
        $right_sizer->Add($btn, 0, wxEXPAND | wxBOTTOM, 7);
        
        EVT_BUTTON($self, $btn, sub {
            wxTheApp->CallAfter(sub {
                $self->on_pause_print->($job);
            });
        });
    }
    {
        my $btn = $self->{btn_resume} = Wx::Button->new($self, -1, "Resume", wxDefaultPosition, wxDefaultSize);
        if (!$job->printing || !$job->paused) {
            $btn->Hide;
        }
        if ($Slic3r::GUI::have_button_icons) {
            $self->{btn_print}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/arrow_up.png", wxBITMAP_TYPE_PNG));
        }
        $right_sizer->Add($btn, 0, wxEXPAND | wxBOTTOM, 7);
        
        EVT_BUTTON($self, $btn, sub {
            wxTheApp->CallAfter(sub {
                $self->on_resume_print->($job);
            });
        });
    }
    
    my $middle_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $middle_sizer->Add($left_sizer, 1, wxEXPAND, 0);
    $middle_sizer->Add($right_sizer, 0, wxEXPAND, 0);
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($title_and_buttons_sizer, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 3);
    $sizer->Add($middle_sizer, 1, wxEXPAND, 0);
    $self->SetSizer($sizer);
    
    return $self;
}

sub enable_print {
    my ($self) = @_;
    
    if (!$self->job->printing) {
        $self->{btn_print}->Show;
    }
    $self->Layout;
}

1;
