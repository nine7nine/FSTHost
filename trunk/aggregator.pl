#!/usr/bin/perl

use Gtk3;
use Data::Dumper;


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

Gtk3->init; # works if you didn't use -init on use
my $window = new Gtk3::Window ('toplevel');
$window->signal_connect('delete_event' => sub { Gtk3->main_quit; });

# Grid
my $grid = new Gtk3::Grid;

$window->add( $grid );

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

die "can't find any window" unless ($i);

$window->show_all;

Gtk3->main;
