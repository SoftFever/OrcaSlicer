# Notify about the end of slicing.
# The notifications are sent out using the Growl protocol if installed, and using DBus XWindow protocol.

package Slic3r::GUI::Notifier;
use Moo;

has 'growler' => (is => 'rw');

my $icon = $Slic3r::var->("Slic3r.png");

sub BUILD {
    my ($self) = @_;
    
    if (eval 'use Growl::GNTP; 1') {
        # register with growl
        eval {
            $self->growler(Growl::GNTP->new(AppName => 'Slic3r', AppIcon => $icon));
            $self->growler->register([{Name => 'SKEIN_DONE', DisplayName => 'Slicing Done'}]);
        };
        # if register() fails (for example because of a timeout), disable growler at all
        $self->growler(undef) if $@;
    }
}

sub notify {
    my ($self, $message) = @_;
    my $title = 'Slicing Done!';

    eval {
        $self->growler->notify(Event => 'SKEIN_DONE', Title => $title, Message => $message)
            if $self->growler;
    };
    # Net::DBus is broken in multithreaded environment
    if (0 && eval 'use Net::DBus; 1') {
        eval {
            my $session = Net::DBus->session;
            my $serv = $session->get_service('org.freedesktop.Notifications');
            my $notifier = $serv->get_object('/org/freedesktop/Notifications',
                                             'org.freedesktop.Notifications');
            $notifier->Notify('Slic3r', 0, $icon, $title, $message, [], {}, -1);
            undef $Net::DBus::bus_session;
        };
    }
}

1;
