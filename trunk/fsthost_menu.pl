#!/usr/bin/perl

use strict;
use Data::Dumper;
use XML::LibXML;
use Gtk3;

my $FSTHOST_GUI = 2; # 0 - no gui , 1 - hide, 2 - normal

my $filename = $ENV{'HOME'}."/.fsthost.xml";

sub start_fsthost { 
	my $tv = shift;
	my ($model, $iter) = $tv->get_selection->get_selected();
	# Get reference to our part of %fst hash
	my $arch = $model->get($iter, 1);
	my $path = $model->get($iter, 2);


	my $cmd = "env FSTHOST_GUI=$FSTHOST_GUI fsthost$arch \"$path\" >/dev/null 2>&1 &";

	print "Spawn: $cmd\n";
	system ( $cmd );
}

sub read_xml_db {
	my $fst = shift;

	# Parse XML DB
	my $parser = XML::LibXML->new();
	my $doc    = $parser->parse_file($filename);
	my $root = $doc->documentElement();
	my @nodes = $root->getChildrenByTagName('fst');
	foreach my $N (@nodes) {
		my $F = $N->getAttribute('file');
		$fst->{$F}->{'path'} = $N->getAttribute('path');
		$fst->{$F}->{'arch'} = $N->getAttribute('arch');

		foreach my $FN ($N->childNodes()) {
			next unless ($FN->nodeType == XML_ELEMENT_NODE);
			$fst->{$F}->{$FN->nodeName} = $FN->textContent();
		}
	}
}

my %fst;
read_xml_db ( \%fst );

#print Dumper \%fst;

Gtk3->init; # works if you didn't use -init on use
my $window = Gtk3::Window->new ('toplevel');
$window->signal_connect('delete_event' => sub { Gtk3->main_quit; });
#$window->set_border_width(5);
 
# VBox
my $vbox = Gtk3::VBox->new ( 0, 5 );
$window->add( $vbox );
$vbox->set_border_width( 5 );

# Scrolled window
#create a scrolled window that will host the treeview
my $sw = Gtk3::ScrolledWindow->new (undef, undef);
$sw->set_shadow_type ('etched-out');
$sw->set_policy ('automatic', 'automatic');
#This is a method of the Gtk3::Widget class,it will force a minimum 
#size on the widget. Handy to give intitial size to a 
#Gtk3::ScrolledWindow class object
$sw->set_size_request (300, 500);
#method of Gtk3::Container
#$sw->set_border_width(5);
$vbox->pack_start($sw, 1, 1,0);

# TreeView
my $tree_store = Gtk3::TreeStore->new(qw/Glib::String Glib::String Glib::String/);

#fill it with arbitry data
foreach (keys %fst) { 	
	#the iter is a pointer in the treestore. We use to add data.
	my $iter = $tree_store->append(undef);
	$tree_store->set ($iter, 0 => $_, 1 => $fst{$_}->{'arch'}, 2 => $fst{$_}->{'path'});
}

#this will create a treeview, specify $tree_store as its model
my $tree_view = Gtk3::TreeView->new($tree_store);

# Connect double-click signal
$tree_view->signal_connect('row-activated' => \&start_fsthost );

#create a Gtk3::TreeViewColumn to add to $tree_view
my $tree_column = Gtk3::TreeViewColumn->new();
$tree_column->set_title ("Plugins");

#create a renderer that will be used to display info in the model
my $renderer = Gtk3::CellRendererText->new;
#add this renderer to $tree_column. This works like a Gtk3::Hbox
# so you can add more than one renderer to $tree_column			
$tree_column->pack_start ($renderer, 0);
# set the cell "text" attribute to column 0   
#- retrieve text from that column in treestore 
# Thus, the "text" attribute's value will depend on the row's value
# of column 0 in the model($treestore),
# and this will be displayed by $renderer,
# which is a text renderer
$tree_column->add_attribute($renderer, text => 0);

#add $tree_column to the treeview
$tree_view->append_column ($tree_column);

# make it searchable
$tree_view->set_search_column(0);

# Allow sorting on the column
$tree_column->set_sort_column_id(0);

# Allow drag and drop reordering of rows
$tree_view->set_reorderable(1);
$sw->add($tree_view);

$window->show_all;
Gtk3->main;
