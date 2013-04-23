#include "jackvst.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkevents.h>
#include <X11/Xlib.h>
#include <sys/syscall.h>
#include "fsthost.xpm"

#ifdef HAVE_LASH
extern void jvst_lash_idle(JackVST *jvst, bool *quit);
#endif

/* from cpuusage.c */
extern void CPUusage_init();
extern double CPUusage_getCurrentValue();

static short mode_cc = 0;
static bool quit = FALSE;
static bool have_fwin = FALSE;

static	GtkWidget* window;
static	GtkWidget* gtk_socket;
static	GtkWidget* socket_align;
static	GtkWidget* fvpacker;
static	GtkWidget* vpacker;
static	GtkWidget* hpacker;
static	GtkWidget* bypass_button;
static	GtkWidget* editor_button;
static	GtkWidget* editor_checkbox;
static	GtkWidget* channel_listbox;
static  GtkWidget* preset_listbox;
static	GtkWidget* midi_learn_toggle;
static	GtkWidget* midi_pc;
static	GtkWidget* midi_filter;
static	GtkWidget* load_button;
static	GtkWidget* save_button;
static	GtkWidget* sysex_button;
static	GtkWidget* volume_slider;
static	GtkWidget* cpu_usage;
static	gulong preset_listbox_signal;
static	gulong volume_signal;
static	gulong gtk_socket_signal;

typedef int (*error_handler_t)( Display *, XErrorEvent *);
static Display *the_gtk_display;
error_handler_t wine_error_handler;
error_handler_t gtk_error_handler;

struct RemoveFilterData {
	MIDIFILTER** filters;
	MIDIFILTER* toRemove;
	GtkWidget* hpacker;
};

static void
learn_handler (GtkToggleButton *but, gpointer ptr)
{
	JackVST* jvst = (JackVST*) ptr;
	
	if ( ! gtk_toggle_button_get_active (but) ) {
		jvst->midi_learn = FALSE;
		return;
	}

	jvst->midi_learn = TRUE;
	jvst->midi_learn_CC = -1;
	jvst->midi_learn_PARAM = -1;
}

static void
bypass_handler (GtkToggleButton *but, gpointer ptr)
{
	JackVST* jvst = (JackVST*) ptr;

	jvst_bypass(jvst, gtk_toggle_button_get_active(but));
}

static void
volume_handler (GtkVScale *slider, gpointer ptr)
{
	JackVST* jvst = (JackVST*) ptr;

	short volume = gtk_range_get_value(GTK_RANGE(slider));
	jvst_set_volume(jvst, volume);
}

static void
sysex_handler (GtkToggleButton *but, gpointer ptr) {
	JackVST* jvst = (JackVST*) ptr;
	jvst_send_sysex(jvst, SYSEX_WANT_DUMP);
}

static void
midi_pc_handler (GtkToggleButton *but, gpointer ptr) {
	JackVST* jvst = (JackVST*) ptr;
	jvst->midi_pc = 
		(gtk_toggle_button_get_active (but)) ? MIDI_PC_SELF : MIDI_PC_PLUG;
}

static void
save_handler (GtkToggleButton *but, gpointer ptr) {
	JackVST* jvst = (JackVST*) ptr;

	GtkWidget *dialog;
	dialog = gtk_file_chooser_dialog_new ("Save Plugin State",
			GTK_WINDOW (window),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
			NULL);

	GtkFileFilter * f1 = gtk_file_filter_new();
	GtkFileFilter * f2 = gtk_file_filter_new();
	GtkFileFilter * f3 = gtk_file_filter_new();
	gtk_file_filter_set_name(f1,"FST Plugin State");
	gtk_file_filter_add_pattern(f1,"*.fps");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),f1);

	gtk_file_filter_set_name(f2,"FXB Bank");
	gtk_file_filter_add_pattern(f2,"*.fxb");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),f2);

	gtk_file_filter_set_name(f3,"FXP Preset");
	gtk_file_filter_add_pattern(f3,"*.fxp");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),f3);

	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER(dialog), TRUE);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		char *selected;
		char *last4;
		const gchar *fa_name;

		selected = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(dialog));

		filename = alloca (strlen (selected) + 5);
		strcpy (filename, selected);

		last4 = selected + strlen(selected) - 4;
		fa_name = gtk_file_filter_get_name( gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(dialog)) );

		// F1 Filter - FPS
		if ( strcmp(gtk_file_filter_get_name(f1), fa_name) == 0) {
			if (strcasecmp (".fps", last4) != 0)
				strcat (filename, ".fps");
		// F2 filter - FXB
		} else if ( strcmp(gtk_file_filter_get_name(f2), fa_name) == 0) {
			if (strcasecmp (".fxb", last4) != 0)
				strcat (filename, ".fxb");
		// F3 Filter - FXP
		} else if ( strcmp(gtk_file_filter_get_name(f3), fa_name) == 0) {
			if (strcasecmp (".fxp", last4) != 0)
				strcat (filename, ".fxp");
		}

		if (! jvst_save_state (jvst, filename)) {
			GtkWidget * errdialog = gtk_message_dialog_new (GTK_WINDOW (window),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					"Error saving file '%s'",
					filename);
			gtk_dialog_run (GTK_DIALOG (errdialog));
			gtk_widget_destroy (errdialog);
		}

		g_free (selected);
	}
	gtk_widget_destroy (dialog);
}

static int
create_preset_store( GtkListStore* store, FST *fst ) {
	short i;
	char progName[24];

	for( i = 0; i < fst->plugin->numPrograms; i++ ) {
		GtkTreeIter new_row_iter;

		if ( fst->vst_version >= 2 ) {
			fst_get_program_name(fst, i, progName, sizeof(progName));
		} else {
			/* FIXME:
			So what ? nasty plugin want that we iterate around all presets ?
			no way - we don't have time for this
			*/
			sprintf ( progName, "preset %d", i );
		}

		gtk_list_store_insert( store, &new_row_iter, i );
		gtk_list_store_set( store, &new_row_iter, 0, progName, 1, i, -1 );
	}

	return 1;
}


static void
load_handler (GtkToggleButton *but, gpointer ptr) {
	JackVST* jvst = (JackVST*) ptr;

	GtkWidget *dialog;
	dialog = gtk_file_chooser_dialog_new ("Load Plugin State",
			GTK_WINDOW (window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
			NULL);

	// All supported
	GtkFileFilter * ff = gtk_file_filter_new();
	gtk_file_filter_set_name(ff,"All supported files");
	gtk_file_filter_add_pattern(ff,"*.fps");
	gtk_file_filter_add_pattern(ff,"*.fxb");
	gtk_file_filter_add_pattern(ff,"*.fxp");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),ff);

	// FST internal format
	ff = gtk_file_filter_new();
	gtk_file_filter_set_name(ff,"FST Plugin State");
	gtk_file_filter_add_pattern(ff,"*.fps");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),ff);

	// FX Files
	ff = gtk_file_filter_new();
	gtk_file_filter_set_name(ff,"FX Files (fxb/fxp)");
	gtk_file_filter_add_pattern(ff,"*.fxb");
	gtk_file_filter_add_pattern(ff,"*.fxp");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog),ff);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

		if (! jvst_load_state (jvst, filename)) {
			GtkWidget * errdialog = gtk_message_dialog_new (GTK_WINDOW (window),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					"Error loading file '%s'",
					filename);
			gtk_dialog_run (GTK_DIALOG (errdialog));
			gtk_widget_destroy (errdialog);
		}
		g_free (filename);
	}
	gtk_widget_destroy (dialog);

	// update preset combo
	g_signal_handler_block (preset_listbox, preset_listbox_signal);
	GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(preset_listbox)));
	gtk_list_store_clear(store);
	create_preset_store(store, jvst->fst );
        g_signal_handler_unblock (preset_listbox, preset_listbox_signal);

	// Update MIDI PC button
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( midi_pc ), (jvst->midi_pc > MIDI_PC_PLUG) );
}

static gboolean
configure_handler (GtkWidget* widget, GdkEventConfigure* ev, GtkSocket *sock) {
	XEvent event;
	gint x, y;
	GdkWindow *w;

	if (sock->plug_window != NULL) return FALSE;

	w = sock->plug_window;
	event.xconfigure.type = ConfigureNotify;

	event.xconfigure.event = GDK_WINDOW_XWINDOW (w);
	event.xconfigure.window = GDK_WINDOW_XWINDOW (w);

	/* The ICCCM says that synthetic events should have root relative
	 * coordinates. We still aren't really ICCCM compliant, since
	 * we don't send events when the real toplevel is moved.
	 */
	gdk_error_trap_push ();
	gdk_window_get_origin (w, &x, &y);
	gdk_error_trap_pop ();

	event.xconfigure.x = x;
	event.xconfigure.y = y;
	event.xconfigure.width = GTK_WIDGET(sock)->allocation.width;
	event.xconfigure.height = GTK_WIDGET(sock)->allocation.height;

	event.xconfigure.border_width = 0;
	event.xconfigure.above = None;
	event.xconfigure.override_redirect = False;

	gdk_error_trap_push ();
	XSendEvent (gdk_x11_drawable_get_xdisplay (w),
		    GDK_WINDOW_XWINDOW (sock->plug_window),
		    False, StructureNotifyMask, &event);
	//gdk_display_sync (gtk_widget_get_display (GTK_WIDGET (sock)));
	gdk_error_trap_pop ();

	return FALSE;
}

static void
editor_handler (GtkToggleButton *but, gpointer ptr) {
	JackVST* jvst = (JackVST*) ptr;

	if (gtk_toggle_button_get_active (but)) {
		bool popup = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor_checkbox));
		if (! fst_run_editor(jvst->fst, popup)) return;
		if (! popup) return;
		
		// Create GTK Socket (Widget)
		gtk_socket = gtk_socket_new ();
		GTK_WIDGET_SET_FLAGS(gtk_socket, GTK_CAN_FOCUS);

		// Add Widget socket to vBox
		socket_align = gtk_alignment_new(0.5, 0, 0, 0);
		gtk_container_add(GTK_CONTAINER(socket_align), gtk_socket);
		gtk_box_pack_start (GTK_BOX(vpacker), socket_align, TRUE, FALSE, 0);

		gtk_widget_set_size_request(gtk_socket, jvst->fst->width, jvst->fst->height);
		gtk_socket_add_id (GTK_SOCKET (gtk_socket), GDK_GPOINTER_TO_NATIVE_WINDOW (jvst->fst->xid) );
		gtk_socket_signal = g_signal_connect (G_OBJECT(window), "configure-event",
			G_CALLBACK(configure_handler), gtk_socket);

		fst_show_editor(jvst->fst);
		gtk_widget_show(socket_align);
		gtk_widget_show(gtk_socket);
	} else if (! jvst->fst->editor_popup) {
		fst_destroy_editor(jvst->fst);
	} else {
		g_signal_handler_disconnect(G_OBJECT(window), gtk_socket_signal);
		gtk_widget_hide(gtk_socket);
		fst_destroy_editor(jvst->fst);
		gtk_widget_set_size_request(gtk_socket, -1, -1);
		gtk_widget_destroy(gtk_socket);
		gtk_widget_destroy(socket_align);
		gtk_window_resize(GTK_WINDOW(window), 1, 1);
	}
}

static GtkListStore* create_channel_store() {
	GtkListStore *retval = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	unsigned short i;
	char buf[10];
	
	for( i=0; i <= 17; i++ ) {
		GtkTreeIter new_row_iter;

		if (i == 0) {
			strcpy( buf, "Omni");
		} else if (i == 17) {
			strcpy( buf, "None");
		} else {
			sprintf( buf, "Ch %d", i);
		}

		gtk_list_store_insert( retval, &new_row_iter, i );
		gtk_list_store_set( retval, &new_row_iter, 0, buf, 1, i, -1 );
	}

	return retval;
}

void store_add(GtkListStore* store, char* text, int value) {
	gtk_list_store_insert_with_values(store, NULL, -1, 0, text, 1, value, -1 );
}

GtkListStore* mf_rule_store() {
	GtkListStore *store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	store_add(store, MF_STR_CHANNEL_REDIRECT, CHANNEL_REDIRECT);
	store_add(store, MF_STR_DROP_ALL, DROP_ALL);
	store_add(store, MF_STR_ACCEPT, ACCEPT);
	return store;
}

GtkListStore* mf_type_store() {
	GtkListStore *store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	store_add(store, MF_STR_ALL, MM_ALL);
	store_add(store, MF_STR_NOTE_OFF, MM_NOTE_OFF);
	store_add(store, MF_STR_NOTE_ON, MM_NOTE_ON);
	store_add(store, MF_STR_AFTERTOUCH, MM_AFTERTOUCH);
	store_add(store, MF_STR_CONTROL_CHANGE, MM_CONTROL_CHANGE);
	store_add(store, MF_STR_PROGRAM_CHANGE, MM_PROGRAM_CHANGE);
	store_add(store, MF_STR_CHANNEL_PRESSURE, MM_CHANNEL_PRESSURE);
	store_add(store, MF_STR_PITCH_BEND, MM_PITCH_BEND);
	return store;
}

static void
combo_changed_handler(GtkCombo* combo, gpointer ptr) {
	uint8_t* value = (uint8_t*) ptr;
	*value = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
}

GtkWidget* add_combo_nosig(GtkWidget* hpacker, GtkListStore* store, int active, const char* tooltip) {
	GtkWidget* combo = gtk_combo_box_new_with_model ( GTK_TREE_MODEL(store) );
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), renderer, "text", 0, NULL);
	gtk_widget_set_tooltip_text( combo, tooltip);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
	gtk_box_pack_start(GTK_BOX(hpacker), GTK_WIDGET(combo), FALSE, FALSE, 0);

	return combo;
}

GtkWidget* add_combo(GtkWidget* hpacker, GtkListStore* store, uint8_t* value, const char* tooltip) {
	GtkWidget* combo = add_combo_nosig(hpacker, store, *value, tooltip);
	g_signal_connect( G_OBJECT(combo), "changed", G_CALLBACK(combo_changed_handler), value ); 

	return combo;
}

static void
entry_changed_handler(GtkEntry* entry, gpointer ptr) {
	uint8_t* value = (uint8_t*) ptr;
	*value = (uint8_t) strtol(gtk_entry_get_text(entry), NULL, 10);
}

GtkWidget* add_entry(GtkWidget* hpacker, uint8_t* value, int len, const char* tooltip) {
	char buf[4];
	snprintf(buf, sizeof buf, "%d", *value);

	GtkWidget *entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hpacker), entry, FALSE, FALSE, 0);
	gtk_entry_set_text(GTK_ENTRY(entry), buf);
	gtk_widget_set_tooltip_text( entry, tooltip);
	gtk_entry_set_max_length(GTK_ENTRY(entry), len);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), len);
	g_signal_connect( G_OBJECT(entry), "changed", G_CALLBACK(entry_changed_handler), value);
	return entry;
}

static void
filter_remove_handler(GtkButton* button, gpointer ptr) {
	struct RemoveFilterData* rbd = (struct RemoveFilterData*) ptr;

	midi_filter_remove ( rbd->filters, rbd->toRemove );
	gtk_container_remove(GTK_CONTAINER(fvpacker), rbd->hpacker);
	GtkWidget* mywin = gtk_widget_get_toplevel (fvpacker);
	gtk_window_resize(GTK_WINDOW(mywin), 400, 1);
}

static void
filter_enable_handler(GtkButton* button, gpointer ptr) {
	uint8_t* enable = (uint8_t*) ptr;
	*enable = (uint8_t) ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) ? 1 : 0 );
}

void filter_addrow(GtkWidget* vpacker, MIDIFILTER **filters, MIDIFILTER *filter) {
	GtkWidget* hpacker = gtk_hbox_new (FALSE, 7);
	gtk_box_pack_start(GTK_BOX(vpacker), hpacker, FALSE, FALSE, 0);

	GtkWidget* checkbox_enable = gtk_check_button_new();
	gtk_widget_set_tooltip_text(checkbox_enable, "Enable");
	if (filter->enabled) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_enable), TRUE);
	g_signal_connect( G_OBJECT(checkbox_enable), "clicked", G_CALLBACK(filter_enable_handler), &filter->enabled);
	gtk_box_pack_start(GTK_BOX(hpacker), checkbox_enable, FALSE, FALSE, 0);

	GtkWidget* combo_type = add_combo(hpacker, mf_type_store(), (uint8_t*) &filter->type, "Filter Type");
	GtkWidget* combo_channel = add_combo(hpacker, create_channel_store(), &filter->channel, "MIDI Channel");
//	GtkWidget* entry_value1 = add_entry(hpacker, &filter->value1, 3, "Value 1");
//	GtkWidget* entry_value2 = add_entry(hpacker, &filter->value2, 3, "Value 2");
	GtkWidget* combo_rule = add_combo(hpacker, mf_rule_store(), (uint8_t*) &filter->rule, "Filter Rule");
	GtkWidget* entry_rvalue = add_entry(hpacker, &filter->rvalue, 3, "Rule Value");

	/* Compiler remove this lines - but this suppress warnings ;-) */
	combo_type = combo_type;
	combo_channel = combo_channel;
	combo_rule = combo_rule;
	entry_rvalue = entry_rvalue;
	/***************************************************************/

	GtkWidget* button_remove = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	struct RemoveFilterData* rbd = malloc( sizeof (struct RemoveFilterData) );
	rbd->filters = filters;
	rbd->toRemove = filter;
	rbd->hpacker = hpacker;
	if (filter->built_in) gtk_widget_set_sensitive (hpacker, FALSE);
	g_signal_connect_data ( G_OBJECT(button_remove), "clicked",
		G_CALLBACK(filter_remove_handler), rbd, (GClosureNotify) free, 0);
	gtk_box_pack_start(GTK_BOX(hpacker), button_remove, FALSE, FALSE, 0);
}

static void
filter_new_handler( GtkToggleButton *but, gpointer ptr ) {
	JackVST* jvst = (JackVST*) ptr;
	MIDIFILTER mf = {0};
	MIDIFILTER* nmf = midi_filter_add( &jvst->filters, &mf );
	filter_addrow(fvpacker, &jvst->filters, nmf);
 	gtk_widget_show_all (fvpacker);
}

static gboolean
fwin_destroy_handler (GtkWidget* widget, GdkEventAny* ev, gpointer ptr) {
	bool* hf = (bool*) ptr;
	*hf = FALSE;
	return FALSE;
}

static void
midifilter_handler (GtkWidget* widget, JackVST *jvst) {
	if (have_fwin) return;
	have_fwin = TRUE;

	GtkWidget* fwin = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (fwin), 400, -1);
//	gtk_widget_set_size_request (fwin, 400, -1);
	gtk_window_set_icon(GTK_WINDOW(fwin), gdk_pixbuf_new_from_xpm_data((const char**) fsthost_xpm));
	g_signal_connect (G_OBJECT(fwin), "delete_event", G_CALLBACK(fwin_destroy_handler), &have_fwin);
	GtkWidget* ftoolbar = gtk_toolbar_new();

	fvpacker = gtk_vbox_new (FALSE, 7);

	gtk_container_add (GTK_CONTAINER (fwin), fvpacker);

	GtkToolItem* button_new = gtk_tool_button_new_from_stock(GTK_STOCK_ADD);
	gtk_toolbar_insert(GTK_TOOLBAR(ftoolbar), button_new, 0);
	gtk_box_pack_start(GTK_BOX(fvpacker), ftoolbar, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(GTK_WIDGET(button_new), "New Filter");
	g_signal_connect (G_OBJECT(button_new),  "clicked", G_CALLBACK(filter_new_handler), jvst);

	MIDIFILTER *f;
	for (f = jvst->filters; f; f = f->next) filter_addrow(fvpacker, &jvst->filters, f);

	gtk_widget_show_all (fwin);
}

static gboolean
destroy_handler (GtkWidget* widget, GdkEventAny* ev, gpointer ptr) {
	JackVST* jvst = (JackVST*) ptr;

	printf("GTK destroy_handler\n");
//	quit = TRUE;

	fst_destroy_editor(jvst->fst);

	gtk_main_quit();
	
	return FALSE;
}

static void
program_change (GtkComboBox *combo, JackVST *jvst) {
	short program = gtk_combo_box_get_active (combo);
	fst_program_change(jvst->fst, program);
}

static void
channel_check(GtkComboBox *combo, JackVST *jvst) {
	uint8_t channel = midi_filter_one_channel_get ( &jvst->channel );
	if ( channel == gtk_combo_box_get_active(combo) ) return;
	gtk_combo_box_set_active(combo, channel);
}

static void
channel_change (GtkComboBox *combo, JackVST *jvst) {
	short channel = gtk_combo_box_get_active (combo);
	midi_filter_one_channel_set( &jvst->channel, channel );
}

static gboolean
idle_cb(JackVST *jvst) {
	FST* fst = (FST*) jvst->fst;
	if (quit) {
		gtk_main_quit();
		return FALSE;
	}

	// If program was changed via plugin or MIDI
	if( fst->want_program == -1 &&
	    gtk_combo_box_get_active( GTK_COMBO_BOX( preset_listbox ) ) != fst->current_program )
	{
		g_signal_handler_block (preset_listbox, preset_listbox_signal);
		gtk_combo_box_set_active( GTK_COMBO_BOX( preset_listbox ), fst->current_program );
        	g_signal_handler_unblock (preset_listbox, preset_listbox_signal);
	}

	// MIDI learn support
	if( jvst->midi_learn && jvst->midi_learn_CC != -1 && jvst->midi_learn_PARAM != -1 ) {
		if( jvst->midi_learn_CC < 128 ) {
			jvst->midi_map[jvst->midi_learn_CC] = jvst->midi_learn_PARAM;
			char name[32];
			gboolean success;
			success = fst->plugin->dispatcher( fst->plugin, effGetParamName, jvst->midi_learn_PARAM, 0, name, 0 );
			if (success) {
				printf("MIDIMAP CC: %d => %s\n", jvst->midi_learn_CC, name);
			} else {
				printf("MIDIMAP CC: %d => %d\n", jvst->midi_learn_CC, jvst->midi_learn_PARAM);
			}

			short cc;
			int paramIndex;
			gboolean show_tooltip = FALSE;
			char paramName[64];
			char tString[96];
			char tooltip[96 * 128];
			tooltip[0] = 0;
		   	for (cc = 0; cc < 128; cc++) {
				paramIndex = jvst->midi_map[cc];
				if ( paramIndex < 0 || paramIndex >= fst->plugin->numParams )
					continue;

				fst->plugin->dispatcher(fst->plugin, effGetParamName, paramIndex, 0, paramName, 0 );
				if (show_tooltip) {
					snprintf(tString, sizeof tString, "\nCC %03d => %s",cc, paramName);
				} else {
					snprintf(tString, sizeof tString, "CC %03d => %s",cc, paramName);
					show_tooltip = TRUE;
				}
				strcat(tooltip, tString);
			}

			if (show_tooltip)
				gtk_widget_set_tooltip_text(midi_learn_toggle, tooltip);
		}
		jvst->midi_learn = FALSE;
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( midi_learn_toggle ), FALSE );
	}

	// If volume was changed by MIDI CC7 message
	if (jvst->volume != -1) {
		g_signal_handler_block(volume_slider, volume_signal);
		gtk_range_set_value(GTK_RANGE(volume_slider), jvst_get_volume(jvst));
		g_signal_handler_unblock(volume_slider, volume_signal);
	}

	// Channel combo
	channel_check(GTK_COMBO_BOX(channel_listbox), jvst);

	// CPU Usage
	gchar tmpstr[24];
	sprintf(tmpstr, "%06.2f", CPUusage_getCurrentValue());
	gtk_label_set_text(GTK_LABEL(cpu_usage), tmpstr);

	// All about Bypass/Resume
	if (jvst->bypassed != gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(bypass_button)) &&
		jvst->want_state == WANT_STATE_NO
	) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bypass_button), jvst->bypassed);
	}
	if (jvst->want_state_cc != mode_cc) {
		mode_cc = jvst->want_state_cc;
		sprintf(tmpstr, "Bypass (MIDI CC: %d)", mode_cc);
		gtk_widget_set_tooltip_text(bypass_button, tmpstr);
	}

	// Editor button in non-popup mode (not embedded)
	if (! jvst->fst->editor_popup) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor_button), jvst->fst->window ? TRUE : FALSE);
	// If is embedded and want resize window
	} else if (jvst->fst->window && jvst->want_resize) {
		jvst->want_resize = FALSE;
		gtk_widget_set_size_request(gtk_socket, jvst->fst->width-6, jvst->fst->height-24);
	}

#ifdef HAVE_LASH
	jvst_lash_idle(jvst, &quit);
#endif
	return TRUE;
}

// Really ugly auxiliary function for create buttons ;-)
static GtkWidget*
make_img_button(const gchar *stock_id, const gchar *tooltip, bool toggle,
	GCallback handler, JackVST* jvst, bool state, GtkWidget* hpacker)
{
	GtkWidget* button = (toggle) ? gtk_toggle_button_new() : gtk_button_new();
	GtkWidget* image = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_button_set_image(GTK_BUTTON(button), image);

	gtk_widget_set_tooltip_text(button, tooltip);

	g_signal_connect (G_OBJECT(button), (toggle ? "toggled" : "clicked"), handler, jvst); 

	if (state) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), state);
	
	gtk_box_pack_start(GTK_BOX(hpacker), button, FALSE, FALSE, 0);

	return button;
}

bool
gtk_gui_start (JackVST* jvst) {
//	printf("GTK Thread WineID: %d | LWP: %d\n", GetCurrentThreadId (), (int) syscall (SYS_gettid));

	// create a GtkWindow containing a GtkSocket...
	//
	// notice the order of the functions.
	// you can only add an id to an anchored widget.

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW(window), jvst->client_name);
	gtk_window_set_resizable (GTK_WINDOW(window), FALSE);

	gtk_window_set_icon(GTK_WINDOW(window), gdk_pixbuf_new_from_xpm_data((const char**) fsthost_xpm));

	vpacker = gtk_vbox_new (FALSE, 7);
	hpacker = gtk_hbox_new (FALSE, 7);
	bypass_button = make_img_button(GTK_STOCK_STOP, "Bypass", TRUE, G_CALLBACK(bypass_handler),
		jvst, jvst->bypassed, hpacker);

	load_button = make_img_button(GTK_STOCK_OPEN, "Load", FALSE, G_CALLBACK(load_handler),
		jvst, FALSE, hpacker);
	save_button = make_img_button(GTK_STOCK_SAVE_AS, "Save", FALSE, G_CALLBACK(save_handler),
		jvst, FALSE, hpacker);
	//----------------------------------------------------------------------------------
	editor_button = make_img_button(GTK_STOCK_EDIT, "Editor", TRUE, G_CALLBACK(editor_handler),
		jvst, FALSE, hpacker);
	editor_checkbox = gtk_check_button_new();
	gtk_widget_set_tooltip_text(editor_checkbox, "Embedded Editor");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor_checkbox), TRUE);
	gtk_box_pack_start(GTK_BOX(hpacker), editor_checkbox, FALSE, FALSE, 0);
	//----------------------------------------------------------------------------------
	midi_learn_toggle = make_img_button(GTK_STOCK_DND, "MIDI Learn", TRUE, G_CALLBACK(learn_handler),
		jvst, FALSE, hpacker);
	sysex_button = make_img_button(GTK_STOCK_EXECUTE, "Send SysEx", FALSE, G_CALLBACK(sysex_handler),
		jvst, FALSE, hpacker);
	midi_pc = make_img_button(GTK_STOCK_CONVERT, "Self handling MIDI PC", TRUE, G_CALLBACK(midi_pc_handler),
		jvst, (jvst->midi_pc > MIDI_PC_PLUG), hpacker);
	midi_filter = make_img_button(GTK_STOCK_PAGE_SETUP, "MIDI FILTER", FALSE,
		G_CALLBACK(midifilter_handler), jvst, FALSE, hpacker);
	//----------------------------------------------------------------------------------
	if (jvst->volume != -1) {
		volume_slider = gtk_hscale_new_with_range(0,127,1);
		gtk_widget_set_size_request(volume_slider, 100, -1);
		gtk_scale_set_value_pos (GTK_SCALE(volume_slider), GTK_POS_LEFT);
		gtk_range_set_value(GTK_RANGE(volume_slider), jvst_get_volume(jvst));
		volume_signal = g_signal_connect (G_OBJECT(volume_slider), "value_changed", 
			G_CALLBACK(volume_handler), jvst);
		gtk_box_pack_start(GTK_BOX(hpacker), volume_slider, FALSE, FALSE, 0);
		gtk_widget_set_tooltip_text(volume_slider, "Volume");
	}
	//----------------------------------------------------------------------------------
	channel_listbox = add_combo_nosig(hpacker, create_channel_store(), 0, "MIDI Channel");
	channel_check( GTK_COMBO_BOX(channel_listbox), jvst );
	g_signal_connect( G_OBJECT(channel_listbox), "changed", G_CALLBACK(channel_change), jvst ); 
	//----------------------------------------------------------------------------------
	GtkListStore* preset_store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	create_preset_store( preset_store, jvst->fst );
	preset_listbox = add_combo_nosig(hpacker, preset_store, jvst->fst->current_program, "Plugin Presets");
	preset_listbox_signal = g_signal_connect( G_OBJECT(preset_listbox), "changed", 
		G_CALLBACK( program_change ), jvst ); 
	//----------------------------------------------------------------------------------
	cpu_usage = gtk_label_new ("0");
	gtk_box_pack_start(GTK_BOX(hpacker), cpu_usage, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(cpu_usage, "CPU Usage");
	//----------------------------------------------------------------------------------

	gtk_container_set_border_width (GTK_CONTAINER(hpacker), 3); 
	g_signal_connect (G_OBJECT(window), "delete_event", G_CALLBACK(destroy_handler), jvst);
	
	gtk_box_pack_start(GTK_BOX(vpacker), hpacker, FALSE, FALSE, 0);

	gtk_container_add (GTK_CONTAINER (window), vpacker);

 	gtk_widget_show_all (window);

	// Nasty hack - toggle when signal is already connected ;-)
	if (jvst->with_editor == WITH_EDITOR_SHOW)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor_button), TRUE);

	g_timeout_add(500, (GSourceFunc) idle_cb, jvst);
	
	printf( "calling gtk_main now\n" );
	gtk_main ();

	// We exit now
	printf("Jack Deactivate\n");
	jack_deactivate(jvst->client);

	fst_close(jvst->fst);

	return TRUE;
}

int fst_xerror_handler( Display *disp, XErrorEvent *ev ) {
	int error_code = (int) ev->error_code;
	char error_text[256];

	XGetErrorText(disp, error_code, (char *) &error_text, 256);

	if( disp == the_gtk_display ) {
		printf( "Xerror : GTK: %s\n", error_text );
		return gtk_error_handler( disp, ev );
	} else {
		printf( "Xerror:  Wine : %s\n", error_text );
		return wine_error_handler( disp, ev );
	}
}

void gtk_gui_init(int *argc, char **argv[]) {
	wine_error_handler = XSetErrorHandler( NULL );
	gtk_init (argc, argv);
	the_gtk_display = gdk_x11_display_get_xdisplay( gdk_display_get_default() );
	gtk_error_handler = XSetErrorHandler( fst_xerror_handler );
	CPUusage_init();
}

void gtk_gui_quit() { gtk_main_quit(); }
