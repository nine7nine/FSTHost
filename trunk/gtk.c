#include "jackvst.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkevents.h>
#include <X11/Xlib.h>

#include <glib-2.0/glib.h>

#ifdef HAVE_LASH
#include <lash/lash.h>
extern lash_client_t *lash_client;
#endif


gboolean quit = FALSE;

static	GtkWidget* window;
static	GtkWidget* gtk_socket;
static	GtkWidget* vpacker;
static	GtkWidget* hpacker;
static	GtkWidget* bypass_button;
static	GtkWidget* editor_button;
static	GtkWidget* channel_listbox;
static  GtkWidget* event_box;
static  GtkWidget* preset_listbox;
static	GtkWidget* midi_learn_toggle;
static	GtkWidget* load_button;
static	GtkWidget* save_button;
static	gulong preset_listbox_signal;
static	gulong gtk_socket_signal;


static void
learn_handler (GtkToggleButton *but, gboolean ptr)
{
	JackVST* jvst = (JackVST*) ptr;
	
	if ( ! gtk_toggle_button_get_active (but) ) {
		jvst->fst->midi_learn = 0;
		gtk_widget_grab_focus( gtk_socket );
		return;
	}


	pthread_mutex_lock( &(jvst->fst->lock) );		
	jvst->fst->midi_learn = 1;
	jvst->fst->midi_learn_CC = -1;
	jvst->fst->midi_learn_PARAM = -1;
	pthread_mutex_unlock( &(jvst->fst->lock) );		


	gtk_widget_grab_focus( gtk_socket );
}

static void
bypass_handler (GtkToggleButton *but, gboolean ptr)
{
	JackVST* jvst = (JackVST*) ptr;
	jvst->bypassed = gtk_toggle_button_get_active (but);
	
	if ( jvst->bypassed ) {
		fst_suspend(jvst->fst);
	} else {
		fst_resume(jvst->fst);
	}
	
	gtk_widget_grab_focus( gtk_socket );
}

static void
save_handler (GtkToggleButton *but, gboolean ptr)
{
 	int i, bytelen = 0;
	void *chunk;

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

		filename = malloc (strlen (selected) + 5);
		strcpy (filename, selected);

		last4 = selected + strlen(selected) - 4;
		fa_name = gtk_file_filter_get_name( gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(dialog)) );

		// F1 Filter - FPS
		if ( strcmp(gtk_file_filter_get_name(f1), fa_name) == 0) {
			if (strcmp (".fps", last4) != 0)
				strcat (filename, ".fps");
		// F2 filter - FXB
		} else if ( strcmp(gtk_file_filter_get_name(f2), fa_name) == 0) {
			if (strcmp (".fxb", last4) != 0 && strcmp (".FXB", last4) != 0)
				strcat (filename, ".fxb");
		// F3 Filter - FXP
		} else if ( strcmp(gtk_file_filter_get_name(f3), fa_name) == 0) {
			if (strcmp (".fxp", last4) != 0 && strcmp (".FXP", last4) != 0)
				strcat (filename, ".fxp");
		}

		if (!fst_save_state (jvst->fst, filename)) {
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
		free (filename);
	}
	gtk_widget_destroy (dialog);
	gtk_widget_grab_focus( gtk_socket );
}

static void
load_handler (GtkToggleButton *but, gboolean ptr)
{
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

		if (!fst_load_state (jvst->fst, filename)) {
			GtkWidget * errdialog = gtk_message_dialog_new (GTK_WINDOW (window),
					GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					"Error loading file '%s'",
					filename);
			gtk_dialog_run (GTK_DIALOG (errdialog));
			gtk_widget_destroy (errdialog);
		}

		printf("File %s loaded\n", filename);
		g_free (filename);
	}
	gtk_widget_destroy (dialog);
	gtk_widget_grab_focus( gtk_socket );

	// update preset combo
	g_signal_handler_block (preset_listbox, preset_listbox_signal);
	GtkTreeModel *store = gtk_combo_box_get_model( GTK_COMBO_BOX (preset_listbox ));
	gtk_list_store_clear( GTK_LIST_STORE( store ) );
	create_preset_store( store, jvst->fst );
        g_signal_handler_unblock (preset_listbox, preset_listbox_signal);

	// Set first program
	fst_program_change(jvst->fst, 0);
}

static gboolean
configure_handler (GtkWidget* widget, GdkEventConfigure* ev, GtkSocket *sock)
{
	XEvent event;
	gint x, y;
	GdkWindow *w;

	g_return_if_fail (sock->plug_window != NULL);

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
editor_handler (GtkToggleButton *but, gboolean ptr)
{
	JackVST* jvst = (JackVST*) ptr;

	if (gtk_toggle_button_get_active (but)) {
		// Create GTK Socket (Widget)
		gtk_socket = gtk_socket_new ();
		GTK_WIDGET_SET_FLAGS(gtk_socket, GTK_CAN_FOCUS);

		// Add Widget socket to vBox
		gtk_box_pack_start (GTK_BOX(vpacker), gtk_socket, TRUE, FALSE, 0);

		if (fst_run_editor (jvst->fst)) {
			fst_error ("cannot create editor");
			return;
		}

		gtk_socket_signal = g_signal_connect (G_OBJECT(window), "configure-event",
			G_CALLBACK(configure_handler), gtk_socket);

		gtk_socket_add_id (GTK_SOCKET (gtk_socket), jvst->fst->xid);

		fst_show_editor(jvst->fst);

		gtk_widget_set_size_request(gtk_socket, jvst->fst->width, jvst->fst->height);
		printf("Plugin - Width: %d | Height: %d\n", jvst->fst->width, jvst->fst->height);
				
		gtk_widget_show_now (gtk_socket);
		gtk_widget_grab_focus( gtk_socket );
	} else {
		g_signal_handler_disconnect(G_OBJECT(window), gtk_socket_signal);
		fst_destroy_editor(jvst->fst);
		gtk_widget_set_size_request(gtk_socket, -1, -1);
		gtk_widget_destroy(gtk_socket);
		gtk_window_resize(GTK_WINDOW(window), 1, 1);
	}
}

void
forward_key_event (GtkSocket *sock, GdkEventKey* ev, JackVST* jvst)
{
	XKeyEvent event;
	Status status;

	g_return_if_fail (sock->plug_window != NULL);
	
	event.type = (ev->type == GDK_KEY_PRESS ? KeyPress : KeyRelease);
	event.display = gdk_x11_drawable_get_xdisplay (sock->plug_window);
	event.window = jvst->fst->xid;
	event.time = ev->time;
	event.x = 1;
	event.y = 1;
	event.x_root = 1;
	event.y_root = 1;
	event.state = ev->state;
	event.keycode = ev->hardware_keycode;
	event.same_screen = True;
	
	gdk_error_trap_push ();
	XSendEvent (event.display, event.window, False, 0, (XEvent*) &event);
	gdk_display_sync (gtk_widget_get_display (GTK_WIDGET (sock)));
	gdk_error_trap_pop ();
}

static gboolean
destroy_handler (GtkWidget* widget, GdkEventAny* ev, gpointer ptr)
{
	JackVST* jvst = (JackVST*) ptr;

	printf("GTK destroy_handler\n");
//	quit = TRUE;

	fst_destroy_editor(jvst->fst);

	jack_deactivate( jvst->client );
	fst_close(jvst->fst);
	
	gtk_main_quit();
	
	return FALSE;
}

int
focus_handler (GtkWidget* widget, GdkEventFocus* ev, gpointer ptr)
{
	if (ev->in) {
		fst_error ("Socket focus in");
	} else {
		fst_error ("Socket focus out");
	}
		       
	return FALSE;
}

static void
program_change (GtkComboBox *combo, JackVST *jvst) {
	int program = gtk_combo_box_get_active (combo);
	// cant be done here. plugin only expects one GUI thread.
	fst_program_change(jvst->fst,program);
	
//	gtk_widget_grab_focus( gtk_socket );
}

static void
channel_change (GtkComboBox *combo, JackVST *jvst) {
	int channel = gtk_combo_box_get_active (combo);

	jvst->channel = channel - 1;

        gtk_widget_grab_focus( gtk_socket );
}

#ifdef HAVE_LASH
void
save_data( JackVST *jvst )
{
	int i, bytelen;
	lash_config_t *config;
	void *chunk;

	for( i=0; i<jvst->fst->plugin->numParams; i++ ) {
	    char buf[10];
	    float param;
	    
	    snprintf( buf, 9, "%d", i );

	    config = lash_config_new_with_key( buf );

	    pthread_mutex_lock( &(jvst->fst->lock) );
	    param = jvst->fst->plugin->getParameter( jvst->fst->plugin, i ); 
	    pthread_mutex_unlock( &(jvst->fst->lock) );

	    lash_config_set_value_double(config, param);
	    lash_send_config(lash_client, config);
	    //lash_config_destroy( config );
	}

	for( i=0; i<128; i++ ) {
	    char buf[16];
	    
	    snprintf( buf, 15, "midi_map%d", i );
	    config = lash_config_new_with_key( buf );
	    lash_config_set_value_int(config, jvst->fst->midi_map[i]);
	    lash_send_config(lash_client, config);
	    //lash_config_destroy( config );
	}

	if ( jvst->fst->plugin->flags & effFlagsProgramChunks ) {
	    // TODO: calling from this thread is wrong.
	    //       is should move it to fst gui thread.
	    printf( "getting chunk...\n" );

	    // XXX: alternative. call using the fst->lock
	    //pthread_mutex_lock( &(fst->lock) );
	    //bytelen = jvst->fst->plugin->dispatcher( jvst->fst->plugin, 23, 0, 0, &chunk, 0 );
	    //pthread_mutex_unlock( &(fst->lock) );

	    bytelen = fst_call_dispatcher( jvst->fst, effGetChunk, 0, 0, &chunk, 0 );
	    printf( "got tha chunk..\n" );
	    if( bytelen ) {
		if( bytelen < 0 ) {
		    printf( "Chunke len < 0 !!! Not saving chunk.\n" );
		} else {
		    config = lash_config_new_with_key( "bin_chunk" );
		    lash_config_set_value(config, chunk, bytelen );
		    lash_send_config(lash_client, config);
		    //lash_config_destroy( config );
		}


	    }
	}

}

void
restore_data(lash_config_t * config, JackVST *jvst )
{
	const char *key;

	key = lash_config_get_key(config);

	if (strncmp(key, "midi_map", strlen( "midi_map")) == 0) {
	    int cc = atoi( key+strlen("midi_map") );
	    int param = lash_config_get_value_int( config );

	    if( cc < 0 || cc>=128 || param<0 || param>=jvst->fst->plugin->numParams ) 
		return;

	    jvst->fst->midi_map[cc] = param;
	    return;
	}

	if ( jvst->fst->plugin->flags & effFlagsProgramChunks) {
	    if (strcmp(key, "bin_chunk") == 0) {
		fst_call_dispatcher( jvst->fst, effSetChunk, 0, lash_config_get_value_size( config ), (void *) lash_config_get_value( config ), 0 );
		return;
	    } 
	} else {
	    pthread_mutex_lock( & jvst->fst->lock );
	    jvst->fst->plugin->setParameter( jvst->fst->plugin, atoi( key ), lash_config_get_value_double( config ) );
	    pthread_mutex_unlock( & jvst->fst->lock );
	}

}
#endif

static gboolean
idle_cb(JackVST *jvst)
{
	FST* fst = (FST*) jvst->fst;
	if (quit) {
		jack_deactivate( jvst->client );
		fst_close( fst);
//		gtk_widget_destroy( window );
		gtk_main_quit();
		return FALSE;
	}

	if( fst->want_program == -1 && 
	    gtk_combo_box_get_active( GTK_COMBO_BOX( preset_listbox ) ) != fst->current_program )
	{
		g_signal_handler_block (preset_listbox, preset_listbox_signal);
		gtk_combo_box_set_active( GTK_COMBO_BOX( preset_listbox ), fst->current_program );
        	g_signal_handler_unblock (preset_listbox, preset_listbox_signal);
	}

	if( fst->midi_learn && fst->midi_learn_CC != -1 && fst->midi_learn_PARAM != -1 ) {
		if( fst->midi_learn_CC < 128 ) {
			fst->midi_map[fst->midi_learn_CC] = fst->midi_learn_PARAM;
			char name[32];
			int success;
			success = fst->plugin->dispatcher( fst->plugin, effGetParamName, fst->midi_learn_PARAM, 0, name, 0 );
			if (success) {
				printf("MIDIMAP CC: %d => %s\n", fst->midi_learn_CC, name);
			} else {
				printf("MIDIMAP CC: %d => %d\n", fst->midi_learn_CC, fst->midi_learn_PARAM);
			}

			int cc, paramIndex;
			int show_tooltip = FALSE;
			char paramName[64];
			char tString[96];
			char tooltip[96 * 128];
			tooltip[0] = 0;
		   	for (cc = 0; cc < 128; cc++) {
				paramIndex = fst->midi_map[cc];
				if ( paramIndex < 0 || paramIndex >= fst->plugin->numParams )
					continue;

				fst->plugin->dispatcher(fst->plugin, effGetParamName, paramIndex, 0, paramName, 0 );

				if (show_tooltip) {
					sprintf(tString, "\nCC %03d => %s",cc, paramName);
				} else {
					sprintf(tString, "CC %03d => %s",cc, paramName);
					show_tooltip = TRUE;
				}

				strcat(tooltip, tString);
			}

			if (show_tooltip)
				gtk_widget_set_tooltip_text(midi_learn_toggle, tooltip);

		}
		fst->midi_learn = 0;
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( midi_learn_toggle ), 0 );
	}

#ifdef HAVE_LASH
	if (lash_enabled(lash_client)) {
	    lash_event_t *event;
	    lash_config_t *config;

	    while ((event = lash_get_event(lash_client))) {
		switch (lash_event_get_type(event)) {
		    case LASH_Quit:
			quit = 1;
			lash_event_destroy(event);
			break;
		    case LASH_Restore_Data_Set:
			printf( "lash_restore... \n" );
			lash_send_event(lash_client, event);
			break;
		    case LASH_Save_Data_Set:
			printf( "lash_save... \n" );
			save_data( jvst );
			lash_send_event(lash_client, event);
			break;
		    case LASH_Server_Lost:
			return 1;
		    default:
			printf("%s: receieved unknown LASH event of type %d",
				__FUNCTION__, lash_event_get_type(event));
			lash_event_destroy(event);
			break;
		}
	    }

	    while ((config = lash_get_config(lash_client))) {
		restore_data(config, jvst);
		lash_config_destroy(config);
	    }

	}
#endif
	return TRUE;
}

int create_preset_store( GtkListStore *store, FST *fst )
{
	int i;


	int vst_version = fst->plugin->dispatcher (fst->plugin, effGetVstVersion, 0, 0, NULL, 0.0f);
	for( i = 0; i < fst->plugin->numPrograms; i++ )
	{
		char buf[100];
		GtkTreeIter new_row_iter;

		snprintf( buf, 90, "preset %d", i );
		if( vst_version >= 2 ) 
			fst->plugin->dispatcher( fst->plugin, effGetProgramNameIndexed, i, 0, buf, 0.0 );

		gtk_list_store_insert( store, &new_row_iter, i );
		gtk_list_store_set( store, &new_row_iter, 0, buf, 1, i, -1 );
	}

	return 1;
}
GtkListStore * create_channel_store() {
	GtkListStore *retval = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	int i;
	char buf[100];
	
	for( i=0; i <= 16; i++ ) {
		GtkTreeIter new_row_iter;

		if (i == 0) {
			snprintf( buf, 90, "All");
		} else {
			snprintf( buf, 90, "Ch %d", i);
		}

		gtk_list_store_insert( retval, &new_row_iter, i );
		gtk_list_store_set( retval, &new_row_iter, 0, buf, 1, i, -1 );
	}

	return retval;
}

int
manage_vst_plugin (JackVST* jvst)
{
	printf("GTK ThID: %d\n", GetCurrentThreadId ());

	// create a GtkWindow containing a GtkSocket...
	//
	// notice the order of the functions.
	// you can only add an id to an anchored widget.
	GtkCellRenderer *renderer;
	
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW(window), jvst->handle->name);

	vpacker = gtk_vbox_new (FALSE, 7);
	hpacker = gtk_hbox_new (FALSE, 7);
	bypass_button = gtk_toggle_button_new_with_label ("bypass");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bypass_button), jvst->bypassed);
	editor_button = gtk_toggle_button_new_with_label ("editor");
	midi_learn_toggle = gtk_toggle_button_new_with_label ("midi Learn");
	save_button = gtk_button_new_with_label ("save state");
	load_button = gtk_button_new_with_label ("load state");

	//----------------------------------------------------------------------------------
	channel_listbox = gtk_combo_box_new_with_model ( GTK_TREE_MODEL(create_channel_store()) );
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (channel_listbox), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (channel_listbox), renderer, "text", 0, NULL);
	gtk_combo_box_set_active( GTK_COMBO_BOX(channel_listbox), (jvst->channel == -1) ? 0 : jvst->channel + 1 );
	g_signal_connect( G_OBJECT(channel_listbox), "changed", G_CALLBACK(channel_change), jvst ); 

	//----------------------------------------------------------------------------------
	GtkListStore* store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
	preset_listbox = gtk_combo_box_new_with_model( GTK_TREE_MODEL(store) );
	create_preset_store( store, jvst->fst );

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (preset_listbox), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (preset_listbox), renderer, "text", 0, NULL);
	gtk_combo_box_set_active( GTK_COMBO_BOX(preset_listbox), 0 );
	preset_listbox_signal = g_signal_connect( G_OBJECT(preset_listbox), "changed", G_CALLBACK( program_change ), jvst ); 
	//----------------------------------------------------------------------------------


	g_signal_connect (G_OBJECT(bypass_button), "toggled",
			    G_CALLBACK(bypass_handler),
			    jvst);

	g_signal_connect (G_OBJECT(midi_learn_toggle), "toggled",
			    G_CALLBACK(learn_handler),
			    jvst);

	g_signal_connect (G_OBJECT(editor_button), "toggled",
			    G_CALLBACK(editor_handler),
			    jvst);

	g_signal_connect (G_OBJECT(load_button), "clicked",
			    G_CALLBACK(load_handler),
			    jvst);

	g_signal_connect (G_OBJECT(save_button), "clicked",
			    G_CALLBACK(save_handler),
			    jvst);

	gtk_container_set_border_width (GTK_CONTAINER(hpacker), 3);

	g_signal_connect (G_OBJECT(window), "delete_event",
			    G_CALLBACK(destroy_handler),
			    jvst);

	
	gtk_box_pack_end   (GTK_BOX(hpacker), midi_learn_toggle, FALSE, FALSE, 0);
	gtk_box_pack_end   (GTK_BOX(hpacker), preset_listbox, FALSE, FALSE, 0);
	gtk_box_pack_end   (GTK_BOX(hpacker), bypass_button, FALSE, FALSE, 0);
	gtk_box_pack_end   (GTK_BOX(hpacker), load_button, FALSE, FALSE, 0);
	gtk_box_pack_end   (GTK_BOX(hpacker), save_button, FALSE, FALSE, 0);
	gtk_box_pack_end   (GTK_BOX(hpacker), editor_button, FALSE, FALSE, 0);
	gtk_box_pack_end   (GTK_BOX(hpacker), channel_listbox, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(vpacker), hpacker, FALSE, FALSE, 0);

	gtk_container_add (GTK_CONTAINER (window), vpacker);

	// normally every socket should register it self like this.
	//g_signal_connect (G_OBJECT(window), "configure_event", G_CALLBACK(configure_handler), gtk_socket);


	// but you can show() a GtkSocket only with an id set.
	//gtk_socket_add_id (GTK_SOCKET (gtk_socket), jvst->fst->xid);
	

 	gtk_widget_show_all (window);

	// Nasty hack ;-)
	if (jvst->with_editor == 2)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(editor_button), TRUE);

	g_timeout_add(500, (GSourceFunc) idle_cb, jvst);
	
	printf( "calling gtk_main now\n" );
	gtk_main ();

	return 0;
}


typedef int (*error_handler_t)( Display *, XErrorEvent *);
static Display *the_gtk_display;
error_handler_t wine_error_handler;
error_handler_t gtk_error_handler;

int fst_xerror_handler( Display *disp, XErrorEvent *ev )
{
	if( disp == the_gtk_display ) {
		printf( "relaying error to gtk\n" );
		return gtk_error_handler( disp, ev );
	} else {
		printf( "relaying error to wine\n" );
		return wine_error_handler( disp, ev );
	}
}

void
gtk_gui_init (int *argc, char **argv[])
{
	wine_error_handler = XSetErrorHandler( NULL );
	gtk_init (argc, argv);
	the_gtk_display = gdk_x11_display_get_xdisplay( gdk_display_get_default() );
	gtk_error_handler = XSetErrorHandler( fst_xerror_handler );
}
