#!/usr/bin/perl

use strict;
use warnings;
use Data::Dumper;
use IO::Socket;

our $Gtk;
our $window = undef;

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

sub add_button_clicked {
	my ( $b, $href ) = @_;

	my $host = 'localhost';
	my $port;
	$href->{'entry'}->get_text() =~ /(\w+):(\d+)|(\d+)/;
	if ( $3 ) {
		$port = $3;
	} else {
		$host = $1;
		$port = $2;
	}
	return unless ( $port );

	my $fst = new FST ( $host, $port );
	return unless ( $fst );

	$fst->show( $href->{'vbox'} );
}

sub gtk_vbox { return ($Gtk eq 'Gtk3') ? new Gtk3::Box ('vertical', 0) : new Gtk2::VBox (); }
sub gtk_hbox { return ($Gtk eq 'Gtk3') ? new Gtk3::Box ('horizontal', 0) : new Gtk2::HBox (); }

sub show_it {
	my $fst = shift;
	our $window;

	# Main Window
	$window = ($Gtk.'::Window')->new( 'toplevel' );
	$window->signal_connect( delete_event => sub { $Gtk->main_quit(); } );
	$window->set_icon_name ( 'fsthost' );
	$window->set_title ( 'FSTHost CTRL (' . $Gtk . ')' );
	$window->set_border_width(5);

	# Vbox
	my $vbox = gtk_vbox();
	$vbox->set_border_width ( 2 );
	$window->add ( $vbox );

	### FIRST HBOX
	my $hbox = gtk_hbox();
	$hbox->set_border_width ( 2 );
	$vbox->pack_start ( $hbox, 0, 0, 0 ); # child, expand, fill, padding

	# Entry
	my $entry = ($Gtk.'::Entry')->new();
	$hbox->pack_start ( $entry, 0, 0, 0 ); # child, expand, fill, padding

	# Add Button
	my $add_button = ($Gtk.'::Button')->new_from_stock('gtk-add');
        $add_button->set_tooltip_text ( 'Add new FSTHOST' );
	$add_button->signal_connect ( clicked => \&add_button_clicked, { entry => $entry, vbox => $vbox } );
	$hbox->pack_start ( $add_button, 0, 0, 0 ); # child, expand, fill, padding

	$window->show_all();

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
}

sub call {
	my ( $self, $cmd ) = @_;

	my $socket = $self->{'socket'};

	print $socket $cmd, "\n";
	$socket->flush();

	my @ret;
	while ( my $line = $socket->getline() ) {
#		$line =~ s/[^[:print:]]//g;
		print $line;
		last if $line =~ /\<OK\>/;
		push ( @ret, $line );
	}
	chomp ( @ret );
	return @ret;
}

sub presets {
	my $self = shift;
	my @presets = $self->call ( 'list_programs' );
	return @presets;
}

sub get_program {
	my $self = shift;
	( /PROGRAM:(\d+)/ and return $1 ) for ( $self->call ( 'get_program' ) );
}

sub set_program {
	my ( $self, $program ) = @_;
	$self->call ( 'set_program:' . $program );
}

sub close {
	my $self = shift;
	$self->call ('quit');
	close $self->{'socket'};
}

##################### GUI ####################################################################
sub sr_button_toggle {
	my ( $b, $fst ) = @_;
	$fst->call ( $b->get_active() ? 'resume' : 'suspend' );
}

sub editor_button_clicked {
	my ( $b, $fst ) = @_;
	$fst->call ( 'editor open' );
}

sub presets_combo_change {
	my ( $p, $fst ) = @_;
	$fst->set_program ( $p->get_active );
}

sub show {
	my ( $self, $vbox ) = @_;

	# Hbox
	my $hbox = main::gtk_hbox();
	$hbox->set_border_width ( 2 );
	$vbox->pack_start ( $hbox, 0, 0, 0 ); # child, expand, fill, padding

	# Label
	my $label = ($Gtk.'::Label')->new( $self->{'host'} . ':' . $self->{'port'} );
	$hbox->pack_start ( $label, 0, 0, 0 ); # child, expand, fill, padding

	# Suspend / Resume
	my $sr_button = ($Gtk.'::ToggleButton')->new_with_label('State');
	$sr_button->set_active(1);
	$sr_button->set_tooltip_text ( 'Suspend / Resume' );
	$sr_button->signal_connect ( 'clicked' => \&sr_button_toggle, $self );
	$hbox->pack_start ( $sr_button, 0, 0, 0 ); # child, expand, fill, padding

	# Editor Open/Close
	my $editor_button = ($Gtk.'::Button')->new_with_label('Editor');
	$editor_button->set_tooltip_text ( 'Editor Open' );
	$editor_button->signal_connect ( 'clicked' => \&editor_button_clicked, $self );
	$hbox->pack_start ( $editor_button, 0, 0, 0 ); # child, expand, fill, padding

	# Presets:
	my $presets_combo = ($Gtk.'::ComboBoxText')->new ();
	$presets_combo->set_tooltip_text ( 'Presets' );
	my @presets = $self->presets();
	my $t = 0;
	foreach ( @presets ) {
		my $txt = ++$t . '. ' . $_;
		$presets_combo->insert_text ( $t, $txt );
	}
	$presets_combo->set_active ( $self->get_program() );
	$presets_combo->signal_connect ( 'changed' => \&presets_combo_change, $self );
	$hbox->pack_start ( $presets_combo, 0, 0, 0 ); # child, expand, fill, padding

	$hbox->show_all();

	return 1;
}

package main;

################### MAIN ######################################################

$Gtk->init();

show_it();
$Gtk->main();

#$fst->close();

