#!/usr/bin/perl

use v5.14;
#use warnings;
#use Data::Dumper;
use POSIX ':sys_wait_h';
use IO::Socket;
use Time::HiRes qw/usleep/;

our $Gtk;
our $FPID;
our $PORTDIR = '/tmp/fsthost';

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

END {
	our $FPID;
	kill('TERM',$FPID) if $FPID;
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
	$object->_autodetect();
	return $object;
}

sub add_fsthost {
	my ( $self, $host, $port ) = @_;

	$host //= 'localhost';
	FSTHOST->new ( $host, $port, $self->{'vbox'} )
		or return;
}

sub _autodetect {
	my $self = shift;

	opendir ( my $D, $PORTDIR )
		or return;

	while ( my $F = readdir($D) ) {
		my ( $pid, $port ) = $F =~ /(\d+)\.(\d+)\.port/
			or next;

#		say $F;
		my $proc_exists = kill(0, $pid); 
		if ( $proc_exists ) {
			$self->add_fsthost(undef, $port);
		} else {
			say "Process $pid daesn't exists";
			unlink $F;
		}
	}
	closedir($D);
}

sub add_fst_entry {
	my ( $be, $self ) = @_;

	$self->{'entry'}->get_text() =~ /(\w+):(\d+)|(\d+)/;
	my $host = ($3) ? undef : $1;
	my $port = ($3) ? $3 : $2;
	return unless $port;

	$self->add_fsthost( $host, $port );
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
	$entry->signal_connect ( 'activate' => \&add_fst_entry, $self );
	$hbox->pack_start ( $entry, 0, 0, 0 ); # child, expand, fill, padding

	# Add Button
	my $add_button = ($Gtk.'::Button')->new_from_stock('gtk-add');
        $add_button->set_tooltip_text ( 'Add new FSTHOST' );
	$add_button->signal_connect ( clicked => \&add_fst_entry, $self );
	$hbox->pack_start ( $add_button, 0, 0, 0 ); # child, expand, fill, padding

	$window->show_all();

	$self->{'window'} = $window;
	$self->{'vbox'} = $vbox;
	$self->{'toolbar'} = $hbox;
	$self->{'entry'} = $entry;
	$self->{'add_button'} = $add_button;

	return 1;
}

package FSTHOST;

sub new {
	my $class = shift;
	my $self = {
		host => shift,
		port => shift,
		vbox => shift,
		fst => {}
	};

        my $object = bless ( $self, $class );
	$object->_connect() or
		return undef;

	$object->_init() or
		return undef;

	return $object;
}

sub _connect {
	my $self = shift;
	$self->{'socket'} = IO::Socket::INET->new ( $self->{'host'} . ':' . $self->{'port'} );
	# return tru if this assignment is sucessfull
}

sub _init {
	my $self = shift;

	foreach ( $self->call('list_plugins') ) {
		my $fst = FST_BOX->new ( $_, $self )
			or next;

		$self->{'fst'}->{$_} = $fst;
		$self->{'vbox'}->pack_start ( $fst->{'hbox'}, 0, 0, 0 ); # child, expand, fill, padding
	}

	# Get all initial values
	$self->dispatch_news(1);

	$self->{'idle_id'} = Glib::Timeout->add ( 1000, \&idle, $self );
}

sub call {
	my ( $self, $cmd ) = @_;

	my $socket = $self->{'socket'};
	say $socket $cmd;
#	say $cmd;

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

sub idle {
	my $self = shift;
	$self->dispatch_news(0);
	return 1;
}

sub close {
	my $self = shift;
	return unless ( $self->{'socket'} );
	$self->call ('quit');
	close $self->{'socket'};
	Glib::Source->remove( $self->{'idle_id'} );
}

sub DESTROY {
	my $self = shift;
	$self->close();
}

sub dispatch_news {
	my ( $self, $all ) = @_;
	my $cmd = 'news';
	$cmd .= ':all' if $all;

	my %ACTION = (
		PROGRAM	=> sub { my $box = shift; $box->{'presets_combo'}->set_active(shift); },
		CHANNEL	=> sub { my $box = shift; $box->{'channels_combo'}->set_active(shift); },
		BYPASS	=> sub { my $box = shift; $box->{'bypass_button'}->set_active(shift); },
		EDITOR	=> sub { my $box = shift; $box->{'editor_button'}->set_active(shift); },
		VOLUME  => sub {
			my ( $box, $value ) = @_;
			my $vb = $box->{'volume_button'};
			$vb->signal_handlers_block_by_func(\&volume_change, $box);
			$vb->set_value( $value / 127 );
			$vb->signal_handlers_unblock_by_func(\&volume_change, $box);
		},
		MIDI_LEARN => sub {
			my ( $box, $value ) = @_;
			my $mle = $box->{'midi_learn_button'};
			$mle->set_active($value);
			$mle->set_tooltip_text(
				"MIDI Learn\n" .
				join "\n", map { s/^(\d+):/CC $1 => / and $_ } $box->call('list_midi_map')
			);
		}
	);

	foreach ( $self->call($cmd) ) {
		my ( $plug, $action, $value ) = m/([\w-+]+):([\w-]+):(\d+)/;
		next unless exists $ACTION{$action};

		# TODO: if new plug then create instance
		next unless exists $self->{'fst'}->{$plug};
		my $box = $self->{'fst'}->{$plug};

		$ACTION{$action}->( $box, $value );
	}
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

sub show_error {
	my ( $self, $msg ) = @_;

	my $errdialog = ($Gtk.'::MessageDialog')->new(
		undef, 'destroy-with-parent', 'error',
		'close', $msg
	);
	$errdialog->run();
	$errdialog->destroy();
}

sub save_button_clicked {
	my ( $b, $self ) = @_;
	my $dialog = ($Gtk.'::FileChooserDialog')->new (
		'Save Plugin State', undef, 'save',
		'gtk-cancel' => 'cancel',
		'gtk-ok'     => 'ok'
	);
	$dialog->set_do_overwrite_confirmation(1);

	my @filters = (
		{ name => 'FST Plugin State', pattern => '*.fps' },
		{ name => 'FXB Bank', pattern => '*.fxb' },
		{ name => 'FXP Preset', pattern => '*.fxp' }
	);

	foreach (@filters) {
		my $filter = ($Gtk.'::FileFilter')->new();
		$filter->set_name( $_->{'name'} );
		$filter->add_pattern( $_->{'pattern'} );
		$dialog->add_filter($filter);
	}

	if ( $dialog->run() eq 'ok' ) {
		my $file = $dialog->get_filename();
		$self->call( 'save', $file );
		# TODO: need handle <FAIL>
#		$self->show_error( 'Error saving file: ' . $file );
	}
	$dialog->destroy();
}

sub load_button_clicked {
	my ( $b, $self ) = @_;
	my $dialog = ($Gtk.'::FileChooserDialog')->new (
		'Load Plugin State', undef, 'open',
		'gtk-cancel' => 'cancel',
		'gtk-ok'     => 'ok'
	);

	my @filters = (
		{ name => 'FST Plugin State', pattern => '*.fps' },
		{ name => 'FXB Bank', pattern => '*.fxb' },
		{ name => 'FXP Preset', pattern => '*.fxp' }
	);

	my $filter = ($Gtk.'::FileFilter')->new();
	$filter->set_name( 'Supported files (*.fps, *.fxp, *.fxb)' );
	$filter->add_pattern( '*.fps' );
	$filter->add_pattern( '*.fxp' );
	$filter->add_pattern( '*.fxb' );
	$dialog->add_filter($filter);

	if ( $dialog->run() eq 'ok' ) {
		$self->call( 'load', $dialog->get_filename() );
		# TODO: need handle <FAIL>
#		$self->show_error( 'Error loading file: ' . $file );
	}

	$dialog->destroy();
}

sub volume_change {
	my ( $b, $value, $self ) = @_;
	my $V = int ( 127 * $value );
	$self->call( 'set_volume', $V );
}

sub editor_button_toggle {
	my ( $b, $self ) = @_;
	$self->call ( 'editor', $b->get_active() ? 'open' : 'close' );
}

sub mle_button_toggle {
	my ( $b, $self ) = @_;
	$self->call ( 'midi_learn', $b->get_active() ? 'start' : 'stop' );
}

sub presets_combo_change {
	my ( $c, $self ) = @_;
	$self->set_program ( $c->get_active );
}

sub channels_combo_change {
	my ( $c, $self ) = @_;
	$self->set_channel ( $c->get_active );
}

#sub close_button_clicked {
#	my ( $b, $self ) = @_;
#	my $window = $self->{'hbox'}->get_toplevel();
#	$self->{'hbox'}->destroy();
#	$window->resize(1,1);
#}

sub show {
	my $self = shift;

	# Hbox
	my $hbox = main::gtk_hbox();
	$hbox->set_border_width ( 2 );

	# Suspend / Resume
	my $sr_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-stop');
	$sr_button->set_tooltip_text ( 'Suspend / Resume' );
	$sr_button->signal_connect ( 'clicked' => \&sr_button_toggle, $self );
	$hbox->pack_start ( $sr_button, 0, 0, 0 ); # child, expand, fill, padding

	# Load Button
	my $load_button = ($Gtk.'::ToolButton')->new_from_stock('gtk-open');
        $load_button->set_tooltip_text ( 'Load' );
	$load_button->signal_connect ( clicked => \&load_button_clicked, $self );
	$hbox->pack_start ( $load_button, 0, 0, 0 ); # child, expand, fill, padding

	# Save Button
	my $save_button = ($Gtk.'::ToolButton')->new_from_stock('gtk-save-as');
        $save_button->set_tooltip_text ( 'Save' );
	$save_button->signal_connect ( clicked => \&save_button_clicked, $self );
	$hbox->pack_start ( $save_button, 0, 0, 0 ); # child, expand, fill, padding

	# Editor Open/Close
	my $editor_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-properties');
	$editor_button->set_tooltip_text ( 'Editor' );
	$editor_button->signal_connect ( 'clicked' => \&editor_button_toggle, $self );
	$hbox->pack_start ( $editor_button, 0, 0, 0 ); # child, expand, fill, padding

	# Midi Learn button
	my $mle_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-dnd');
	$mle_button->signal_connect ( 'clicked' => \&mle_button_toggle, $self );
	$hbox->pack_start ( $mle_button, 0, 0, 0 ); # child, expand, fill, padding

	# Channels:
	my $channels_combo = main::gtk_combo();
	$channels_combo->set_tooltip_text ( 'MIDI Channels' );
	$channels_combo->insert_text( $_ , 'Ch ' . $_ ) foreach ( 0 .. 17 );
	$channels_combo->signal_connect ( 'changed' => \&channels_combo_change, $self );
	$hbox->pack_start ( $channels_combo, 0, 0, 0 ); # child, expand, fill, padding

	# Volume
	my $volume_button = ($Gtk.'::VolumeButton')->new();
	$volume_button->signal_connect ( 'value-changed' => \&volume_change, $self );
	$hbox->pack_start ( $volume_button, 0, 0, 0 ); # child, expand, fill, padding

	# Close
#	my $close_button = ($Gtk.'::ToolButton')->new_from_stock('gtk-close');
#	$close_button->set_tooltip_text ( 'Close' );
#	$close_button->signal_connect ( 'clicked' => \&close_button_clicked, $self );
#	$hbox->pack_start ( $close_button, 0, 0, 0 ); # child, expand, fill, padding

	# Presets:
	my $presets_combo = main::gtk_combo();
	$presets_combo->set_tooltip_text ( 'Presets' );
	my $t = 0;
	$presets_combo->insert_text ( $t, $t++ . '. ' . $_  ) for $self->presets();
	$presets_combo->signal_connect ( 'changed' => \&presets_combo_change, $self );
	$hbox->pack_start ( $presets_combo, 0, 0, 0 ); # child, expand, fill, padding

	# Label
	my $fsthost = $self->{'fsthost'};
	my $L = $fsthost->{'host'} . ':' . $fsthost->{'port'} . ':' . $self->{'name'};
	my $label = ($Gtk.'::Label')->new( $L );
	$hbox->pack_start ( $label, 0, 0, 0 ); # child, expand, fill, padding

	$hbox->show_all();

	$self->{'bypass_button'} = $sr_button;
	$self->{'editor_button'} = $editor_button;
	$self->{'volume_button'} = $volume_button;
	$self->{'midi_learn_button'} = $mle_button;
	$self->{'presets_combo'} = $presets_combo;
	$self->{'channels_combo'} = $channels_combo;
	$self->{'hbox'} = $hbox;

	return 1;
}

package FST;

sub new {
	my $class = shift;
	my $self = {
		name => shift,
		fsthost => shift
	};
}

sub presets {
	my $self = shift;
	my @presets = map { s/^[\w-+]+:\d+:// and $_ } $self->call ( 'list_programs' );
	return @presets;
}

sub set_program {
	my ( $self, $program ) = @_;
	$self->call ( 'set_program', $program );
}

sub set_channel {
	my ( $self, $channel ) = @_;
	$self->call ( 'set_channel', $channel );
}

sub call {
	my ( $self, $cmd, $value ) = @_;

	$cmd .= ':' . $self->{'name'};
	$cmd .= ':' . $value if defined $value;

	my $fsthost = $self->{'fsthost'};
	return $fsthost->call($cmd);
}

################### MAIN ######################################################
package main;

if ( @ARGV > 0 ) {
	$FPID = fork;
	die 'Cannot fork' unless defined $FPID;

	if ( $FPID == 0 ) {
#		close (STDOUT);
#		close (STDERR);
		# FIXME: detetect ARCH ( 32 , 64 )
		my $A = $ENV{'A'} // 32;
		exec('fsthost' . $A, '-n', '-S 0', @ARGV);
		die 'Cannot exec: ' . $!;
	}

	say 'Waiting for socket ...';
	my $FP = ${PORTDIR} . '/' . ${FPID} . '.[0-9]*.port';
	while ( 1 ) {
		my $F = glob $FP;
		last if defined $F;

		my $kid = waitpid( $FPID, WNOHANG );
		die 'Kid died :-)' if $kid < 0;

		usleep(100 * 1000); # 100 ms
	}
}

$Gtk->init();

MainForm->new();

$Gtk->main();

