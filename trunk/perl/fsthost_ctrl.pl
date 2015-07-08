#!/usr/bin/perl

use v5.14;
#use warnings;
#use Data::Dumper;
use IO::Socket;

our $Gtk;

# Load GTK modules
BEGIN {
	our $Gtk;

	my @GTK;
	if ( exists $ENV{'FSTMENU_GTK'} ) {
		push ( @GTK, 'Gtk'.$ENV{'FSTMENU_GTK'} );
	} else {
#		push ( @GTK, qw/Gtk2 Gtk3/ );
		push ( @GTK, qw/Gtk3 Gtk2/ );
	}

	for my $pkg ( @GTK ) {
		last if eval {
			require $pkg.'.pm';
			$pkg->import();
			$Gtk = $pkg;
		};
	}
	die "Can't find modules Gtk[23]" unless ( $Gtk );
}


# Auxiliary methods for GTK2 / GTK3 support
sub gtk_vbox { return ($Gtk eq 'Gtk3') ? Gtk3::Box->new ('vertical', 0) : Gtk2::VBox->new(); }
sub gtk_hbox { return ($Gtk eq 'Gtk3') ? Gtk3::Box->new ('horizontal', 0) : Gtk2::HBox->new(); }
sub gtk_combo { return ($Gtk eq 'Gtk3') ? Gtk3::ComboBoxText->new() : Gtk2::ComboBox->new_text(); }

package MainForm;

sub new {
	my $class = shift;
	my $self = {};

	my $object = bless ( $self, $class );
	$object->_init();
	return $object;
}

sub add_fst {
	my ( $be, $self ) = @_;

	my $host = 'localhost';
	my $port;
	$self->{'entry'}->get_text() =~ /(\w+):(\d+)|(\d+)/;
	if ( $3 ) {
		$port = $3;
	} else {
		$host = $1;
		$port = $2;
	}
	return unless ( $port );

	my $fst = FST_BOX->new ( $host, $port );
	return unless ( $fst );
	$self->{'vbox'}->pack_start ( $fst->{'hbox'}, 0, 0, 0 ); # child, expand, fill, padding
}

sub _init {
	my $self = shift;

	# Main Window
	my $window = ($Gtk.'::Window')->new( 'toplevel' );
	$window->signal_connect( delete_event => sub { $Gtk->main_quit(); } );
	$window->set_icon_name ( 'fsthost' );
	$window->set_title ( 'FSTHost CTRL (' . $Gtk . ')' );
	$window->set_border_width(5);

	# Vbox
	my $vbox = main::gtk_vbox();
	$vbox->set_border_width ( 2 );
	$window->add ( $vbox );

	### FIRST HBOX aka toolbar
	my $hbox = main::gtk_hbox();
	$hbox->set_border_width ( 2 );
	$vbox->pack_start ( $hbox, 0, 0, 0 ); # child, expand, fill, padding

	# Entry
	my $entry = ($Gtk.'::Entry')->new();
	$entry->signal_connect ( 'activate' => \&add_fst, $self );
	$hbox->pack_start ( $entry, 0, 0, 0 ); # child, expand, fill, padding

	# Add Button
	my $add_button = ($Gtk.'::Button')->new_from_stock('gtk-add');
        $add_button->set_tooltip_text ( 'Add new FSTHOST' );
	$add_button->signal_connect ( clicked => \&add_fst, $self );
	$hbox->pack_start ( $add_button, 0, 0, 0 ); # child, expand, fill, padding

	$window->show_all();

	$self->{'window'} = $window;
	$self->{'vbox'} = $vbox;
	$self->{'toolbar'} = $hbox;
	$self->{'entry'} = $entry;
	$self->{'add_button'} = $add_button;

	return 1;
}

package FST_BOX;

use parent -norequire, 'FST';

sub new {
	my $class = shift;

	my $self = $class->SUPER::new ( shift, shift );
	return undef unless ( $self );
	
        my $object = bless ( $self, $class );
	unless ( $object->show() ) {
		$object->close();
		return undef;
	}

	return $object
}

sub sr_button_toggle {
	my ( $b, $self ) = @_;
	$self->call ( $b->get_active() ? 'suspend' : 'resume' );
}

sub editor_button_toggle {
	my ( $b, $self ) = @_;
	$self->call ( $b->get_active() ? 'editor_open' : 'editor_close' );
}

sub presets_combo_change {
	my ( $c, $self ) = @_;
	$self->set_program ( $c->get_active );
}

sub channels_combo_change {
	my ( $c, $self ) = @_;
	$self->set_channel ( $c->get_active );
}

sub close_button_clicked {
	my ( $b, $self ) = @_;
	my $window = $self->{'hbox'}->get_toplevel();
	Glib::Source->remove( $self->{'idle_id'} );
	$self->{'hbox'}->destroy();
	$window->resize(1,1);
}

sub action {
	my ( $self, $action, $value ) = @_;

	my %ACTION = (
		PROGRAM	=> sub { $self->{'presets_combo'}->set_active(shift); },
		CHANNEL	=> sub { $self->{'channels_combo'}->set_active(shift); },
		BYPASS	=> sub { $self->{'bypass_button'}->set_active(shift); },
		EDITOR	=> sub { $self->{'editor_button'}->set_active(shift); }
	);

	return unless exists $ACTION{$action};
	$ACTION{$action}->( $value );
}

sub idle {
	my $self = shift;

	my $news = $self->news(0);
	$self->action( $_, $news->{$_} ) for ( keys %$news );

	return 1;
}

sub show {
	my $self = shift;

	# Hbox
	my $hbox = main::gtk_hbox();
	$hbox->set_border_width ( 2 );

	# Label
	my $label = ($Gtk.'::Label')->new( $self->{'host'} . ':' . $self->{'port'} );
	$hbox->pack_start ( $label, 0, 0, 0 ); # child, expand, fill, padding

	# Suspend / Resume
	my $sr_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-stop');
	$sr_button->set_tooltip_text ( 'Suspend / Resume' );
	$sr_button->signal_connect ( 'clicked' => \&sr_button_toggle, $self );
	$hbox->pack_start ( $sr_button, 0, 0, 0 ); # child, expand, fill, padding

	# Editor Open/Close
	my $editor_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-properties');
	$editor_button->set_tooltip_text ( 'Editor' );
	$editor_button->signal_connect ( 'clicked' => \&editor_button_toggle, $self );
	$hbox->pack_start ( $editor_button, 0, 0, 0 ); # child, expand, fill, padding

	# Presets:
	my $presets_combo = main::gtk_combo();
	$presets_combo->set_tooltip_text ( 'Presets' );
	my $t = 0;
	$presets_combo->insert_text ( $t++, $t . '. ' . $_  ) for $self->presets();
	$presets_combo->signal_connect ( 'changed' => \&presets_combo_change, $self );
	$hbox->pack_start ( $presets_combo, 0, 0, 0 ); # child, expand, fill, padding

	# Channels:
	my $channels_combo = main::gtk_combo();
	$channels_combo->set_tooltip_text ( 'MIDI Channels' );
	$channels_combo->insert_text( $_ , 'Ch ' . $_ ) for ( 0 .. 17 );
	$channels_combo->signal_connect ( 'changed' => \&channels_combo_change, $self );
	$hbox->pack_start ( $channels_combo, 0, 0, 0 ); # child, expand, fill, padding

	# Close
	my $close_button = ($Gtk.'::ToolButton')->new_from_stock('gtk-close');
	$close_button->set_tooltip_text ( 'Close' );
	$close_button->signal_connect ( 'clicked' => \&close_button_clicked, $self );
	$hbox->pack_start ( $close_button, 0, 0, 0 ); # child, expand, fill, padding

	$hbox->show_all();

	$self->{'idle_id'} = Glib::Timeout->add ( 1000, \&idle, $self );

	$self->{'bypass_button'} = $sr_button;
	$self->{'editor_button'} = $editor_button;
	$self->{'presets_combo'} = $presets_combo;
	$self->{'channels_combo'} = $channels_combo;
	$self->{'hbox'} = $hbox;

	# Get all initial values
	my $news = $self->news(1);
	$self->action( $_, $news->{$_} ) for ( keys %$news );

	return 1;
}

package FST;

sub new {
	my $class = shift;
	my $self = {
		host => shift,
		port => shift
	};

        my $object = bless ( $self, $class );
	return ( $object->_connect() ) ? $object : undef;
}

sub _connect {
	my $self = shift;
	$self->{'socket'} = IO::Socket::INET->new ( $self->{'host'} . ':' . $self->{'port'} );
	# return tru if this assignment is sucessfull
}

sub call {
	my ( $self, $cmd, $value ) = @_;

	my $socket = $self->{'socket'};

	my $line = $cmd;
	$line .= ':' . $value if $value;

	say $socket $line;
#	say $line;

	my @ret;
	while ( my $line = $socket->getline() ) {
		chomp($line);
#		$line =~ s/[^[:print:]]//g;
#		say $line;
		last if ( $line eq '<OK>' || $line eq '<FAIL>' );
		push ( @ret, $line );
	}
	return @ret;
}

sub presets {
	my $self = shift;
	my @presets = $self->call ( 'list_programs' );
	return @presets;
}

sub news {
	my ( $self, $all ) = @_;
	my $cmd = 'news';
	$cmd .= ':all' if $all;

	my %ret = map { /(\w+):(\d+)/ } $self->call($cmd);
	return \%ret;
}

sub set_program {
	my ( $self, $program ) = @_;
	$self->call ( 'set_program', $program );
}

sub set_channel {
	my ( $self, $channel ) = @_;
	$self->call ( 'set_channel', $channel );
}

sub close {
	my $self = shift;
	return unless ( $self->{'socket'} );
	$self->call ('quit');
	close $self->{'socket'};
}

sub DESTROY {
	my $self = shift;
	$self->close();
}

################### MAIN ######################################################
package main;

$Gtk->init();

MainForm->new();

$Gtk->main();

