#include <gtk/gtk.h>
#include <strings.h>

#if (GTK_MAJOR_VERSION < 3)
#include <gdk/gdkx.h>
#include <gdk/gdkevents.h>
#else
#include <gtk/gtkx.h>
#include <gdk/gdk.h>
#endif

#include <X11/Xlib.h>
#include <sys/syscall.h>
#include "fsthost.xpm"

#include "gjfst.h"

/* cpuusage.c */
extern void CPUusage_init();
extern double CPUusage_getCurrentValue();

#if (GTK_MAJOR_VERSION < 3)
/* FIXME: workaround code - will be removed */
#define gtk_box_new(orientation, spacing) ( (orientation == GTK_ORIENTATION_HORIZONTAL) ? gtk_hbox_new (FALSE, spacing) : \
	(orientation == GTK_ORIENTATION_VERTICAL) ? gtk_vbox_new (FALSE, spacing) : NULL )
#define gtk_scale_new_with_range(chuj, ...) gtk_hscale_new_with_range( __VA_ARGS__ )
#define GDK_POINTER_TO_XID GDK_GPOINTER_TO_NATIVE_WINDOW
#endif /* (GTK_MAJOR_VERSION < 3) */

/* FIXME: Temporary fix for Slackware 14.1 which using glibc v2.17 */
#if ! GLIB_CHECK_VERSION(2,40,0)
#warning "Using glibc < 2.40"
#define g_info printf
#endif

static short mode_cc = 0;
static bool no_cpu_usage = false;

static	GtkWidget* window;
static	GtkWidget* vpacker;

#ifdef EMBEDDED_EDITOR
static	GtkWidget* gtk_socket;
static	GtkWidget* socket_align;
#endif

typedef struct {
	JFST* jfst;
	ChangesLast changes_last;
	GtkWidget* fvpacker;
	GtkWidget* hpacker;
	GtkWidget* bypass_button;
	GtkWidget* editor_button;
#ifdef EMBEDDED_EDITOR
	GtkWidget* editor_checkbox;
#endif
	GtkWidget* channel_listbox;
	GtkWidget* transposition_spin;
	GtkWidget* preset_listbox;
	GtkWidget* midi_learn_toggle;
	GtkWidget* midi_pc;
	GtkWidget* change_pn;
	GtkWidget* midi_filter;
	GtkWidget* load_button;
	GtkWidget* save_button;
	GtkWidget* sysex_button;
	GtkWidget* volume_slider;
	GtkWidget* cpu_usage;
	gulong preset_listbox_signal;
	gulong volume_signal;
	bool have_fwin;
	bool have_cpu_usage;
} GJFST;

typedef int (*error_handler_t)( Display *, XErrorEvent *);
static Display *the_gtk_display;
error_handler_t wine_error_handler;
error_handler_t gtk_error_handler;
bool window_title_by_fst = true; /* Set window title to plugin name ? */

/* ------------------------------- HELPERS ---------------------------------------------- */
static void
combo_changed_handler(GtkComboBox* combo, gpointer ptr) {
	int* value = (int*) ptr;

	GtkTreeIter iter;
	GtkTreeModel *tree = gtk_combo_box_get_model( combo );
	gtk_combo_box_get_active_iter( combo, &iter );
	gtk_tree_model_get( tree, &iter, 1 , value, -1 );

//	*value = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
}

static void combo_set_active( GtkComboBox* combo, int active ) {
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_combo_box_get_model( combo );
	int value;
	gboolean valid;
	for ( valid = gtk_tree_model_get_iter_first (model, &iter);
		valid;
		valid = gtk_tree_model_iter_next (model, &iter)
	) {
		gtk_tree_model_get( model, &iter, 1 , &value, -1 );
		if ( value == active ) {
			gtk_combo_box_set_active_iter( combo, &iter );
			break;
		}
	}
}

GtkWidget* add_combo_nosig(GtkWidget* hpacker, GtkListStore* store, int active, const char* tooltip) {
	GtkWidget* combo = gtk_combo_box_new_with_model ( GTK_TREE_MODEL(store) );
	g_object_unref( G_OBJECT( store ) );
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo), renderer, "text", 0, NULL);
	gtk_widget_set_tooltip_text( combo, tooltip);
	combo_set_active ( GTK_COMBO_BOX ( combo ), active );
	gtk_box_pack_start(GTK_BOX(hpacker), GTK_WIDGET(combo), FALSE, FALSE, 0);

	return combo;
}

GtkWidget* add_combo(GtkWidget* hpacker, GtkListStore* store, int* value, const char* tooltip) {
	GtkWidget* combo = add_combo_nosig(hpacker, store, *value, tooltip);
	g_signal_connect( G_OBJECT(combo), "changed", G_CALLBACK(combo_changed_handler), value ); 

	return combo;
}

static void
entry_changed_handler_uint8 (GtkEntry* entry, gpointer ptr) {
	uint8_t* value = (uint8_t*) ptr;
	*value = (uint8_t) strtol(gtk_entry_get_text(entry), NULL, 10);
}

static void
entry_changed_handler_int8 (GtkEntry* entry, gpointer ptr) {
	int8_t* value = (int8_t*) ptr;
	*value = (int8_t) strtol(gtk_entry_get_text(entry), NULL, 10);
}

enum VTYPE {
	VTYPE_UINT8,
	VTYPE_INT8
};

GtkWidget* add_entry(GtkWidget* hpacker, void* value, enum VTYPE vtype,int len, const char* tooltip) {
	GtkWidget *entry = gtk_entry_new();

	char buf[5];
	if ( vtype == VTYPE_UINT8 ) {
		uint8_t* vp = (uint8_t*) value;
		snprintf(buf, sizeof buf, "%d", *vp);
		g_signal_connect( G_OBJECT(entry), "changed", G_CALLBACK(entry_changed_handler_uint8), value);
	} else { /* VTYPE_INT8 */
		int8_t* vp = (int8_t*) value;
		snprintf(buf, sizeof buf, "%d", *vp);
		g_signal_connect( G_OBJECT(entry), "changed", G_CALLBACK(entry_changed_handler_int8), value);
	}

	gtk_entry_set_text(GTK_ENTRY(entry), buf);
	gtk_widget_set_tooltip_text(entry, tooltip);
	gtk_entry_set_max_length(GTK_ENTRY(entry), len);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), len);
	gtk_box_pack_start(GTK_BOX(hpacker), entry, FALSE, FALSE, 0);

	return entry;
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

/* ------------------------------- MIDI FILTERS ---------------------------------------------- */
struct RemoveFilterData {
	MIDIFILTER** filters;
	MIDIFILTER* toRemove;
	GtkWidget* hpacker;
};

static void store_add(GtkListStore* store, int value) {
	gtk_list_store_insert_with_values(store, NULL, -1, 0, midi_filter_key2name(value), 1, value, -1 );
}

GtkListStore* mf_rule_store() {
	GtkListStore *store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	store_add(store, CHANNEL_REDIRECT);
	store_add(store, TRANSPOSE);
	store_add(store, DROP_ALL);
	store_add(store, ACCEPT);
	return store;
}

GtkListStore* mf_type_store() {
	GtkListStore *store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	store_add(store, MM_ALL);
	store_add(store, MM_NOTE);
	store_add(store, MM_NOTE_OFF);
	store_add(store, MM_NOTE_ON);
	store_add(store, MM_AFTERTOUCH);
	store_add(store, MM_CONTROL_CHANGE);
	store_add(store, MM_PROGRAM_CHANGE);
	store_add(store, MM_CHANNEL_PRESSURE);
	store_add(store, MM_PITCH_BEND);
	return store;
}

static void
filter_remove_handler(GtkButton* button, gpointer ptr) {
	struct RemoveFilterData* rbd = (struct RemoveFilterData*) ptr;

	midi_filter_remove ( rbd->filters, rbd->toRemove );

	GtkWidget* fvpacker = gtk_widget_get_ancestor( rbd->hpacker, GTK_TYPE_BOX );
	gtk_container_remove(GTK_CONTAINER(fvpacker), rbd->hpacker);

	GtkWidget* mywin = gtk_widget_get_toplevel (rbd->hpacker);
	gtk_window_resize( GTK_WINDOW(mywin), 400, 1);
}

static void
filter_enable_handler(GtkButton* button, gpointer ptr) {
	uint8_t* enable = (uint8_t*) ptr;
	*enable = (uint8_t) ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) ? 1 : 0 );
}

static void
filter_addrow(GtkWidget* vpacker, MIDIFILTER **filters, MIDIFILTER *filter) {
	GtkWidget* hpacker = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 7);
	gtk_box_pack_start(GTK_BOX(vpacker), hpacker, FALSE, FALSE, 0);

	GtkWidget* checkbox_enable = gtk_check_button_new();
	gtk_widget_set_tooltip_text(checkbox_enable, "Enable");
	if (filter->enabled) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox_enable), TRUE);
	g_signal_connect( G_OBJECT(checkbox_enable), "clicked", G_CALLBACK(filter_enable_handler), &filter->enabled);
	gtk_box_pack_start(GTK_BOX(hpacker), checkbox_enable, FALSE, FALSE, 0);

	GtkWidget* combo_type = add_combo(hpacker, mf_type_store(), (int*) &filter->type, "Filter Type");
	GtkWidget* combo_channel = add_combo(hpacker, create_channel_store(), (int*) &filter->channel, "MIDI Channel");
//	GtkWidget* entry_value1 = add_entry(hpacker, &filter->value1, VTYPE_UINT8, 3, "Value 1");
//	GtkWidget* entry_value2 = add_entry(hpacker, &filter->value2, VTYPE_UINT8, 3, "Value 2");
	GtkWidget* combo_rule = add_combo(hpacker, mf_rule_store(), (int*) &filter->rule, "Filter Rule");
	GtkWidget* entry_rvalue = add_entry(hpacker, &filter->rvalue, VTYPE_INT8, 3, "Rule Value");

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
	GJFST* gjfst = (GJFST*) ptr;
	JFST* jfst = gjfst->jfst;
	MIDIFILTER mf = {0};
	MIDIFILTER* nmf = midi_filter_add( &jfst->filters, &mf );

	filter_addrow(gjfst->fvpacker, &jfst->filters, nmf);
 	gtk_widget_show_all (gjfst->fvpacker);
}

static gboolean
fwin_destroy_handler (GtkWidget* widget, GdkEventAny* ev, gpointer ptr) {
	bool* hf = (bool*) ptr;
	*hf = FALSE;
	return FALSE;
}

static void
midifilter_handler (GtkWidget* widget, GJFST *gjfst) {
	if (gjfst->have_fwin) return;
	gjfst->have_fwin = TRUE;
	JFST* jfst = gjfst->jfst;

	GtkWidget* fwin = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size (GTK_WINDOW (fwin), 400, -1);
//	gtk_widget_set_size_request (fwin, 400, -1);
	gtk_window_set_icon(GTK_WINDOW(fwin), gdk_pixbuf_new_from_xpm_data((const char**) fsthost_xpm));
	g_signal_connect (G_OBJECT(fwin), "delete_event", G_CALLBACK(fwin_destroy_handler), &gjfst->have_fwin);
	GtkWidget* ftoolbar = gtk_toolbar_new();

	gjfst->fvpacker = gtk_box_new (GTK_ORIENTATION_VERTICAL, 7);

	gtk_container_add (GTK_CONTAINER (fwin), gjfst->fvpacker);

	GtkToolItem* button_new = gtk_tool_button_new_from_stock(GTK_STOCK_ADD);
	gtk_toolbar_insert(GTK_TOOLBAR(ftoolbar), button_new, 0);
	gtk_box_pack_start(GTK_BOX(gjfst->fvpacker), ftoolbar, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(GTK_WIDGET(button_new), "New Filter");
	g_signal_connect (G_OBJECT(button_new),  "clicked", G_CALLBACK(filter_new_handler), gjfst);

	MIDIFILTER *f;
	for (f = jfst->filters; f; f = f->next) filter_addrow(gjfst->fvpacker, &jfst->filters, f);

	gtk_widget_show_all (fwin);
}
/* ------------------------------------------------------------------------------------------- */

#ifdef VUMETER
static	GtkWidget* vumeter;
static	guint vumeter_level = 0;
#define VUMETER_SIZE 50

static void
vumeter_draw_handler (GtkWidget *widget, cairo_t *cr, gpointer user_data) {
	cairo_pattern_t *vumeter;
	int size = vumeter_level * VUMETER_SIZE / 100.0;
	vumeter = cairo_pattern_create_linear (0, 10, 0, 20);
	cairo_pattern_add_color_stop_rgba(vumeter, 0.1, 0, 0, 0, 1);
	cairo_pattern_add_color_stop_rgba(vumeter, 0.5, 0, 1, 0, 1);
	cairo_pattern_add_color_stop_rgba(vumeter, 0.9, 0, 0, 0, 1);

	cairo_rectangle(cr, 0, 5, size, 20);
	cairo_set_source(cr, vumeter);
	cairo_fill(cr);
	cairo_pattern_destroy (vumeter);
}
#endif

static void
gtk_edit_close_handler ( void* arg ) {
	GJFST* gjfst = (GJFST*) arg;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gjfst->editor_button), FALSE );
}

static void
learn_handler (GtkToggleButton *but, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	if ( gtk_toggle_button_get_active (but) ) {
		jfst_midi_learn(gjfst->jfst, true);
	} else {
		jfst_midi_learn(gjfst->jfst, false);
	}
}

static void
bypass_handler (GtkToggleButton *but, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	jfst_bypass(gjfst->jfst, gtk_toggle_button_get_active(but));
}

static void
volume_handler (GtkVScale *slider, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	short volume = gtk_range_get_value(GTK_RANGE(slider));
	jfst_set_volume(gjfst->jfst, volume);
}

static void
sysex_handler (GtkToggleButton *but, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	jfst_send_sysex(gjfst->jfst, SYSEX_TYPE_DUMP);
}

static void
midi_pc_handler (GtkToggleButton *but, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	gjfst->jfst->midi_pc = 
		(gtk_toggle_button_get_active (but)) ? MIDI_PC_SELF : MIDI_PC_PLUG;
}

static void
save_handler (GtkToggleButton *but, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	JFST* jfst = gjfst->jfst;

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

	if ( jfst->default_state_file )
		gtk_file_chooser_set_filename ( GTK_FILE_CHOOSER(dialog), jfst->default_state_file );

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

		if (! jfst_save_state (jfst, filename)) {
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
	char progName[FST_MAX_PROG_NAME];
	int32_t i;

	gtk_list_store_clear(store);
	for( i = 0; i < fst_num_presets(fst); i++ ) {
		GtkTreeIter new_row_iter;

		fst_get_program_name(fst, i, progName, sizeof(progName));

		gtk_list_store_insert( store, &new_row_iter, i );
		gtk_list_store_set( store, &new_row_iter, 0, progName, 1, i, -1 );
	}

	return 1;
}

static void
change_name_handler ( GtkToggleButton *but, gpointer ptr ) {
	GJFST* gjfst = (GJFST*) ptr;
	JFST* jfst = gjfst->jfst;

	GtkWidget* dialog = gtk_dialog_new_with_buttons (
		"Change preset name",
		GTK_WINDOW (window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_REJECT,
		NULL
	);
	if ( ! dialog ) return;

	/* Add entry */
	GtkWidget *entry = gtk_entry_new();
	gtk_entry_set_max_length ( GTK_ENTRY (entry), 23 );
	GtkWidget* content_area = gtk_dialog_get_content_area ( GTK_DIALOG (dialog) );
	gtk_box_pack_start ( GTK_BOX(content_area), entry, TRUE, TRUE, 7 );
	gtk_widget_show(entry);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		const char *text = gtk_entry_get_text ( GTK_ENTRY (entry) );
		fst_set_program_name ( jfst->fst, text );

		// update preset combo
		g_signal_handler_block (gjfst->preset_listbox, gjfst->preset_listbox_signal);
		GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(gjfst->preset_listbox)));
		create_preset_store(store, jfst->fst );
		gtk_combo_box_set_active( GTK_COMBO_BOX( gjfst->preset_listbox ), fst_get_program(jfst->fst) );
        	g_signal_handler_unblock (gjfst->preset_listbox, gjfst->preset_listbox_signal);
	}
	gtk_widget_destroy (entry);
	gtk_widget_destroy (dialog);
}

static void
load_handler (GtkToggleButton *but, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	JFST* jfst = gjfst->jfst;

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

		if (! jfst_load_state (jfst, filename)) {
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
	g_signal_handler_block (gjfst->preset_listbox, gjfst->preset_listbox_signal);
	GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(gjfst->preset_listbox)));
	create_preset_store(store, jfst->fst );
	gtk_combo_box_set_active( GTK_COMBO_BOX( gjfst->preset_listbox ), fst_get_program(jfst->fst) );
        g_signal_handler_unblock (gjfst->preset_listbox, gjfst->preset_listbox_signal);

	// Update MIDI PC button
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( gjfst->midi_pc ), (jfst->midi_pc == MIDI_PC_SELF) );

	// Update transposition spin button
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(gjfst->transposition_spin), midi_filter_transposition_get(jfst->transposition));
}

#ifdef MOVING_WINDOWS_WORKAROUND
/* Workaround for moving problem - some plugins menus were stay where window was opened */
static gboolean
configure_handler (GtkWidget* widget, GdkEventConfigure* ev, JFST* jfst) {
	SetWindowPos(jfst->fst->window, HWND_BOTTOM, 0, 0, 0, 0, SWP_STATECHANGED|SWP_NOREDRAW|SWP_NOSENDCHANGING|
		SWP_ASYNCWINDOWPOS|SWP_NOCOPYBITS|SWP_NOMOVE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_DEFERERASE|SWP_NOSIZE);

	/* TRUE to stop other handlers from being invoked for the event.
	   FALSE to propagate the event further. */
	return FALSE;
}
#endif

static void
editor_handler (GtkToggleButton *but, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;
	JFST* jfst = gjfst->jfst;
	FST* fst = jfst->fst;

	if (gtk_toggle_button_get_active (but)) {
#ifndef EMBEDDED_EDITOR
		fst_run_editor(fst, false); // popup = false
#else
		bool popup = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gjfst->editor_checkbox));
		if (! fst_run_editor(fst, popup)) return;
		if (! popup) return;

		// Create GTK Socket (Widget)
		gtk_socket = gtk_socket_new ();
		gtk_widget_set_can_default(gtk_socket, TRUE);

		// Add Widget socket to vBox
		socket_align = gtk_alignment_new(0.5, 0, 0, 0);
		gtk_container_add(GTK_CONTAINER(socket_align), gtk_socket);
		gtk_box_pack_start (GTK_BOX(vpacker), socket_align, TRUE, FALSE, 0);

		gtk_widget_set_size_request(gtk_socket, fst_width(fst), fst_height(fst));
		gtk_socket_add_id (GTK_SOCKET(gtk_socket), GDK_POINTER_TO_XID(fst_xid(fst)) );

#ifdef MOVING_WINDOWS_WORKAROUND
		g_signal_connect (G_OBJECT(window), "configure-event", G_CALLBACK(configure_handler), jfst);
#endif
		fst_show_editor(fst);
		gtk_widget_show(socket_align);
		gtk_widget_show(gtk_socket);
	} else if ( fst_has_popup_editor(fst) ) {
		// For some reason window was closed before we reach this function
		// That's mean GtkSocket is already destroyed
		if ( fst_has_window(fst) ) {
			gtk_widget_hide(gtk_socket);
			fst_call ( fst, EDITOR_CLOSE );
			gtk_widget_set_size_request(gtk_socket, -1, -1);
			gtk_widget_destroy(gtk_socket);
		}
		gtk_widget_destroy(socket_align);
		gtk_window_resize(GTK_WINDOW(window), 1, 1);
#endif /* EMBEDDED_EDITOR */
	} else {
		fst_call ( fst, EDITOR_CLOSE );
	}
}

static gboolean
destroy_handler (GtkWidget* widget, GdkEventAny* ev, gpointer ptr) {
	GJFST* gjfst = (GJFST*) ptr;

	g_info("GTK destroy_handler");

	fst_call ( gjfst->jfst->fst, EDITOR_CLOSE );

	gtk_main_quit();
	
	return FALSE;
}

static void
program_change (GtkComboBox *combo, FST* fst) {
	short program = gtk_combo_box_get_active (combo);
	fst_set_program( fst, program );
}

static void
transposition_change (GtkSpinButton* spin_button, gpointer ptr) {
	MIDIFILTER* t = (MIDIFILTER*) ptr;
	int8_t value = (int8_t) gtk_spin_button_get_value ( spin_button );
	midi_filter_transposition_set ( t, value );
}

static void
channel_check(GtkComboBox *combo, JFST *jfst) {
	uint8_t channel = midi_filter_one_channel_get ( &jfst->channel );
	if ( channel == gtk_combo_box_get_active(combo) ) return;
	gtk_combo_box_set_active(combo, channel);
}

static void
channel_change (GtkComboBox *combo, JFST *jfst) {
	short channel = gtk_combo_box_get_active (combo);
	midi_filter_one_channel_set( &jfst->channel, channel );
}

static gboolean
idle_cb(GJFST *gjfst) {
	JFST* jfst = gjfst->jfst;
	FST* fst = jfst->fst;
	Changes changes = jfst_detect_changes( jfst, &(gjfst->changes_last) );

	// Changes - program
	if ( changes & CHANGE_PROGRAM ) {
		g_signal_handler_block (gjfst->preset_listbox, gjfst->preset_listbox_signal);
		gtk_combo_box_set_active( GTK_COMBO_BOX( gjfst->preset_listbox ), fst_get_program(fst) );
        	g_signal_handler_unblock (gjfst->preset_listbox, gjfst->preset_listbox_signal);
	}

	// Changes - channel
	if ( changes & CHANGE_CHANNEL )
		channel_check(GTK_COMBO_BOX(gjfst->channel_listbox), jfst);

	// Changes - Bypass/Resume
	if (changes & CHANGE_BYPASS)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gjfst->bypass_button), jfst->bypassed);

	// Changes - MIDI learn
	if ( changes & CHANGE_MIDILE ) {
		MidiLearn* ml = &jfst->midi_learn;
		bool show_tooltip = false;
		char tooltip[96 * 128];
		tooltip[0] = '\0';
		uint8_t cc;
		char ParamName[FST_MAX_PARAM_NAME];
		char tString[96];
	   	for (cc = 0; cc < 128; cc++) {
			int32_t paramIndex = ml->map[cc];
			if ( paramIndex < 0 || paramIndex >= fst_num_params(fst) )
				continue;

			fst_get_param_name(fst, paramIndex, ParamName);

			snprintf(tString, sizeof tString, "CC %03d => %s",cc, ParamName);

			if (show_tooltip) strcat(tooltip, "\n");
			else show_tooltip = true;
		
			strcat(tooltip, tString);
		}

		if (show_tooltip)
			gtk_widget_set_tooltip_text(gjfst->midi_learn_toggle, tooltip);
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( gjfst->midi_learn_toggle ), FALSE );
	}

	// Changes - volume
	if (jfst->volume != -1 && (changes & CHANGE_VOLUME) ) {
		g_signal_handler_block(gjfst->volume_slider, gjfst->volume_signal);
		gtk_range_set_value(GTK_RANGE(gjfst->volume_slider), jfst_get_volume(jfst));
		g_signal_handler_unblock(gjfst->volume_slider, gjfst->volume_signal);
	}

#ifdef VUMETER
	// VU Meter
	vumeter_level = jfst->out_level;
	gtk_widget_queue_draw ( gjfst->vumeter );
#endif

	// CPU Usage
	if ( gjfst->have_cpu_usage ) {
		gchar tmpstr[24];
		sprintf(tmpstr, "%06.2f", CPUusage_getCurrentValue());
		gtk_label_set_text(GTK_LABEL(gjfst->cpu_usage), tmpstr);
	}

	if (jfst->want_state_cc != mode_cc) {
		mode_cc = jfst->want_state_cc;
		gchar tmpstr[24];
		sprintf(tmpstr, "Bypass (MIDI CC: %d)", mode_cc);
		gtk_widget_set_tooltip_text(gjfst->bypass_button, tmpstr);
	}

	return TRUE;
}

#ifdef EMBEDDED_EDITOR
// Editor Window is embedded and want resize window
static void gtk_gui_resize ( JFST* jfst ) {
	FST* fst = jfst->fst;
	if ( fst_has_popup_editor(fst) && fst_has_window(fst) )
		gtk_widget_set_size_request(gtk_socket, fst_width(fst), fst_height(fst));
}
#endif

// Really ugly auxiliary function for create buttons ;-)
static GtkWidget*
make_img_button(const gchar *stock_id, const gchar *tooltip, bool toggle,
	void* handler, GJFST* gjfst, bool state, GtkWidget* hpacker)
{
	GtkWidget* button = (toggle) ? gtk_toggle_button_new() : gtk_button_new();
	GtkWidget* image = gtk_image_new_from_stock(stock_id, GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_button_set_image(GTK_BUTTON(button), image);

	gtk_widget_set_tooltip_text(button, tooltip);

	g_signal_connect (G_OBJECT(button), (toggle ? "toggled" : "clicked"), G_CALLBACK(handler), gjfst); 

	if (state) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), state);
	
	gtk_box_pack_start(GTK_BOX(gjfst->hpacker), button, FALSE, FALSE, 0);

	return button;
}

static GJFST* gjfst_new ( JFST* jfst ) {
	GJFST* gjfst = malloc ( sizeof(GJFST) );
	gjfst->jfst = jfst;
	jfst->user_ptr = gjfst;

	GtkWidget* hpacker = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 7);
	gjfst->hpacker = hpacker;

	gjfst->bypass_button = make_img_button(GTK_STOCK_STOP, "Bypass", TRUE, &bypass_handler,
		gjfst, jfst->bypassed, hpacker);

	gjfst->load_button = make_img_button(GTK_STOCK_OPEN, "Load", FALSE, &load_handler,
		gjfst, FALSE, hpacker);
	gjfst->save_button = make_img_button(GTK_STOCK_SAVE_AS, "Save", FALSE, &save_handler,
		gjfst, FALSE, hpacker);
	//------- EDITOR ------------------------------------------------------------------------------
	gjfst->editor_button = make_img_button(GTK_STOCK_PROPERTIES, "Editor", TRUE, &editor_handler,
		gjfst, FALSE, hpacker);
#ifdef EMBEDDED_EDITOR
	gjfst->editor_checkbox = gtk_check_button_new();
	gtk_widget_set_tooltip_text(gjfst->editor_checkbox, "Embedded Editor");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gjfst->editor_checkbox), FALSE);
	gtk_box_pack_start(GTK_BOX(hpacker), gjfst->editor_checkbox, FALSE, FALSE, 0);
#endif
	fst_set_window_close_callback( jfst->fst, gtk_edit_close_handler, gjfst );
	//------- MIDI LEARN / SYSEX / MIDI SELF PROGRAM CHANGE / MIDI FILTER -------------------------
	gjfst->midi_learn_toggle = make_img_button(GTK_STOCK_DND, "MIDI Learn", TRUE, &learn_handler,
		gjfst, FALSE, hpacker);
	gjfst->sysex_button = make_img_button(GTK_STOCK_EXECUTE, "Send SysEx", FALSE, &sysex_handler,
		gjfst, FALSE, hpacker);
	gjfst->midi_pc = make_img_button(GTK_STOCK_CONVERT, "Self handling MIDI PC", TRUE, &midi_pc_handler,
		gjfst, (jfst->midi_pc > MIDI_PC_PLUG), hpacker);
	gjfst->midi_filter = make_img_button(GTK_STOCK_PAGE_SETUP, "MIDI FILTER", FALSE, &midifilter_handler,
		gjfst, FALSE, hpacker);
	gjfst->have_fwin = FALSE;
	//------- VOLUME CONTROL ----------------------------------------------------------------------
	if (jfst->volume != -1) {
		gjfst->volume_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0,127,1);
		gtk_widget_set_size_request(gjfst->volume_slider, 100, -1);
		gtk_scale_set_value_pos (GTK_SCALE(gjfst->volume_slider), GTK_POS_LEFT);
		gtk_range_set_value(GTK_RANGE(gjfst->volume_slider), jfst_get_volume(jfst));
		gjfst->volume_signal = g_signal_connect (G_OBJECT(gjfst->volume_slider), "value_changed", 
			G_CALLBACK(volume_handler), gjfst);
		gtk_widget_set_tooltip_text(gjfst->volume_slider, "Volume");
		gtk_box_pack_start(GTK_BOX(hpacker), gjfst->volume_slider, FALSE, FALSE, 0);
	}
	//------- MIDI CHANNEL ------------------------------------------------------------------------
	gjfst->channel_listbox = add_combo_nosig(hpacker, create_channel_store(), 0, "MIDI Channel");
	channel_check( GTK_COMBO_BOX(gjfst->channel_listbox), jfst );
	g_signal_connect( G_OBJECT(gjfst->channel_listbox), "changed", G_CALLBACK(channel_change), jfst ); 
	//------- TRANSPOSITION -----------------------------------------------------------------------
	GtkWidget* t = gtk_spin_button_new_with_range (-36, 36, 1);
	gtk_spin_button_set_increments (GTK_SPIN_BUTTON(t), 1, 12);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(t), midi_filter_transposition_get(jfst->transposition));
	gtk_widget_set_tooltip_text(t, "Transposition");
	g_signal_connect( G_OBJECT(t), "value-changed", G_CALLBACK( transposition_change ), jfst->transposition );
	gtk_box_pack_start(GTK_BOX(hpacker), t, FALSE, FALSE, 0);
	gjfst->transposition_spin = t;
	//------- PRESETS -----------------------------------------------------------------------------
	GtkListStore* preset_store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	create_preset_store( preset_store, jfst->fst );
	gjfst->preset_listbox = add_combo_nosig(hpacker, preset_store, fst_get_program(jfst->fst), "Plugin Presets");
	gjfst->preset_listbox_signal = g_signal_connect( G_OBJECT(gjfst->preset_listbox), "changed", 
		G_CALLBACK( program_change ), jfst->fst ); 
	//------- CHANGE PROGRAM NAME ------------------------------------------------------------------
	gjfst->change_pn = make_img_button(GTK_STOCK_EDIT, "Change program name", FALSE, &change_name_handler,
		gjfst, FALSE, hpacker);
	//------- CPU USAGE ----------------------------------------------------------------------------
	if ( no_cpu_usage ) {
		gjfst->have_cpu_usage = false;
	} else {
		no_cpu_usage = true;
		gjfst->have_cpu_usage = true;
		gjfst->cpu_usage = gtk_label_new ("0");
		gtk_box_pack_start(GTK_BOX(hpacker), gjfst->cpu_usage, FALSE, FALSE, 0);
		gtk_widget_set_tooltip_text(gjfst->cpu_usage, "CPU Usage");
	}
	//------- VU METER -----------------------------------------------------------------------------
#ifdef VUMETER
	gjfst->vumeter = gtk_drawing_area_new();
	gtk_widget_set_size_request(gjfst->vumeter, VUMETER_SIZE, 20);
	g_signal_connect(G_OBJECT(gjfst->vumeter), "draw", G_CALLBACK(vumeter_draw_handler), NULL);
	gtk_box_pack_start(GTK_BOX(hpacker), gjfst->vumeter, FALSE, FALSE, 0);
#endif
	//----------------------------------------------------------------------------------------------
	gtk_container_set_border_width (GTK_CONTAINER(hpacker), 3); 
	g_signal_connect (G_OBJECT(window), "delete_event", G_CALLBACK(destroy_handler), gjfst);

#ifdef EMBEDDED_EDITOR
	jfst_set_gui_resize_cb( jfst, gtk_gui_resize );
#endif

	return gjfst;
}

static int gjfst_xerror_handler( Display *disp, XErrorEvent *ev ) {
	int error_code = (int) ev->error_code;
	char error_text[256];

	XGetErrorText(disp, error_code, (char *) &error_text, 256);

	if ( disp == the_gtk_display ) {
		g_info( "Xerror : GTK: %s", error_text );
		return gtk_error_handler( disp, ev );
	} else {
		g_info( "Xerror:  Wine : %s", error_text );
		return wine_error_handler( disp, ev );
	}
}

/* ------------------------------- PUBLIC ---------------------------------------------- */
void gjfst_add (JFST* jfst, bool editor) {
//	g_info("GTK Thread WineID: %d | LWP: %d", GetCurrentThreadId (), (int) syscall (SYS_gettid));

	GJFST* gjfst = gjfst_new ( jfst );

	gtk_box_pack_start(GTK_BOX(vpacker), gjfst->hpacker, FALSE, FALSE, 0);

	// GTK GUI idle
	g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 500, (GSourceFunc) idle_cb, gjfst, NULL);

	// Set main window title
	if ( window_title_by_fst ) {
		gtk_window_set_title (GTK_WINDOW(window), jfst->client_name);
		window_title_by_fst = false; // For second plugin we need more generic name
	} else {
		gtk_window_set_title (GTK_WINDOW(window), "FSTHost");
	}

	// This also emit signal which do the rest ;-)
	if (editor) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gjfst->editor_button), TRUE);
}

void gjfst_init(int *argc, char **argv[]) {
	wine_error_handler = XSetErrorHandler( NULL );
	gtk_init (argc, argv);
	the_gtk_display = gdk_x11_display_get_xdisplay( gdk_display_get_default() );
	gtk_error_handler = XSetErrorHandler( gjfst_xerror_handler );
	CPUusage_init();

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_resizable (GTK_WINDOW(window), FALSE);

	gtk_window_set_icon(GTK_WINDOW(window), gdk_pixbuf_new_from_xpm_data((const char**) fsthost_xpm));

	vpacker = gtk_box_new (GTK_ORIENTATION_VERTICAL, 7);
	gtk_container_add (GTK_CONTAINER (window), vpacker);
}

void gjfst_start() {
	gtk_widget_show_all (window);

	g_info( "calling gtk_main now" );
	gtk_main ();
}

void gjfst_free(JFST* jfst) {
	GJFST* gjfst = (GJFST*) jfst->user_ptr;
	free( gjfst );
}

void gjfst_quit() {
	gtk_main_quit();
}
