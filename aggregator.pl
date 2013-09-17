#!/usr/bin/perl

use strict;
use warnings;
#use Data::Dumper;
use Gtk3;

use constant CLASS => 'fsthost32';
use constant TITLE => 'FSTHost Aggregator';

our %xw;
our $i = 0;

sub xwininfo {
	my ( $xw, $class ) = @_;

	my @xwininfo = `xwininfo -root -int -tree`;

	chomp ( @xwininfo );
	foreach my $line ( grep( /\(\"$class\"/, @xwininfo) ) {
#		print "LINE: $line\n";
		if ($line =~ m/\s*(\d+)\s*\"(\S+)\".*?(\d+)x(\d+)\+/) {
			my $xid = $1;
			my $name = $2;
			my $width = $3;
			my $height = $4;

			next if ($name eq $class);
			next if exists $xw{$xid};

			$xw{$xid}{'name'} = $name;
			$xw{$xid}{'width'} = $width;
			$xw{$xid}{'height'} = $height;
		}
	}
}

sub idle_thread {
	print "IDLE\n";

	my $xw = shift;
	my $grid = $xw->{'grid'};
	my $window = $xw->{'window'};

	xwininfo ( $xw, CLASS );
	foreach (keys %$xw) {
		next if ($_ eq 'grid' || $_ eq 'window');
		next if exists ( $xw->{$_}->{'added'} );
		my $xxw = $xw->{$_};

		print "XID:$_ Name:$$xxw{name} Width:$$xxw{width} Height:$$xxw{height}\n";

		my $socket = new Gtk3::Socket;

		$socket->set_size_request ( $xxw->{'width'} , $xxw->{'height'} );
		$grid->attach ( $socket, 0, $i++, 1, 1 );
		$socket->add_id ( $_ );
		$socket->show();

		$xxw->{'added'} = 1;
	}
	return Glib::SOURCE_CONTINUE;
}

Gtk3->init; # works if you didn't use -init on use
my $window = new Gtk3::Window ('toplevel');
$window->set_title( TITLE );
$window->signal_connect('delete_event' => sub { Gtk3->main_quit; });

# Grid
my $grid = new Gtk3::Grid;
$window->add( $grid );
$xw{'grid'} = $grid;
$xw{'window'} = $window;
idle_thread( \%xw );

Glib::Timeout->add( 1000, \&idle_thread, \%xw, 200 );


$window->show_all;

Gtk3->main;

