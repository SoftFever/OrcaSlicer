package Slic3r::GUI::Projector;
use strict;
use warnings;
use Wx qw(:dialog :id :misc :sizer :systemsettings :bitmap :button :icon wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_TEXT_ENTER EVT_SPINCTRL EVT_SLIDER);
use base qw(Wx::Dialog Class::Accessor);
use utf8;

__PACKAGE__->mk_accessors(qw(config config2 screen controller _optgroups));

sub new {
    my ($class, $parent) = @_;
    my $self = $class->SUPER::new($parent, -1, "Projector for DLP", wxDefaultPosition, wxDefaultSize);
    $self->config2({
        display                 => 0,
        show_bed                => 1,
        invert_y                => 0,
        zoom                    => 100,
        exposure_time           => 2,
        bottom_exposure_time    => 7,
        settle_time             => 1.5,
        bottom_layers           => 3,
        z_lift                  => 5,
        z_lift_speed            => 8,
        offset                  => [0,0],
    });
    
    my $ini = eval { Slic3r::Config->read_ini("$Slic3r::GUI::datadir/DLP.ini") };
    if ($ini) {
        foreach my $opt_id (keys %{$ini->{_}}) {
            my $value = $ini->{_}{$opt_id};
            if ($opt_id eq 'offset') {
                $value = [ split /,/, $value ];
            }
            $self->config2->{$opt_id} = $value;
        }
    }
    
    my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    $self->config(Slic3r::Config->new_from_defaults(
        qw(serial_port serial_speed bed_shape start_gcode end_gcode z_offset)
    ));
    $self->config->apply(wxTheApp->{mainframe}->config);
    
    my @optgroups = ();
    {
        push @optgroups, my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
            parent      => $self,
            title       => 'USB/Serial connection',
            config      => $self->config,
            label_width => 200,
        );
        $sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
        
        {
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label => 'Serial port',
            );
            my $serial_port = $optgroup->get_option('serial_port');
            $serial_port->side_widget(sub {
                my ($parent) = @_;
            
                my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new($Slic3r::var->("arrow_rotate_clockwise.png"), wxBITMAP_TYPE_PNG),
                    wxDefaultPosition, wxDefaultSize, &Wx::wxBORDER_NONE);
                $btn->SetToolTipString("Rescan serial ports")
                    if $btn->can('SetToolTipString');
                EVT_BUTTON($self, $btn, sub {
                    $optgroup->get_field('serial_port')->set_values([ wxTheApp->scan_serial_ports ]);
                });
            
                return $btn;
            });
            my $serial_test = sub {
                my ($parent) = @_;
            
                my $btn = $self->{serial_test_btn} = Wx::Button->new($parent, -1,
                    "Test", wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxBU_EXACTFIT);
                $btn->SetFont($Slic3r::GUI::small_font);
                if ($Slic3r::GUI::have_button_icons) {
                    $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("wrench.png"), wxBITMAP_TYPE_PNG));
                }
            
                EVT_BUTTON($self, $btn, sub {
                    my $sender = Slic3r::GCode::Sender->new;
                    my $res = $sender->connect(
                        $self->{config}->serial_port,
                        $self->{config}->serial_speed,
                    );
                    if ($res && $sender->wait_connected) {
                        Slic3r::GUI::show_info($self, "Connection to printer works correctly.", "Success!");
                    } else {
                        Slic3r::GUI::show_error($self, "Connection failed.");
                    }
                });
                return $btn;
            };
            $line->append_option($serial_port);
            $line->append_option($optgroup->get_option('serial_speed'));
            $line->append_widget($serial_test);
            $optgroup->append_line($line);
        }
    }
    
    {
        push @optgroups, my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
            parent      => $self,
            title       => 'G-code',
            config      => $self->config,
            label_width => 200,
        );
        $sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
        
        {
            my $option = $optgroup->get_option('start_gcode');
            $option->height(50);
            $option->full_width(1);
            $optgroup->append_single_option_line($option);
        }
        {
            my $option = $optgroup->get_option('end_gcode');
            $option->height(50);
            $option->full_width(1);
            $optgroup->append_single_option_line($option);
        }
    }
    
    my $on_change = sub {
        my ($opt_id, $value) = @_;
        
        $self->config2->{$opt_id} = $value;
        $self->screen->reposition;
        $self->show_print_time;
        
        my $serialized = {};
        foreach my $opt_id (keys %{$self->config2}) {
            my $value = $self->config2->{$opt_id};
            if (ref($value) eq 'ARRAY') {
                $value = join ',', @$value;
            }
            $serialized->{$opt_id} = $value;
        }
        Slic3r::Config->write_ini(
            "$Slic3r::GUI::datadir/DLP.ini",
            { _ => $serialized });
    };
    
    {
        push @optgroups, my $optgroup = Slic3r::GUI::OptionsGroup->new(
            parent      => $self,
            title       => 'Projection',
            on_change   => $on_change,
            label_width => 200,
        );
        $sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
        
        {
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label => 'Display',
            );
        
            my @displays = 0 .. (Wx::Display::GetCount()-1);
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'display',
                type        => 'select',
                label       => 'Display',
                tooltip     => '',
                labels      => [@displays],
                values      => [@displays],
                default     => $self->config2->{display},
            ));
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'zoom',
                type        => 'percent',
                label       => 'Zoom %',
                tooltip     => '',
                default     => $self->config2->{zoom},
                min         => 0.1,
                max         => 100,
            ));
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'offset',
                type        => 'point',
                label       => 'Offset',
                tooltip     => '',
                default     => $self->config2->{offset},
            ));
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'invert_y',
                type        => 'bool',
                label       => 'Invert Y',
                tooltip     => '',
                default     => $self->config2->{invert_y},
            ));
            $optgroup->append_line($line);
        }
        
        $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id      => 'show_bed',
            type        => 'bool',
            label       => 'Show bed',
            tooltip     => '',
            default     => $self->config2->{show_bed},
        ));
    }
    
    {
        push @optgroups, my $optgroup = Slic3r::GUI::OptionsGroup->new(
            parent      => $self,
            title       => 'Print',
            on_change   => $on_change,
            label_width => 200,
        );
        $sizer->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
        
        {
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label => 'Time (seconds)',
            );
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'bottom_exposure_time',
                type        => 'f',
                label       => 'Bottom exposure',
                tooltip     => '',
                default     => $self->config2->{bottom_exposure_time},
            ));
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'exposure_time',
                type        => 'f',
                label       => 'Exposure',
                tooltip     => '',
                default     => $self->config2->{exposure_time},
            ));
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'settle_time',
                type        => 'f',
                label       => 'Settle',
                tooltip     => '',
                default     => $self->config2->{settle_time},
            ));
            $optgroup->append_line($line);
        }
        
        $optgroup->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
            opt_id      => 'bottom_layers',
            type        => 'i',
            label       => 'Bottom layers',
            tooltip     => '',
            default     => $self->config2->{bottom_layers},
        ));
        
        {
            my $line = Slic3r::GUI::OptionsGroup::Line->new(
                label => 'Z Lift',
            );
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'z_lift',
                type        => 'f',
                label       => 'Distance',
                sidetext    => 'mm',
                tooltip     => '',
                default     => $self->config2->{z_lift},
            ));
            $line->append_option(Slic3r::GUI::OptionsGroup::Option->new(
                opt_id      => 'z_lift_speed',
                type        => 'f',
                label       => 'Speed',
                sidetext    => 'mm/s',
                tooltip     => '',
                default     => $self->config2->{z_lift_speed},
            ));
            $optgroup->append_line($line);
        }
    }
    
    $self->_optgroups([@optgroups]);
    
    {
        my $sizer1 = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($sizer1, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
        
        {
            my $btn = $self->{btn_manual_control} = Wx::Button->new($self, -1, 'Manual Control', wxDefaultPosition, wxDefaultSize);
            if ($Slic3r::GUI::have_button_icons) {
                $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("cog.png"), wxBITMAP_TYPE_PNG));
            }
            $sizer1->Add($btn, 0);
            EVT_BUTTON($self, $btn, sub {
                my $sender = Slic3r::GCode::Sender->new;
                my $res = $sender->connect(
                    $self->config->serial_port,
                    $self->config->serial_speed,
                );
                if (!$res || !$sender->wait_connected) {
                    Slic3r::GUI::show_error(undef, "Connection failed. Check serial port and speed.");
                    return;
                }
                my $dlg = Slic3r::GUI::Controller::ManualControlDialog->new
                    ($self, $self->config, $sender);
                $dlg->ShowModal;
                $sender->disconnect;
            });
            
            
        }
        {
            my $btn = $self->{btn_print} = Wx::Button->new($self, -1, 'Print', wxDefaultPosition, wxDefaultSize);
            if ($Slic3r::GUI::have_button_icons) {
                $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("control_play.png"), wxBITMAP_TYPE_PNG));
            }
            $sizer1->Add($btn, 0);
            EVT_BUTTON($self, $btn, sub {
                $self->controller->start_print;
                $self->_update_buttons;
                $self->_set_status('');
            });
        }
        {
            my $btn = $self->{btn_stop} = Wx::Button->new($self, -1, 'Stop/Black', wxDefaultPosition, wxDefaultSize);
            if ($Slic3r::GUI::have_button_icons) {
                $btn->SetBitmap(Wx::Bitmap->new($Slic3r::var->("control_stop.png"), wxBITMAP_TYPE_PNG));
            }
            $sizer1->Add($btn, 0);
            EVT_BUTTON($self, $btn, sub {
                $self->controller->stop_print;
                $self->_update_buttons;
                $self->_set_status('');
            });
        }
        
        {
            {
                my $text = Wx::StaticText->new($self, -1, "Layer:", wxDefaultPosition, wxDefaultSize);
                $text->SetFont($Slic3r::GUI::small_font);
                $sizer1->Add($text, 0, wxEXPAND | wxLEFT, 10);
            }
            {
                my $spin = $self->{layers_spinctrl} = Wx::SpinCtrl->new($self, -1, 0, wxDefaultPosition, [60,-1],
                    0, 0, 300, 0);
                $sizer1->Add($spin, 0);
                EVT_SPINCTRL($self, $spin, sub {
                    my $value = $spin->GetValue;
                    $self->{layers_slider}->SetValue($value);
                    $self->controller->project_layer($value);
                    $self->_update_buttons;
                });
            }
            {
                my $slider = $self->{layers_slider} = Wx::Slider->new(
                    $self, -1,
                    0,           # default
                    0,           # min
                    300,         # max
                    wxDefaultPosition,
                    wxDefaultSize,
                );
                $sizer1->Add($slider, 1);
                EVT_SLIDER($self, $slider, sub {
                    my $value = $slider->GetValue;
                    $self->{layers_spinctrl}->SetValue($value);
                    $self->controller->project_layer($value);
                    $self->_update_buttons;
                });
            }
        }
        
        my $sizer2 = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($sizer2, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
        
        {
            $self->{status_text} = Wx::StaticText->new($self, -1, "", wxDefaultPosition, wxDefaultSize);
            $self->{status_text}->SetFont($Slic3r::GUI::small_font);
            $sizer2->Add($self->{status_text}, 1 | wxEXPAND);
        }
    }
    
    {
        my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
        EVT_BUTTON($self, wxID_CLOSE, sub {
            $self->_close;
        });
        $sizer->Add($buttons, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    }
    
    $self->SetSizer($sizer);
    $sizer->SetSizeHints($self);
    
    # reuse existing screen if any
    if ($Slic3r::GUI::DLP_projection_screen) {
        $self->screen($Slic3r::GUI::DLP_projection_screen);
        $self->screen->config($self->config);
        $self->screen->config2($self->config2);
    } else {
        $self->screen(Slic3r::GUI::Projector::Screen->new($parent, $self->config, $self->config2));
        $Slic3r::GUI::DLP_projection_screen = $self->screen;
    }
    $self->screen->reposition;
    $self->screen->Show;
    wxTheApp->{mainframe}->Hide;
    
    # initialize controller
    $self->controller(Slic3r::GUI::Projector::Controller->new(
        config  => $self->config,
        config2 => $self->config2,
        screen  => $self->screen,
        on_project_layer => sub {
            my ($layer_num) = @_;
            
            $self->{layers_spinctrl}->SetValue($layer_num);
            $self->{layers_slider}->SetValue($layer_num);
            
            my $duration = $self->controller->remaining_print_time;
            $self->_set_status(sprintf "Printing layer %d/%d (z = %.2f); %d minutes and %d seconds left",
                $layer_num, $self->controller->layer_count,
                $self->controller->current_layer_height,
                int($duration/60), ($duration - int($duration/60)*60));  # % truncates to integer
        },
        on_print_completed => sub {
            $self->_update_buttons;
            $self->_set_status('');
            Wx::Bell();
        },
    ));
    {
        my $max = $self->controller->layer_count-1;
        $self->{layers_spinctrl}->SetRange(0, $max); 
        $self->{layers_slider}->SetRange(0, $max);
    }
    
    $self->_update_buttons;
    $self->show_print_time;
    
    return $self;
}

sub _update_buttons {
    my ($self) = @_;
    
    my $is_printing = $self->controller->is_printing;
    my $is_projecting = $self->controller->is_projecting;
    $self->{btn_manual_control}->Show(!$is_printing);
    $self->{btn_print}->Show(!$is_printing && !$is_projecting);
    $self->{btn_stop}->Show($is_printing || $is_projecting);
    $self->{layers_spinctrl}->Enable(!$is_printing);
    $self->{layers_slider}->Enable(!$is_printing);
    if ($is_printing) {
        $_->disable for @{$self->_optgroups};
    } else {
        $_->enable for @{$self->_optgroups};
    }
    $self->Layout;
}

sub _set_status {
    my ($self, $status) = @_;
    $self->{status_text}->SetLabel($status // '');
    $self->{status_text}->Wrap($self->{status_text}->GetSize->GetWidth);
    $self->{status_text}->Refresh;
    $self->Layout;
}

sub show_print_time {
    my ($self) = @_;
    
    
    my $duration = $self->controller->print_time;
    $self->_set_status(sprintf "Estimated print time: %d minutes and %d seconds",
        int($duration/60), ($duration - int($duration/60)*60));  # % truncates to integer
}

sub _close {
    my $self = shift;
    
    # if projection screen is not on the same display as our dialog,
    # ask the user whether they want to keep it open
    my $keep_screen = 0;
    my $display_area = Wx::Display->new($self->config2->{display})->GetGeometry;
    if (!$display_area->Contains($self->GetScreenPosition)) {
        my $res = Wx::MessageDialog->new($self, "Do you want to keep the black screen open?", 'Black screen', wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION)->ShowModal;
        $keep_screen = ($res == wxID_YES);
    }
    
    if ($keep_screen) {
        $self->screen->config(undef);
        $self->screen->config2(undef);
        $self->screen->Refresh;
    } else {
        $self->screen->Destroy;
        $self->screen(undef);
        $Slic3r::GUI::DLP_projection_screen = undef;
    }
    wxTheApp->{mainframe}->Show;
    
    my $printer_tab = wxTheApp->{mainframe}{options_tabs}{printer};
    $printer_tab->load_config($self->config);
    
    $self->EndModal(wxID_OK);
}

package Slic3r::GUI::Projector::Controller;
use Moo;
use Wx qw(wxTheApp :id :timer);
use Wx::Event qw(EVT_TIMER);
use Slic3r::Print::State ':steps';
use Time::HiRes qw(gettimeofday tv_interval);

has 'config'                => (is => 'ro', required => 1);
has 'config2'               => (is => 'ro', required => 1);
has 'screen'                => (is => 'ro', required => 1);
has 'on_project_layer'      => (is => 'rw');
has 'on_print_completed'    => (is => 'rw');
has 'sender'                => (is => 'rw');
has 'timer'                 => (is => 'rw');
has 'is_printing'           => (is => 'rw', default => sub { 0 });
has '_print'                => (is => 'rw');
has '_layers'               => (is => 'rw');
has '_heights'              => (is => 'rw');
has '_layer_num'            => (is => 'rw');
has '_timer_cb'             => (is => 'rw');

sub BUILD {
    my ($self) = @_;
    
    Slic3r::GUI::disable_screensaver();
    
    $self->set_print(wxTheApp->{mainframe}->{plater}->{print});
    
    # projection timer
    my $timer_id = &Wx::NewId();
    $self->timer(Wx::Timer->new($self->screen, $timer_id));
    EVT_TIMER($self->screen, $timer_id, sub {
        my $cb = $self->_timer_cb;
        $self->_timer_cb(undef);
        $cb->();
    });
}

sub delay {
    my ($self, $wait, $cb) = @_;
    
    $self->_timer_cb($cb);
    $self->timer->Start($wait * 1000, wxTIMER_ONE_SHOT);
}

sub set_print {
    my ($self, $print) = @_;
    
    # make sure layers were sliced
    {
        my $progress_dialog;
        foreach my $object (@{$print->objects}) {
            next if $object->step_done(STEP_SLICE);
            $progress_dialog //= Wx::ProgressDialog->new('Slicing…', "Processing layers…", 100, undef, 0);
            $progress_dialog->Pulse;
            $object->slice;
        }
        $progress_dialog->Destroy if $progress_dialog;
    }
    
    $self->_print($print);
    
    # sort layers by Z
    my %layers = ();
    foreach my $layer (map { @{$_->layers}, @{$_->support_layers} } @{$print->objects}) {
        my $height = $layer->print_z;
        $layers{$height} //= [];
        push @{$layers{$height}}, $layer;
    }
    $self->_layers({ %layers });
    $self->_heights([ sort { $a <=> $b } keys %layers ]);
}

sub layer_count {
    my ($self) = @_;
    
    return scalar @{$self->_heights};
}

sub current_layer_height {
    my ($self) = @_;
    
    return $self->_heights->[$self->_layer_num];
}

sub start_print {
    my ($self) = @_;
    
    {
        $self->sender(Slic3r::GCode::Sender->new);
        my $res = $self->sender->connect(
            $self->config->serial_port,
            $self->config->serial_speed,
        );
        if (!$res || !$self->sender->wait_connected) {
            Slic3r::GUI::show_error(undef, "Connection failed. Check serial port and speed.");
            return;
        }
        Slic3r::debugf "connected to " . $self->config->serial_port . "\n";
        
        # send custom start G-code
        $self->sender->send($_, 1) for grep !/^;/, split /\n/, $self->config->start_gcode;
    }
    
    $self->is_printing(1);
    
    # TODO: block until the G1 command has been performed
    # we could do this with M400 + M115 but maybe it's not portable
    $self->delay(5, sub {
        # start with black
        Slic3r::debugf "starting black projection\n";
        $self->_layer_num(-1);
        $self->screen->project_layers(undef);
        $self->delay($self->config2->{settle_time}, sub {
            $self->project_next_layer;
        });
    });
}

sub stop_print {
    my ($self) = @_;
    
    if ($self->sender) {
        $self->sender->disconnect;
    }
    
    $self->is_printing(0);
    $self->timer->Stop;
    $self->_timer_cb(undef);
    $self->screen->project_layers(undef);
}

sub print_completed {
    my ($self) = @_;
    
    # send custom end G-code
    if ($self->sender) {
        $self->sender->send($_, 1) for grep !/^;/, split /\n/, $self->config->end_gcode;
    }
    
    # call this before the on_print_completed callback otherwise buttons
    # won't be updated correctly
    $self->stop_print;
    
    $self->on_print_completed->()
        if $self->is_printing && $self->on_print_completed;
}

sub is_projecting {
    my ($self) = @_;
    
    return defined $self->screen->layers;
}

sub project_layer {
    my ($self, $layer_num) = @_;
    
    if (!defined $layer_num || $layer_num >= $self->layer_count) {
        $self->screen->project_layers(undef);
        return;
    }
    
    my @layers = @{ $self->_layers->{ $self->_heights->[$layer_num] } };
    $self->screen->project_layers([ @layers ]);
}

sub project_next_layer {
    my ($self) = @_;
    
    $self->_layer_num($self->_layer_num + 1);
    Slic3r::debugf "projecting layer %d\n", $self->_layer_num;
    if ($self->_layer_num >= $self->layer_count) {
        $self->print_completed;
        return;
    }
    
    $self->on_project_layer->($self->_layer_num) if $self->on_project_layer;
    
    if ($self->sender) {
        my $z = $self->current_layer_height + $self->config->z_offset;
        my $F = $self->config2->{z_lift_speed} * 60;
        if ($self->config2->{z_lift} != 0) {
            $self->sender->send(sprintf("G1 Z%.5f F%d", $z + $self->config2->{z_lift}, $F), 1);
        }
        $self->sender->send(sprintf("G1 Z%.5f F%d", $z, $F), 1);
    }
    
    # TODO: we should block until G1 commands have been performed, see note below
    $self->delay($self->config2->{settle_time}, sub {
        $self->project_layer($self->_layer_num);
        
        # get exposure time
        my $time = $self->config2->{exposure_time};
        if ($self->_layer_num < $self->config2->{bottom_layers}) {
            $time = $self->config2->{bottom_exposure_time};
        }
        
        $self->delay($time, sub {
            $self->screen->project_layers(undef);
            $self->project_next_layer;
        });
    });
}

sub remaining_print_time {
    my ($self) = @_;
    
    my $remaining_layers = @{$self->_heights} - $self->_layer_num;
    my $remaining_bottom_layers = $self->_layer_num >= $self->config2->{bottom_layers}
        ? 0
        : $self->config2->{bottom_layers} - $self->_layer_num;
    
    return $remaining_bottom_layers * $self->config2->{bottom_exposure_time}
        + ($remaining_layers - $remaining_bottom_layers) * $self->config2->{exposure_time}
        + $remaining_layers * $self->config2->{settle_time};
}

sub print_time {
    my ($self) = @_;
    
    return $self->config2->{bottom_layers} * $self->config2->{bottom_exposure_time}
        + (@{$self->_heights} - $self->config2->{bottom_layers}) * $self->config2->{exposure_time}
        + @{$self->_heights} * $self->config2->{settle_time};
}

sub DESTROY {
    my ($self) = @_;
    
    $self->timer->Stop if $self->timer;
    $self->sender->disconnect if $self->sender;
    Slic3r::GUI::enable_screensaver();
}

package Slic3r::GUI::Projector::Screen;
use Wx qw(:dialog :id :misc :sizer :colour :pen :brush :font wxBG_STYLE_CUSTOM);
use Wx::Event qw(EVT_PAINT EVT_SIZE);
use base qw(Wx::Dialog Class::Accessor);

use List::Util qw(min);
use Slic3r::Geometry qw(X Y unscale scale);
use Slic3r::Geometry::Clipper qw(intersection_pl);

__PACKAGE__->mk_accessors(qw(config config2 scaling_factor bed_origin layers));

sub new {
    my ($class, $parent, $config, $config2) = @_;
    my $self = $class->SUPER::new($parent, -1, "Projector", wxDefaultPosition, wxDefaultSize, 0);
    
    $self->config($config);
    $self->config2($config2);
    $self->SetBackgroundStyle(wxBG_STYLE_CUSTOM);
    EVT_SIZE($self, \&_resize);
    EVT_PAINT($self, \&_repaint);
    $self->_resize;
    
    return $self;
}

sub reposition {
    my ($self) = @_;
    
    my $display = Wx::Display->new($self->config2->{display});
    my $area = $display->GetGeometry;
    $self->Move($area->GetPosition);
    # ShowFullScreen doesn't use the right screen
    #$self->ShowFullScreen($self->config2->{fullscreen});
    $self->SetSize($area->GetSize);
    $self->_resize;
    $self->Refresh;
}

sub _resize {
    my ($self) = @_;
    
    return if !$self->config;
    my ($cw, $ch) = $self->GetSizeWH;
    
    # get bed shape polygon
    my $bed_polygon = Slic3r::Polygon->new_scale(@{$self->config->bed_shape});
    my $bb = $bed_polygon->bounding_box;
    my $size = $bb->size;
    my $center = $bb->center;

    # calculate the scaling factor needed for constraining print bed area inside preview
    # scaling_factor is expressed in pixel / mm
    $self->scaling_factor(min($cw / unscale($size->x), $ch / unscale($size->y))); #)
    
    # apply zoom to scaling factor
    if ($self->config2->{zoom} != 0) {
        # TODO: make sure min and max in the option config are enforced
        $self->scaling_factor($self->scaling_factor * ($self->config2->{zoom}/100));
    }
    
    # calculate the displacement needed for centering bed on screen
    $self->bed_origin([
        $cw/2 - (unscale($center->x) - $self->config2->{offset}->[X]) * $self->scaling_factor,
        $ch/2 - (unscale($center->y) - $self->config2->{offset}->[Y]) * $self->scaling_factor,  #))
    ]);
    
    $self->Refresh;
}

sub project_layers {
    my ($self, $layers) = @_;
    
    $self->layers($layers);
    $self->Refresh;
}

sub _repaint {
    my ($self) = @_;
    
    my $dc = Wx::AutoBufferedPaintDC->new($self);
    my ($cw, $ch) = $self->GetSizeWH;
    return if $cw == 0;  # when canvas is not rendered yet, size is 0,0
    
    $dc->SetPen(Wx::Pen->new(wxBLACK, 1, wxSOLID));
    $dc->SetBrush(Wx::Brush->new(wxBLACK, wxSOLID));
    $dc->DrawRectangle(0, 0, $cw, $ch);
    
    return if !$self->config;
    
    # turn size into max visible coordinates
    # TODO: or should we use ClientArea?
    $cw--;
    $ch--;
    
    # draw bed
    if ($self->config2->{show_bed}) {
        $dc->SetPen(Wx::Pen->new(wxRED, 2, wxSOLID));
        $dc->SetBrush(Wx::Brush->new(wxWHITE, wxTRANSPARENT));
        
        # draw contour
        my $bed_polygon = Slic3r::Polygon->new_scale(@{$self->config->bed_shape});
        $dc->DrawPolygon($self->scaled_points_to_pixel($bed_polygon), 0, 0);
        
        # draw grid
        $dc->SetPen(Wx::Pen->new(wxRED, 1, wxSOLID));
        {
            my $bb = $bed_polygon->bounding_box;
            my $step = scale 10;  # 1cm grid
            my @polylines = ();
            for (my $x = $bb->x_min - ($bb->x_min % $step) + $step; $x < $bb->x_max; $x += $step) {
                push @polylines, Slic3r::Polyline->new([$x, $bb->y_min], [$x, $bb->y_max]);
            }
            for (my $y = $bb->y_min - ($bb->y_min % $step) + $step; $y < $bb->y_max; $y += $step) {
                push @polylines, Slic3r::Polyline->new([$bb->x_min, $y], [$bb->x_max, $y]);
            }
            $dc->DrawLine(map @$_, @$_)
                for map $self->scaled_points_to_pixel([ @$_[0,-1] ]),
                    @{intersection_pl(\@polylines, [$bed_polygon])};
        }
        
        # draw axes orientation
        $dc->SetPen(Wx::Pen->new(wxWHITE, 4, wxSOLID));
        {
            foreach my $endpoint ([10, 0], [0, 10]) {
                $dc->DrawLine(
                    map @{$self->unscaled_point_to_pixel($_)}, [0,0], $endpoint
                );
            }
            
            $dc->SetTextForeground(wxWHITE);
            $dc->SetFont(Wx::Font->new(20, wxDEFAULT, wxNORMAL, wxNORMAL));
            $dc->DrawText("X", @{$self->unscaled_point_to_pixel([10, -2])});
            $dc->DrawText("Y", @{$self->unscaled_point_to_pixel([-2, 10])});
        }
    }
    
    return if !defined $self->layers;
    
    # get layers at this height
    # draw layers
    $dc->SetPen(Wx::Pen->new(wxWHITE, 1, wxSOLID));
    foreach my $layer (@{$self->layers}) {
        my @polygons = sort { $a->contains_point($b->first_point) ? -1 : 1 } map @$_, @{ $layer->slices };
        foreach my $copy (@{$layer->object->_shifted_copies}) {
            foreach my $polygon (@polygons) {
                $polygon = $polygon->clone;
                $polygon->translate(@$copy);
                
                if ($polygon->is_counter_clockwise) {
                    $dc->SetBrush(Wx::Brush->new(wxWHITE, wxSOLID));
                } else {
                    $dc->SetBrush(Wx::Brush->new(wxBLACK, wxSOLID));
                }
                $dc->DrawPolygon($self->scaled_points_to_pixel($polygon->pp), 0, 0);
            }
        }
    }
}

# convert a model coordinate into a pixel coordinate
sub unscaled_point_to_pixel {
    my ($self, $point) = @_;
    
    my $zero = $self->bed_origin;
    my $p = [
        $point->[X] * $self->scaling_factor + $zero->[X],
        $point->[Y] * $self->scaling_factor + $zero->[Y],
    ];
    
    if (!$self->config2->{invert_y}) {
        my $ch = $self->GetSize->GetHeight;
        $p->[Y] = $ch - $p->[Y];
    }
    
    return $p;
}

sub scaled_points_to_pixel {
    my ($self, $points) = @_;
    
    return [
        map $self->unscaled_point_to_pixel($_),
            map Slic3r::Pointf->new_unscale(@$_),
            @$points
    ];
}

1;
