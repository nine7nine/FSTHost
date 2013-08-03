#!/usr/bin/perl

use warnings;
#use strict;
use threads;
use threads::shared;
use Gtk3;
use Glib qw/TRUE FALSE/;
use Data::Dumper;
use POSIX qw(mkfifo);

my %shash;
my $grid;

sub xwininfo {
	my $name = shift;

	my @xwininfo = `xwininfo -int -name "$name"`;
	my %xw;

	chomp ( @xwininfo );
	foreach ( @xwininfo ) {
		#print "LINE: $line";
		if (m/Window id:\s*([0-9]+)/) {
			$xw{'xid'} = $1;
		} elsif (m/\W*(.*)\W*:\W*(.*)\W*/) {
			$xw{$1} = $2;
		}
	}

	return \%xw;
}

sub idle_thread {
	if ($shash{'die'} == 1) {
		Gtk3->main_quit;
		return FALSE;
	}
	
	my $line = <F>;
	if ($line) {
		chomp ($line);
		print $line . "\n";

		my $socket = new Gtk3::Socket;

		#$socket->set_size_request ( $xw->{'Width'} , $xw->{'Height'} );
		$grid->attach ( $socket, 0, $shash{'row'}++, 1, 1 );
		$socket->add_id ( $xw->{'xid'} );
	}

	return TRUE;
}

Glib::Object->set_threadsafe (TRUE);

# setup shared hash
share(%shash); #will work for first level keys
$shash{'die'} = 0;
$shash{'row'} = 0;

Gtk3->init; # works if you didn't use -init on use
my $window = new Gtk3::Window ('toplevel');
$window->signal_connect('delete_event' => sub { Gtk3->main_quit; });

# Grid
$grid = new Gtk3::Grid;

$window->add( $grid );

my $path = '/tmp/chuj';
unlink ($path) if ( -p $path );
mkfifo($path, 0700) || die "mkfifo $path failed: $!";

open(F, '<', $path) || die "can't open $path: $!";


=chuj
my $plug = $ARGV[0];
my $i = 0;
foreach (@ARGV) {
	my $xw = xwininfo ( $_ );
	next unless ( $xw->{'xid'} );

	my $socket = new Gtk3::Socket;

	$socket->set_size_request ( $xw->{'Width'} , $xw->{'Height'} );
	$grid->attach ( $socket, 0, $i++, 1, 1 );
	$socket->add_id ( $xw->{'xid'} );
}
=cut

#die "can't find any window" unless ($i);

Glib::Timeout->add( 300, \&idle_thread, undef, 200 );

$window->show_all;

Gtk3->main;

close(F);
unlink($path);

