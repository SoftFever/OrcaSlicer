package Slic3r::GUI::SystemInfo;
use strict;
use warnings;
use utf8;

use Wx qw(:font :html :misc :dialog :sizer :systemsettings :frame :id wxTheClipboard);
use Wx::Event qw(EVT_HTML_LINK_CLICKED EVT_LEFT_DOWN EVT_BUTTON);
use Wx::Html;
use base 'Wx::Dialog';

sub new {
    my ($class, %params) = @_;
    my $self = $class->SUPER::new($params{parent}, -1, 'Slic3r Prusa Edition - System Information', wxDefaultPosition, [600, 340], 
        wxDEFAULT_DIALOG_STYLE | wxMAXIMIZE_BOX | wxRESIZE_BORDER);
    $self->{text_info} = $params{text_info};

    $self->SetBackgroundColour(Wx::wxWHITE);
    my $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $self->SetSizer($vsizer);

    # text
    my $text =
        '<html>' .
        '<body bgcolor="#ffffff" link="#808080">' .
        ($params{slic3r_info} // '') .
        ($params{copyright_info} // '') .
        ($params{system_info} // '') .
        ($params{opengl_info} // '') .
        '</body>' .
        '</html>';
    my $html = $self->{html} = Wx::HtmlWindow->new($self, -1, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);
    my $font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    my $size = &Wx::wxMSW ? 8 : 10;
    $html->SetFonts($font->GetFaceName, $font->GetFaceName, [$size * 1.5, $size * 1.4, $size * 1.3, $size, $size, $size, $size]);
    $html->SetBorders(10);
    $html->SetPage($text);
    $vsizer->Add($html, 1, wxEXPAND | wxALIGN_LEFT | wxRIGHT | wxBOTTOM, 0);
    EVT_HTML_LINK_CLICKED($self, $html, \&link_clicked);
    
    my $buttons = $self->CreateStdDialogButtonSizer(wxOK);
    my $btn_copy_to_clipboard = Wx::Button->new($self, -1, "Copy to Clipboard", wxDefaultPosition, wxDefaultSize);
    $buttons->Insert(0, $btn_copy_to_clipboard, 0, wxLEFT, 5);
    EVT_BUTTON($self, $btn_copy_to_clipboard, \&copy_to_clipboard);
    $self->SetEscapeId(wxID_CLOSE);
    EVT_BUTTON($self, wxID_CLOSE, sub {
        $self->EndModal(wxID_CLOSE);
        $self->Close;
    });
#    $vsizer->Add($buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
    $vsizer->Add($buttons, 0, wxEXPAND | wxALL, 5);
    
    return $self;
}

sub link_clicked {
    my ($self, $event) = @_;

    Wx::LaunchDefaultBrowser($event->GetLinkInfo->GetHref);
    $event->Skip(0);
}

sub copy_to_clipboard {
    my ($self, $event) = @_;
    my $data = $self->{text_info};
    wxTheClipboard->Open;
    wxTheClipboard->SetData(Wx::TextDataObject->new($data));
    wxTheClipboard->Close;
}

1;
