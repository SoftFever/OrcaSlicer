package Slic3r::GUI::AboutDialog;
use strict;
use warnings;
use utf8;

use Wx qw(:font :html :misc :dialog :sizer :systemsettings :frame :id);
use Wx::Event qw(EVT_HTML_LINK_CLICKED EVT_LEFT_DOWN EVT_BUTTON);
use Wx::Print;
use Wx::Html;
use base 'Wx::Dialog';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1, 'About Slic3r', wxDefaultPosition, [600, 340], wxCAPTION);

    $self->SetBackgroundColour(Wx::wxWHITE);
    my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $self->SetSizer($hsizer);

    # logo
    my $logo = Slic3r::GUI::AboutDialog::Logo->new($self, -1, wxDefaultPosition, wxDefaultSize);
    $logo->SetBackgroundColour(Wx::wxWHITE);
    $hsizer->Add($logo, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    my $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $hsizer->Add($vsizer, 1, wxEXPAND, 0);

    # title
    my $title = Wx::StaticText->new($self, -1, $Slic3r::FORK_NAME, wxDefaultPosition, wxDefaultSize);
    my $title_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $title_font->SetWeight(wxFONTWEIGHT_BOLD);
    $title_font->SetFamily(wxFONTFAMILY_ROMAN);
    $title_font->SetPointSize(24);
    $title->SetFont($title_font);
    $vsizer->Add($title, 0, wxALIGN_LEFT | wxTOP, 30);

    # version
    my $version = Wx::StaticText->new($self, -1, "Version $Slic3r::VERSION", wxDefaultPosition, wxDefaultSize);
    my $version_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $version_font->SetPointSize(&Wx::wxMSW ? 9 : 11);
    $version->SetFont($version_font);
    $vsizer->Add($version, 0, wxALIGN_LEFT | wxBOTTOM, 10);

    # text
    my $text =
        '<html>' .
        '<body bgcolor="#ffffff" link="#808080">' .
        '<font color="#808080">' .
        'Copyright &copy; 2016 Vojtech Bubnik, Prusa Research. <br />' .
        'Copyright &copy; 2011-2016 Alessandro Ranellucci. <br />' .
        '<a href="http://slic3r.org/">Slic3r</a> is licensed under the ' .
        '<a href="http://www.gnu.org/licenses/agpl-3.0.html">GNU Affero General Public License, version 3</a>.' .
        '<br /><br /><br />' .
        'Contributions by Henrik Brix Andersen, Nicolas Dandrimont, Mark Hindess, Petr Ledvina, Y. Sapir, Mike Sheldrake and numerous others. ' .
        'Manual by Gary Hodgson. Inspired by the RepRap community. <br />' .
        'Slic3r logo designed by Corey Daniels, <a href="http://www.famfamfam.com/lab/icons/silk/">Silk Icon Set</a> designed by Mark James. ' .
        '</font>' .
        '</body>' .
        '</html>';
    my $html = Wx::HtmlWindow->new($self, -1, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER);
    my $font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    my $size = &Wx::wxMSW ? 8 : 10;
    $html->SetFonts($font->GetFaceName, $font->GetFaceName, [$size, $size, $size, $size, $size, $size, $size]);
    $html->SetBorders(2);
    $html->SetPage($text);
    $vsizer->Add($html, 1, wxEXPAND | wxALIGN_LEFT | wxRIGHT | wxBOTTOM, 20);
    EVT_HTML_LINK_CLICKED($self, $html, \&link_clicked);
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    $self->SetEscapeId(wxID_CLOSE);
    EVT_BUTTON($self, wxID_CLOSE, sub {
        $self->EndModal(wxID_CLOSE);
        $self->Close;
    });
    $vsizer->Add($buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
    
    EVT_LEFT_DOWN($self, sub { $self->Close });
    EVT_LEFT_DOWN($logo, sub { $self->Close });
    
    return $self;
}

sub link_clicked {
    my ($self, $event) = @_;

    Wx::LaunchDefaultBrowser($event->GetLinkInfo->GetHref);
    $event->Skip(0);
}

package Slic3r::GUI::AboutDialog::Logo;
use Wx qw(:bitmap :dc);
use Wx::Event qw(EVT_PAINT);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my $self = $class->SUPER::new(@_);

    $self->{logo} = Wx::Bitmap->new(Slic3r::var("Slic3r_192px.png"), wxBITMAP_TYPE_PNG);
    $self->SetMinSize(Wx::Size->new($self->{logo}->GetWidth, $self->{logo}->GetHeight));

    EVT_PAINT($self, \&repaint);

    return $self;
}

sub repaint {
    my ($self, $event) = @_;

    my $dc = Wx::PaintDC->new($self);
    $dc->SetBackgroundMode(wxTRANSPARENT);

    my $size = $self->GetSize;
    my $logo_w = $self->{logo}->GetWidth;
    my $logo_h = $self->{logo}->GetHeight;
    $dc->DrawBitmap($self->{logo}, ($size->GetWidth - $logo_w) / 2, ($size->GetHeight - $logo_h) / 2, 1);

    $event->Skip;
}

1;
