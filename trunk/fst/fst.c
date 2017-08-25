#include "fst_int.h"

void fst_lock   ( FST* fst ) { pthread_mutex_lock  ( &fst->lock ); }
void fst_unlock ( FST* fst ) { pthread_mutex_unlock( &fst->lock ); }

void fst_set_chunk(FST* fst, enum FxFileType type, int size, void* chunk) {
	fst_call_dispatcher(fst, effSetChunk, type, size, chunk, 0);
}

int fst_get_chunk(FST* fst, enum FxFileType type, void* chunk) {
	return fst_call_dispatcher( fst, effGetChunk, type, 0, chunk, 0 );
}

bool fst_want_midi_in ( FST* fst ) {
	/* No MIDI at all - very old/rare v1 plugins */
	if (fst->vst_version < 2) return false;
	return (fst->isSynth || fst->canReceiveVstEvents || fst->canReceiveVstMidiEvent);
}

bool fst_want_midi_out ( FST* fst ) {
	/* No MIDI at all - very old/rare v1 plugins */
	if (fst->vst_version < 2) return false;
	return (fst->canSendVstEvents || fst->canSendVstMidiEvent);
}

void fst_set_window_close_callback ( FST* fst, FSTWindowCloseCallback f, void* ptr ) {
	fst->window_close_cb = f;
	fst->window_close_cb_data = ptr;
}

void fst_process ( FST* fst, float** ins, float** outs, int32_t frames ) {
	if ( fst->plugin->flags & effFlagsCanReplacing ) {
		fst->plugin->processReplacing (fst->plugin, ins, outs, frames);
	} else {
		fst->plugin->process (fst->plugin, ins, outs, frames);
	}
}

void fst_process_events ( FST* fst, void* events ) {
	fst->plugin->dispatcher (fst->plugin, effProcessEvents, 0, 0, (VstEvents*) events, 0.0f);
}

float fst_get_param ( FST* fst, int32_t param ) {
	return fst->plugin->getParameter(fst->plugin,param);
}

void fst_set_param ( FST* fst, int32_t param, float value ) {
	fst->plugin->setParameter( fst->plugin, param, value );
}

bool fst_process_trylock ( FST* fst ) { return ( pthread_mutex_trylock ( &(fst->process_lock) ) == 0 ); }
void fst_process_lock ( FST* fst ) { pthread_mutex_lock ( &(fst->process_lock) ); }
void fst_process_unlock ( FST* fst ) { pthread_mutex_unlock ( &(fst->process_lock) ); }

int32_t	fst_num_params	( FST* fst ) { return fst->plugin->numParams; }
int32_t	fst_num_presets	( FST* fst ) { return fst->plugin->numPrograms; }
int32_t	fst_num_ins	( FST* fst ) { return fst->plugin->numInputs; }
int32_t	fst_num_outs	( FST* fst ) { return fst->plugin->numOutputs; }
int32_t	fst_uid 	( FST* fst ) { return fst->plugin->uniqueID; }
int32_t	fst_version 	( FST* fst ) { return fst->plugin->version; }
bool	fst_has_chunks	( FST* fst ) { return fst->plugin->flags & effFlagsProgramChunks; }
bool	fst_has_window	( FST* fst ) { return fst->window; }
int32_t	fst_max_port_name ( FST* fst ) { return kVstMaxLabelLen; }
int	fst_width (FST* fst) { return fst->width; }
int	fst_height (FST* fst) { return fst->height; }
AMC*	fst_amc (FST* fst) { return &fst->amc; }
void*	fst_xid (FST* fst) { return fst->xid; }
bool	fst_has_popup_editor ( FST* fst ) { return fst->editor_popup; }

const char* fst_name (FST* fst) { return fst->handle->name; }
const char* fst_path (FST* fst) { return fst->handle->path; }

void fst_get_param_name ( FST* fst, int32_t param, char* name ) {
	fst_call_dispatcher ( fst, effGetParamName, param, 0, (void*) name, 0 );
}

bool fst_has_editor ( FST* fst ) {
	return fst->plugin->flags & effFlagsHasEditor;
}
