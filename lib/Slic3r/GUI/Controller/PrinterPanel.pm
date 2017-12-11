package Slic3r::GUI::Controller::PrinterPanel;
use strict;
use warnings;
use utf8;

use Wx qw(wxTheApp :panel :id :misc :sizer :button :bitmap :window :gauge :timer
    :textctrl :font :systemsettings);
use Wx::Event qw(EVT_BUTTON EVT_MOUSEWHEEL EVT_TIMER EVT_SCROLLWIN);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(printer_name config sender jobs 
    printing status_timer temp_timer));

use constant CONNECTION_TIMEOUT => 3;               # seconds
use constant STATUS_TIMER_INTERVAL => 1000;         # milliseconds
use constant TEMP_TIMER_INTERVAL   => 5000;         # milliseconds

sub new {
    my ($class, $parent, $printer_name, $config) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, [500, 250]);
    
    $self->printer_name($printer_name || 'Printer');
    $self->config($config);
    $self->jobs([]);
    
    # set up the timer that polls for updates
    {
        my $timer_id = &Wx::NewId();
        $self->status_timer(Wx::Timer->new($self, $timer_id));
        EVT_TIMER($self, $timer_id, sub {
            my ($self, $event) = @_;
            
            if ($self->printing) {
                my $queue_size = $self->sender->queue_size;
                $self->{gauge}->SetValue($self->{gauge}->GetRange - $queue_size);
                if ($queue_size == 0) {
                    $self->print_completed;
                }
            }
            $self->{log_textctrl}->AppendText("$_\n") for @{$self->sender->purge_log};
            {
                my $temp = $self->sender->getT;
                if ($temp eq '') {
                    $self->{temp_panel}->Hide;
                } else {
                    if (!$self->{temp_panel}->IsShown) {
                        $self->{temp_panel}->Show;
                        $self->Layout;
                    }
                    $self->{temp_text}->SetLabel($temp . "°C");
                    
                    $temp = $self->sender->getB;
                    if ($temp eq '') {
                        $self->{bed_temp_text}->SetLabel('n.a.');
                    } else {
                        $self->{bed_temp_text}->SetLabel($temp . "°C");
                    }
                }
            }
        });
    }
    
    # set up the timer that sends temperature requests
    # (responses are handled by status_timer)
    {
        my $timer_id = &Wx::NewId();
        $self->temp_timer(Wx::Timer->new($self, $timer_id));
        EVT_TIMER($self, $timer_id, sub {
            my ($self, $event) = @_;
            $self->sender->send("M105", 1);  # send it through priority queue
        });
    }
    
    my $box = Wx::StaticBox->new($self, -1, "");
    my $sizer = Wx::StaticBoxSizer->new($box, wxHORIZONTAL);
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    # printer name
    {
        my $text = Wx::StaticText->new($box, -1, $self->printer_name, wxDefaultPosition, [220,-1]);
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
            $self->{btn_rescan_serial} = my $btn = Wx::BitmapButton->new($box, -1, Wx::Bitmap->new(Slic3r::var("arrow_rotate_clockwise.png"), wxBITMAP_TYPE_PNG),
                wxDefaultPosition, wxDefaultSize, &Wx::wxBORDER_NONE);
            $btn->SetToolTipString("Rescan serial ports")
                if $btn->can('SetToolTipString');
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
            $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("delete.png"), wxBITMAP_TYPE_PNG));
            $serial_speed_sizer->Add($btn, 0, wxLEFT, 5);
            EVT_BUTTON($self, $btn, \&disconnect);
        }
        $conn_sizer->Add($serial_speed_sizer, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
    }
    
    # buttons
    {
        $self->{btn_connect} = my $btn = Wx::Button->new($box, -1, "Connect to printer", wxDefaultPosition, [-1, 40]);
        my $font = $btn->GetFont;
        $font->SetPointSize($font->GetPointSize + 2);
        $btn->SetFont($font);
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("arrow_up.png"), wxBITMAP_TYPE_PNG));
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
    $self->{status_text} = Wx::StaticText->new($box, -1, "", wxDefaultPosition, [200,-1]);
    $left_sizer->Add($self->{status_text}, 1, wxEXPAND | wxTOP, 15);
    
    # manual control
    {
        $self->{btn_manual_control} = my $btn = Wx::Button->new($box, -1, "Manual control", wxDefaultPosition, wxDefaultSize);
        $btn->SetFont($Slic3r::GUI::small_font);
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("cog.png"), wxBITMAP_TYPE_PNG));
        $btn->Hide;
        $left_sizer->Add($btn, 0, wxTOP, 15);
        EVT_BUTTON($self, $btn, sub {
            my $dlg = Slic3r::GUI::Controller::ManualControlDialog->new
                ($self, $self->config, $self->sender);
            $dlg->ShowModal;
        });
    }
    
    # temperature
    {
        my $temp_panel = $self->{temp_panel} = Wx::Panel->new($box, -1);
        my $temp_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        
        my $temp_font = Wx::Font->new($Slic3r::GUI::small_font);
        $temp_font->SetWeight(wxFONTWEIGHT_BOLD);
        {
            my $text = Wx::StaticText->new($temp_panel, -1, "Temperature:", wxDefaultPosition, wxDefaultSize);
            $text->SetFont($Slic3r::GUI::small_font);
            $temp_sizer->Add($text, 0, wxALIGN_CENTER_VERTICAL);
        
            $self->{temp_text} = Wx::StaticText->new($temp_panel, -1, "", wxDefaultPosition, wxDefaultSize);
            $self->{temp_text}->SetFont($temp_font);
            $self->{temp_text}->SetForegroundColour(Wx::wxRED);
            $temp_sizer->Add($self->{temp_text}, 1, wxALIGN_CENTER_VERTICAL);
        }
        {
            my $text = Wx::StaticText->new($temp_panel, -1, "Bed:", wxDefaultPosition, wxDefaultSize);
            $text->SetFont($Slic3r::GUI::small_font);
            $temp_sizer->Add($text, 0, wxALIGN_CENTER_VERTICAL);
        
            $self->{bed_temp_text} = Wx::StaticText->new($temp_panel, -1, "", wxDefaultPosition, wxDefaultSize);
            $self->{bed_temp_text}->SetFont($temp_font);
            $self->{bed_temp_text}->SetForegroundColour(Wx::wxRED);
            $temp_sizer->Add($self->{bed_temp_text}, 1, wxALIGN_CENTER_VERTICAL);
        }
        $temp_panel->SetSizer($temp_sizer);
        $temp_panel->Hide;
        $left_sizer->Add($temp_panel, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);
    }
    
    # print jobs panel
    $self->{print_jobs_sizer} = my $print_jobs_sizer = Wx::BoxSizer->new(wxVERTICAL);
    {
        my $text = Wx::StaticText->new($box, -1, "Queue:", wxDefaultPosition, wxDefaultSize);
        $text->SetFont($Slic3r::GUI::small_font);
        $print_jobs_sizer->Add($text, 0, wxEXPAND, 0);
        
        $self->{jobs_panel} = Wx::ScrolledWindow->new($box, -1, wxDefaultPosition, wxDefaultSize,
            wxVSCROLL | wxBORDER_NONE);
        $self->{jobs_panel}->SetScrollbars(0, 1, 0, 1);
        $self->{jobs_panel_sizer} = Wx::BoxSizer->new(wxVERTICAL);
        $self->{jobs_panel}->SetSizer($self->{jobs_panel_sizer});
        $print_jobs_sizer->Add($self->{jobs_panel}, 1, wxEXPAND, 0);
        
        # TODO: fix this. We're trying to pass the scroll event to the parent but it
        # doesn't work.
        EVT_SCROLLWIN($self->{jobs_panel}, sub {
            my ($panel, $event) = @_;
            
            my $controller = $self->GetParent;
            my $new_event = Wx::ScrollWinEvent->new(
                $event->GetEventType,
                $event->GetPosition,
                $event->GetOrientation,
            );
            $controller->ProcessEvent($new_event);
        }) if 0;
    }
    
    my $log_sizer = Wx::BoxSizer->new(wxVERTICAL);
    {
        my $text = Wx::StaticText->new($box, -1, "Log:", wxDefaultPosition, wxDefaultSize);
        $text->SetFont($Slic3r::GUI::small_font);
        $log_sizer->Add($text, 0, wxEXPAND, 0);
        
        my $log = $self->{log_textctrl} = Wx::TextCtrl->new($box, -1, "", wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxBORDER_NONE);
        $log->SetBackgroundColour($box->GetBackgroundColour);
        $log->SetFont($Slic3r::GUI::small_font);
        $log->SetEditable(0);
        $log_sizer->Add($self->{log_textctrl}, 1, wxEXPAND, 0);
    }
    
    $sizer->Add($left_sizer, 0, wxEXPAND | wxALL, 0);
    $sizer->Add($print_jobs_sizer, 2, wxEXPAND | wxALL, 0);
    $sizer->Add($log_sizer, 1, wxEXPAND | wxLEFT, 15);
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    
    $self->_update_connection_controls;
    
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
    $self->{btn_manual_control}->Hide;
    $self->{btn_manual_control}->Disable;
    
    if ($self->is_connected) {
        $self->{btn_connect}->Hide;
        $self->{btn_manual_control}->Show;
        if (!$self->printing || $self->printing->paused) {
            $self->{btn_disconnect}->Show;
            $self->{btn_manual_control}->Enable;
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
    $self->{status_text}->Wrap($self->{status_text}->GetSize->GetWidth);
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
        $self->set_status("Connection failed. Check serial port and speed.");
    } else {
        if ($self->sender->wait_connected) {
            $self->set_status("Printer is online. You can now start printing from the queue on the right.");
            $self->status_timer->Start(STATUS_TIMER_INTERVAL, wxTIMER_CONTINUOUS);
            $self->temp_timer->Start(TEMP_TIMER_INTERVAL, wxTIMER_CONTINUOUS);
        
            # request temperature now, without waiting for the timer
            $self->sender->send("M105", 1);
        } else {
            $self->set_status("Connection failed. Check serial port and speed.");
        }
    }
    $self->_update_connection_controls;
    $self->reload_jobs;
}

sub disconnect {
    my ($self) = @_;
    
    $self->status_timer->Stop;
    $self->temp_timer->Stop;
    return if !$self->is_connected;
    
    $self->printing->printing(0) if $self->printing;
    $self->printing(undef);
    $self->{gauge}->Hide;
    $self->{temp_panel}->Hide;
    $self->sender->disconnect;
    $self->set_status("");
    $self->_update_connection_controls;
    $self->reload_jobs;
}

sub update_serial_ports {
    my ($self) = @_;
    
    my $cb = $self->{serial_port_combobox};
    my $current = $cb->GetValue;
    $cb->Clear;
    $cb->Append($_) for Slic3r::GUI::scan_serial_ports;
    $cb->SetValue($current);
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
    
    $self->set_status('Printing...');
    $self->{log_textctrl}->AppendText(sprintf "=====\n");
    $self->{log_textctrl}->AppendText(sprintf "Printing %s\n", $job->name);
    $self->{log_textctrl}->AppendText(sprintf "Print started at %s\n", $self->_timestamp);
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
    
    $self->set_status('Print completed.');
    $self->{log_textctrl}->AppendText(sprintf "Print completed at %s\n", $self->_timestamp);
    
    $self->reload_jobs;
}

sub reload_jobs {
    my ($self) = @_;
    
    # reorder jobs
    @{$self->jobs} = sort { ($a->printed <=> $b->printed) || ($a->timestamp <=> $b->timestamp) }
        @{$self->jobs};
    
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
        $panel->on_abort_print(sub {
            my ($job) = @_;
            $self->sender->purge_queue;
            $self->printing(undef);
            $job->printing(0);
            $job->paused(0);
            $self->reload_jobs;
            $self->_update_connection_controls;
            $self->{gauge}->Disable;
            $self->{gauge}->Hide;
            $self->set_status('Print was aborted.');
            $self->{log_textctrl}->AppendText(sprintf "Print aborted at %s\n", $self->_timestamp);
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
    $self->{print_jobs_sizer}->Layout;
}

sub _timestamp {
    my ($self) = @_;
    
    my @time = localtime(time);
    return sprintf '%02d:%02d:%02d', @time[2,1,0];
}

package Slic3r::GUI::Controller::PrinterPanel::PrintJob;
use Moo;

use File::Basename qw(basename);

has 'id'                => (is => 'ro', required => 1);
has 'timestamp'         => (is => 'ro', default => sub { time });
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

use Wx qw(wxTheApp :panel :id :misc :sizer :button :bitmap :font :dialog :icon :timer
    :colour :brush :pen);
use Wx::Event qw(EVT_BUTTON EVT_TIMER EVT_ERASE_BACKGROUND);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(job on_delete_job on_print_job on_pause_print on_resume_print
    on_abort_print blink_timer));

sub new {
    my ($class, $parent, $job) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize);
    
    $self->job($job);
    $self->SetBackgroundColour(wxWHITE);
    
    {
        my $white_brush = Wx::Brush->new(wxWHITE, wxSOLID);
        my $pen = Wx::Pen->new(Wx::Colour->new(200,200,200), 1, wxSOLID);
        EVT_ERASE_BACKGROUND($self, sub {
            my ($self, $event) = @_;
            my $dc = $event->GetDC;
            my $size = $self->GetSize;
            $dc->SetBrush($white_brush);
            $dc->SetPen($pen);
            $dc->DrawRoundedRectangle(0, 0, $size->GetWidth,$size->GetHeight, 6);
        });
    }
    
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    {
        $self->{job_name_textctrl} = my $text = Wx::StaticText->new($self, -1, $job->name, wxDefaultPosition, wxDefaultSize);
        my $font = $text->GetFont;
        $font->SetWeight(wxFONTWEIGHT_BOLD);
        $text->SetFont($font);
        if ($job->printed) {
            $text->SetForegroundColour($Slic3r::GUI::grey);
        }
        $left_sizer->Add($text, 0, wxEXPAND, 0);
    }
    {
        my $filament_stats = join "\n",
            map "$_ (" . sprintf("%.2f", $job->filament_stats->{$_}/1000) . "m)",
            sort keys %{$job->filament_stats};
        my $text = Wx::StaticText->new($self, -1, $filament_stats, wxDefaultPosition, wxDefaultSize);
        $text->SetFont($Slic3r::GUI::small_font);
        if ($job->printed && !$job->printing) {
            $text->SetForegroundColour($Slic3r::GUI::grey);
        }
        $left_sizer->Add($text, 0, wxEXPAND | wxTOP, 6);
    }
    
    my $buttons_sizer = Wx::BoxSizer->new(wxVERTICAL);
    my $button_style = Wx::wxBORDER_NONE | wxBU_EXACTFIT;
    {
        my $btn = $self->{btn_delete} = Wx::Button->new($self, -1, 'Delete',
            wxDefaultPosition, wxDefaultSize, $button_style);
        $btn->SetToolTipString("Delete this job from print queue")
            if $btn->can('SetToolTipString');
        $btn->SetFont($Slic3r::GUI::small_font);
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("delete.png"), wxBITMAP_TYPE_PNG));
        if ($job->printing) {
            $btn->Hide;
        }
        $buttons_sizer->Add($btn, 0, wxBOTTOM, 2);
        
        EVT_BUTTON($self, $btn, sub {
            my $res = Wx::MessageDialog->new($self, "Are you sure you want to delete this print job?", 'Delete Job', wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION)->ShowModal;
            return unless $res == wxID_YES;
            
            wxTheApp->CallAfter(sub {
                $self->on_delete_job->($job);
            });
        });
    }
    {
        my $label = $job->printed ? 'Print Again' : 'Print This';
        my $btn = $self->{btn_print} = Wx::Button->new($self, -1, $label, wxDefaultPosition, wxDefaultSize,
            $button_style);
        $btn->SetFont($Slic3r::GUI::small_bold_font);
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("control_play.png"), wxBITMAP_TYPE_PNG));
        $btn->SetBitmapCurrent(Wx::Bitmap->new(Slic3r::var("control_play_blue.png"), wxBITMAP_TYPE_PNG));
        #$btn->SetBitmapPosition(wxRIGHT);
        $btn->Hide;
        $buttons_sizer->Add($btn, 0, wxBOTTOM, 2);
        
        EVT_BUTTON($self, $btn, sub {
            wxTheApp->CallAfter(sub {
                $self->on_print_job->($job);
            });
        });
    }
    {
        my $btn = $self->{btn_pause} = Wx::Button->new($self, -1, "Pause", wxDefaultPosition, wxDefaultSize,
            $button_style);
        $btn->SetFont($Slic3r::GUI::small_font);
        if (!$job->printing || $job->paused) {
            $btn->Hide;
        }
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("control_pause.png"), wxBITMAP_TYPE_PNG));
        $btn->SetBitmapCurrent(Wx::Bitmap->new(Slic3r::var("control_pause_blue.png"), wxBITMAP_TYPE_PNG));
        $buttons_sizer->Add($btn, 0, wxBOTTOM, 2);
        
        EVT_BUTTON($self, $btn, sub {
            wxTheApp->CallAfter(sub {
                $self->on_pause_print->($job);
            });
        });
    }
    {
        my $btn = $self->{btn_resume} = Wx::Button->new($self, -1, "Resume", wxDefaultPosition, wxDefaultSize,
            $button_style);
        $btn->SetFont($Slic3r::GUI::small_font);
        if (!$job->printing || !$job->paused) {
            $btn->Hide;
        }
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("control_play.png"), wxBITMAP_TYPE_PNG));
        $btn->SetBitmapCurrent(Wx::Bitmap->new(Slic3r::var("control_play_blue.png"), wxBITMAP_TYPE_PNG));
        $buttons_sizer->Add($btn, 0, wxBOTTOM, 2);
        
        EVT_BUTTON($self, $btn, sub {
            wxTheApp->CallAfter(sub {
                $self->on_resume_print->($job);
            });
        });
    }
    {
        my $btn = $self->{btn_abort} = Wx::Button->new($self, -1, "Abort", wxDefaultPosition, wxDefaultSize,
            $button_style);
        $btn->SetFont($Slic3r::GUI::small_font);
        if (!$job->printing) {
            $btn->Hide;
        }
        $btn->SetBitmap(Wx::Bitmap->new(Slic3r::var("control_stop.png"), wxBITMAP_TYPE_PNG));
        $btn->SetBitmapCurrent(Wx::Bitmap->new(Slic3r::var("control_stop_blue.png"), wxBITMAP_TYPE_PNG));
        $buttons_sizer->Add($btn, 0, wxBOTTOM, 2);
        
        EVT_BUTTON($self, $btn, sub {
            wxTheApp->CallAfter(sub {
                $self->on_abort_print->($job);
            });
        });
    }
    
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $sizer->Add($left_sizer, 1, wxEXPAND | wxALL, 6);
    $sizer->Add($buttons_sizer, 0, wxEXPAND | wxALL, 6);
    $self->SetSizer($sizer);
    
    # set-up the timer that changes the job name color while printing
    if ($self->job->printing && !$self->job->paused) {
        my $timer_id = &Wx::NewId();
        $self->blink_timer(Wx::Timer->new($self, $timer_id));
        my $blink = 0;  # closure
        my $colour = Wx::Colour->new(0, 190, 0);
        EVT_TIMER($self, $timer_id, sub {
            my ($self, $event) = @_;
            
            $self->{job_name_textctrl}->SetForegroundColour($blink ? Wx::wxBLACK : $colour);
            $blink = !$blink;
        });
        $self->blink_timer->Start(1000, wxTIMER_CONTINUOUS);
    }
    
    return $self;
}

sub enable_print {
    my ($self) = @_;
    
    if (!$self->job->printing) {
        $self->{btn_print}->Show;
    }
    $self->Layout;
}

sub Destroy {
    my ($self) = @_;
    
    # There's a gap between the time Perl destroys the wxPanel object and
    # the blink_timer member, so the wxTimer might still fire an event which
    # isn't handled properly, causing a crash. So we ensure that blink_timer
    # is stopped before we destroy the wxPanel.
    $self->blink_timer->Stop if $self->blink_timer;
    return $self->SUPER::Destroy;
}

1;
