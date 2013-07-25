#!/usr/bin/perl

use strict;
use Data::Dumper;
use XML::LibXML;
use Gtk3;

my $FSTHOST_GUI = 2; # 0 - no gui , 1 - hide, 2 - normal

my $filename = $ENV{'HOME'}."/.fsthost.xml";
my $label;

sub get_cmd_from_tv {
	my $tv = shift;
	my $selection = $tv->get_selection;
	return "" if not $selection;

	my ($model, $iter) = $selection->get_selected();

	# Get reference to our part of %fst hash
	my $arch = $model->get($iter, 1);
	my $path = $model->get($iter, 2);

	return "env FSTHOST_GUI=$FSTHOST_GUI fsthost$arch \"$path\" >/dev/null 2>&1 &";
}

sub start_fsthost { 
	my $tv = shift;

	my $cmd = get_cmd_from_tv($tv);
	if ( not $cmd ) {
		print "Empty cmd ?\n";
		return;
	}
	print "Spawn: $cmd\n";
	system ( $cmd );
}

sub tv_selection_changed {
	my $tv = shift;

	$label->set_text ( get_cmd_from_tv($tv) );
}

sub read_xml_db {
	my $fst = shift;

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
#$window->set_icon_from_file ( 'fsthost.xpm', 0 );
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
$sw->set_size_request (700, 700);
#method of Gtk3::Container
#$sw->set_border_width(5);
$vbox->pack_start($sw, 1, 1, 0);

# TreeView
my $tree_store = Gtk3::TreeStore->new(qw/Glib::String Glib::String Glib::String/);

#fill it with arbitry data
foreach (sort keys %fst) { 	
	#the iter is a pointer in the treestore. We use to add data.
	my $iter = $tree_store->append(undef);
	$tree_store->set ($iter, 0 => $_, 1 => $fst{$_}->{'arch'}, 2 => $fst{$_}->{'path'});
}

#create a renderer that will be used to display info in the model
my $renderer = Gtk3::CellRendererText->new();

#this will create a treeview, specify $tree_store as its model
my $tree_view = Gtk3::TreeView->new($tree_store);

# Connect double-click signal
$tree_view->signal_connect('row-activated' => \&start_fsthost );
$tree_view->signal_connect('cursor-changed' => \&tv_selection_changed );

my @columns_desc = ( 'Plugin', 'Arch' , 'Path' );
foreach (0 .. 2) {
	#create a Gtk3::TreeViewColumn to add to $tree_view
	my $tree_column = Gtk3::TreeViewColumn->new();
	$tree_column->set_title ( $columns_desc[$_] );

	# add renderer to $tree_column. This works like a Gtk3::Hbox
	# so you can add more than one renderer to $tree_column			
	$tree_column->pack_start ($renderer, 0);

	# set the cell "text" attribute to column 0   
	#- retrieve text from that column in treestore 
	# Thus, the "text" attribute's value will depend on the row's value
	# of column 0 in the model($treestore),
	# and this will be displayed by $renderer,
	# which is a text renderer
	$tree_column->add_attribute($renderer, text => $_);

	#add $tree_column to the treeview
	$tree_view->append_column ($tree_column);

	# Allow sorting on the column
	$tree_column->set_sort_column_id($_);
}

# make searchable 
$tree_view->set_search_column(0);

# Allow drag and drop reordering of rows
$tree_view->set_reorderable(1);

# Add treeview to scrolled window
$sw->add($tree_view);

$label = Gtk3::Label->new( '' );
$label->set_selectable(1);
$vbox->pack_start($label, 1, 1, 0);

$window->show_all;
Gtk3->main;

