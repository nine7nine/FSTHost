#!/usr/bin/perl

use strict;
#use Data::Dumper;
use XML::LibXML;

our $gtk_version;

# Load GTK modules
BEGIN {
	our $gtk_version;

#	for my $pkg ( qw/Gtk3 Gtk2/ ) {
	for my $pkg ( qw/Gtk2 Gtk3/ ) {
		last if eval {
			require $pkg.'.pm';
			$pkg->import();
			$gtk_version = $pkg;
		};
	}
	die "Can't find modules Gtk[23]" unless ( $gtk_version );
}

use constant {
	NOGUI => 0, # Disable GUI at all
	GUI_HIDE => 1,
	GUI_NORMAL => 2
};

our $FSTHOST_GUI = GUI_NORMAL; # default
our %FSTHOST_OPTIONS = (
	'p' => '',
	'j' => 'system'
);
our $filename = $ENV{'HOME'} . '/.fsthost.xml';

sub get_cmd_from_tv {
	my $tv = shift;
	my $selection = $tv->get_selection or return "";
	my ($model, $iter) = $selection->get_selected() or return "";

	# Get reference to our part of %fst hash
	my $arch = $model->get ($iter, 1);
	my $path = $model->get ($iter, 2);

	my $opts;
	( $opts .= " -$_ $FSTHOST_OPTIONS{$_}" ) for ( keys %FSTHOST_OPTIONS );

	return "env FSTHOST_GUI=$FSTHOST_GUI fsthost$arch $opts \"$path\" >/dev/null 2>&1 &";
}

sub start_fsthost { 
	my $tv = shift;

	my $cmd = get_cmd_from_tv ($tv);
	if ( not $cmd ) {
		print "Empty cmd ?\n";
		return;
	}
	print "spawn: $cmd\n";
	system ( $cmd );
}

sub tv_selection_changed {
	my ( $tv, $label ) = @_;
	my $txt = get_cmd_from_tv ( $tv );
	$label->set_text ( $txt );
}

sub edit_button_toggle {
	my ( $b, $data ) = @_;
	$FSTHOST_GUI = ( $b->get_active() ) ? GUI_NORMAL : GUI_HIDE;
	tv_selection_changed ( $data->{'tv'}, $data->{'label'} );
}

sub ctp_button_toggle {
	my ( $b, $data ) = @_;
	if ( $b->get_active() ) {
		$FSTHOST_OPTIONS{'p'} = '';
	} else {
		delete $FSTHOST_OPTIONS{'p'};
	}
	tv_selection_changed ( $data->{'tv'}, $data->{'label'} );
}

sub connect_button_toggle {
	my ( $b, $data ) = @_;
	if ( $b->get_active() ) {
		$FSTHOST_OPTIONS{'j'} = 'system';
	} else {
		delete $FSTHOST_OPTIONS{'j'};
	}
	tv_selection_changed ( $data->{'tv'}, $data->{'label'} );
}

sub read_xml_db {
	my $fst = shift;

	my $parser = new XML::LibXML ();
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

my $Gtk = $gtk_version;
$Gtk->init();
my $window = ($Gtk.'::Window')->new( 'toplevel' );
$window->signal_connect('delete_event' => sub { $Gtk->main_quit() } );
$window->set_icon_from_file ( 'fsthost.xpm' ) if ( ($Gtk.'::MAJOR_VERSION') > 2 );
$window->set_title ( 'FSTHost Menu (' . $gtk_version . ')' );
$window->set_border_width(5);

# Vbox
sub gtk_vbox { return ($gtk_version eq 'Gtk3') ? new Gtk3::Box ('vertical', 0) : new Gtk2::VBox (); }
my $vbox = gtk_vbox();
$vbox->set_border_width ( 2 );
$window->add ( $vbox );

# Toolbar
my $toolbar = ($Gtk.'::Toolbar')->new();
$vbox->pack_start ( $toolbar, 0, 0, 0 ); # child, expand, fill, padding

# Label for command
my $label = ($Gtk.'::Label')->new( '' );
$label->set_selectable(1);
$vbox->pack_start ( $label, 0, 0, 0 ); # child, expand, fill, padding

# Scrolled window
#create a scrolled window that will host the treeview
my $sw = ($Gtk.'::ScrolledWindow')->new(undef,undef);
$sw->set_shadow_type ('etched-out');
$sw->set_policy ('never', 'automatic');
#This is a method of the Gtk3::Widget class,it will force a minimum 
#size on the widget. Handy to give intitial size to a 
#Gtk3::ScrolledWindow class object
$sw->set_size_request (600, 300);
$vbox->pack_start ( $sw, 1, 1, 0 ); # child, expand, fill, padding

# TreeView
my $tree_store = ($Gtk.'::TreeStore')->new(qw/Glib::String Glib::String Glib::String/);

#fill it with arbitry data
foreach (sort keys %fst) { 	
	#the iter is a pointer in the treestore. We use to add data.
	my $iter = $tree_store->append(undef);
	$tree_store->set ($iter, 0 => $_, 1 => $fst{$_}->{'arch'}, 2 => $fst{$_}->{'path'});
}

#create a renderer that will be used to display info in the model
my $renderer = ($Gtk.'::CellRendererText')->new();

#this will create a treeview, specify $tree_store as its model
my $tree_view = ($Gtk.'::TreeView')->new($tree_store);

# Edit Button
my $edit_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-edit');
$edit_button->set_active(1);
$edit_button->set_tooltip_text ( 'Toggle GUI type NORMAL/HIDDEN' );
$edit_button->signal_connect ( 'toggled' => \&edit_button_toggle, { label => $label, tv => $tree_view } );
$toolbar->insert ( $edit_button, 0 );

# Connect to physical button
my $ctp_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-connect');
$ctp_button->set_active(1);
$ctp_button->set_tooltip_text ( 'Connect to physical MIDI ports' );
$ctp_button->signal_connect ( 'toggled' => \&ctp_button_toggle, { label => $label, tv => $tree_view } );
$toolbar->insert ( $ctp_button, 0 );

# Connect button
my $connect_button = ($Gtk.'::ToggleToolButton')->new_from_stock('gtk-execute');
$connect_button->set_active(1);
$connect_button->set_tooltip_text ( 'Connect to "system" ports' );
$connect_button->signal_connect ( 'toggled' => \&connect_button_toggle, { label => $label, tv => $tree_view } );
$toolbar->insert ( $connect_button, 0 );

# Connect double-click signal
$tree_view->signal_connect ('row-activated' => \&start_fsthost );
$tree_view->signal_connect ('cursor-changed' => \&tv_selection_changed, $label );

my @columns_desc = ( 'Plugin', 'Arch' , 'Path' );
foreach (0 .. 2) {
	#create a Gtk3::TreeViewColumn to add to $tree_view
	my $tree_column = ($Gtk.'::TreeViewColumn')->new();
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
$tree_view->set_search_column (0);

# Allow drag and drop reordering of rows
$tree_view->set_reorderable (1);

# Add treeview to scrolled window
$sw->add ($tree_view);

$window->show_all();
$Gtk->main();
