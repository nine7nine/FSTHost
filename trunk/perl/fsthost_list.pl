#!/usr/bin/perl

=head1 NAME

B<FSTHost menu>

=head1 DESCRIPTION

FSTHost menu. Support for Gtk2 and Gtk3

=head1 EXAMPLES

C<fsthost_menu.pl>

or

C<fsthost_menu.pl hide>

=head1 ENVIRONMENT

To force Gtk version use:

export FSTMENU_GTK=ver # where I<ver> is 2 or 3

=head1 AUTHOR

Pawel Piatek <xj@wp.pl>

=cut

use v5.14;
#use strict;
#use warnings;
#use Data::Dumper;
use XML::LibXML;

our $filename = $ENV{'HOME'} . '/.fsthost.xml';

sub read_xml_db {
	my $fst = shift;

	my $parser = new XML::LibXML ();
	my $doc    = $parser->parse_file($filename);
	my $root = $doc->documentElement();
	my @nodes = $root->getChildrenByTagName('fst');
	foreach my $N ( @nodes ) {
		my $F = $N->getAttribute('file');
		$fst->{$F}->{'path'} = $N->getAttribute('path');
		$fst->{$F}->{'arch'} = $N->getAttribute('arch');

		my @childs = grep { $_->nodeType == XML_ELEMENT_NODE } $N->childNodes();
		map { $fst->{$F}->{$_->nodeName} = $_->textContent() } @childs;
	}
}

my %fst;
read_xml_db( \%fst );

my @keys = qw/name arch path/;
foreach my $f ( values %fst ) {
	my @v = map { $f->{$_} } @keys;
	say join('|', @v);
}
