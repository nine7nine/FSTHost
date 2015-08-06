#include <stdlib.h>
#include <stdio.h>

#include "log/log.h"
#include "fst.h"

#define INF log_info
#define DEBUG log_debug
#define ERR log_error

/** Root chunk identifier for Programs (fxp) and Banks (fxb). */
#define cMagic			'CcnK'

/** Regular Program (fxp) identifier. */
#define fMagic			'FxCk'

/** Regular Bank (fxb) identifier. */
#define bankMagic		'FxBk'

/** Program (fxp) identifier for opaque chunk data. */
#define chunkPresetMagic	'FPCh'

/** Bank (fxb) identifier for opaque chunk data. */
#define chunkBankMagic		'FBCh'

#define FXB_BLANK_HOLE 156

typedef struct {
	int32_t chunkMagic;
	int32_t byteSize;
	int32_t fxMagic;
	int32_t version;
	int32_t fxID;
	int32_t fxVersion;
	int32_t numPrograms;
} FXHeader;

static inline uint32_t endian_swap(uint32_t x)
{
//	return (x>>24) | ((x<<8) & 0x00FF0000) | ((x>>8) & 0x0000FF00) | (x<<24);
	return __builtin_bswap32 (x);
}

static inline float endian_swap_float ( float x )
{
	return (float) endian_swap ( (uint32_t) x );
}

static void INF_TYPE ( int32_t type ) {
	const char* strType = "Unknown";
	switch ( type ) {
	case fMagic:
		strType = "fMagic";
		break;
	case chunkPresetMagic:
		strType = "chunkPresetMagic";
		break;
	case bankMagic:
		strType = "bankMagic";
		break;
	case chunkBankMagic:
		strType = "chunkBankMagic";
		break;
	}
	INF( "FX File type is: %s", strType );
}

static void fx_load_chunk ( FST *fst, FILE *fxfile, enum FxFileType chunkType )
{
	size_t chunkSize;
	size_t br;

	br = fread (&chunkSize, sizeof(size_t), 1, fxfile);
	if (br != 1) return; // This should never happend
	chunkSize = endian_swap(chunkSize);
	INF("Chunk size: %zu", chunkSize);

	// FIXME: are we should call this also for regular bank ? if not then why there is numElements ?
	VstPatchChunkInfo chunkInfo;
	chunkInfo.version = 1;
	chunkInfo.pluginUniqueID = fst_uid(fst);
	chunkInfo.pluginVersion = fst_version(fst);
	chunkInfo.numElements = 1;
	if ( chunkType == FXBANK) {
		fst_call_dispatcher(fst, effBeginLoadBank, 0, 0, &chunkInfo, 0);
	} else if (chunkType == FXPROGRAM) {
		fst_call_dispatcher(fst, effBeginLoadProgram, 0, 0, &chunkInfo, 0);
	}

	void* chunk = malloc ( chunkSize );
	br = fread (chunk, 1, chunkSize, fxfile);
	if (br == chunkSize) {
		INF("SetChunk type : %d", chunkType);
		fst_call_dispatcher(fst, effSetChunk, chunkType, chunkSize, chunk, 0);
	} else {
		ERR("Error while read chunk (got: %zu, want: %zu)", br, chunkSize);
	}
	free(chunk);
}

static void fx_load_current_program( FST *fst, FILE *fxfile)
{
	int32_t currentProgram;
	size_t br = fread ( &currentProgram, sizeof currentProgram, 1, fxfile );
	if (br != 1) return;

	currentProgram = endian_swap( currentProgram );
	fst_program_change(fst, currentProgram);
}

// NOTE: Program numbers -1 and -2 mean we are not in Bank
static void fx_load_program ( FST *fst, FILE *fxfile, short programNumber )
{
	FXHeader fxHeader;
	char prgName[28];
	bool isChunk;
        size_t br;

	// if we in Bank
	if (programNumber >= 0) {
		br = fread ( &fxHeader, sizeof(FXHeader), 1, fxfile );

		// Note program here is actually a parameter
		fxHeader.numPrograms = endian_swap( fxHeader.numPrograms );
		fxHeader.fxMagic = endian_swap( fxHeader.fxMagic );

		fst_program_change (fst, programNumber);

		if (fxHeader.fxMagic == chunkPresetMagic) {
			isChunk=TRUE;
		} else if (fxHeader.fxMagic == fMagic) {
			isChunk=FALSE;
		} else {
			ERR("FX File: program %d is unknown type", programNumber);
			return;
		}
	} else if (programNumber == -1) {
		isChunk=FALSE;
	} else if (programNumber == -2) {
		isChunk=TRUE;
	} else {
		ERR("programNumber - wrongly set to %d", programNumber);
		return;
	}

	br = fread ( &prgName, sizeof(prgName), 1, fxfile);
	if (br != 1) return; // This should never happen
//	prgName = endian_swap(prgName);
	fst_call_dispatcher(fst, effSetProgramName, 0, 0, prgName, 0);

	if (isChunk) {
		fx_load_chunk(fst, fxfile, FXPROGRAM);
	} else {
		float Params[fxHeader.numPrograms];
		br = fread(&Params, sizeof(float), fxHeader.numPrograms, fxfile);

		pthread_mutex_lock (&fst->lock);
		int32_t i;
		for (i = 0; i < fxHeader.numPrograms; i++ ) {
			float v = endian_swap_float ( Params[i] );
			fst_set_param( fst, i, v );
		}
		pthread_mutex_unlock (&fst->lock);
	}
}

int fst_load_fxfile ( FST *fst, const char *filename )
{
        size_t br;

	FILE *fxfile = fopen( filename, "rb" );

	if (! fxfile) {
		ERR("Can't open file: %s", filename);
		return 0;
	}

	FXHeader fxHeader;
	br = fread ( &fxHeader, sizeof(FXHeader), 1, fxfile );
	if (br != 1) {
		ERR("FX File is corupted - can not load header. Loaded only: %zu", br);
		fclose(fxfile);
		return 0; // This should never happend
	}
        fxHeader.fxID = endian_swap( fxHeader.fxID );
	fxHeader.numPrograms = endian_swap( fxHeader.numPrograms );
	fxHeader.chunkMagic = endian_swap( fxHeader.chunkMagic );
	fxHeader.fxMagic = endian_swap( fxHeader.fxMagic );
	fxHeader.version = endian_swap( fxHeader.version );

	ERR("Numprograms: %d", fxHeader.numPrograms);

	if (fxHeader.chunkMagic != cMagic) {
		ERR("FX File is corupted - wrong magic (%d != %d)", fxHeader.chunkMagic, cMagic);
		fclose(fxfile);
		return 0;
	}

	INF("Compare: Plugin UniqueID (%d) to Bank fxID (%d)", fst_uid(fst), fxHeader.fxID);
	if (fst_uid(fst) != fxHeader.fxID) {
		ERR( "Error: Plugin UniqID not match");
		fclose( fxfile );
		return 0;
	}

	switch (fxHeader.fxMagic) {
		// Preset file with float parameters
		case fMagic:
			fx_load_program(fst, fxfile, -1);
			break;
		// Preset file with one chunk
		case chunkPresetMagic:
			fx_load_program(fst, fxfile, -2);
			break;
		// Bank file with programs
		case bankMagic:
			// For version 2 read current program
			if (fxHeader.version == 2)
				fx_load_current_program(fst, fxfile);
			// skip blank hole
			fseek ( fxfile , FXB_BLANK_HOLE, SEEK_SET );
			int32_t i;
			for (i=0; i < fxHeader.numPrograms; i++)
				fx_load_program(fst, fxfile, i);
			break;
		// Bank file with one chunk
		case chunkBankMagic:
			// For version 2 read current program
			if (fxHeader.version == 2)
				fx_load_current_program(fst, fxfile);
			// skip blank hole
			fseek ( fxfile , FXB_BLANK_HOLE , SEEK_SET );
			fx_load_chunk(fst, fxfile, FXBANK);
			break;
		default:
			ERR("Unknown FX file type");
			fclose(fxfile);
			return 0;
	}
	INF_TYPE ( fxHeader.fxMagic );

	fclose(fxfile);
	return 1;
}

static void fx_save_params ( FST *fst, FILE *fxfile )
{
	float Params[ fst_num_params(fst) ];

	pthread_mutex_lock (&fst->lock);
	int32_t i;
	for (i = 0; i < fst_num_params(fst); i++ )
		Params[i] = endian_swap_float ( fst_get_param(fst, i) );

	pthread_mutex_unlock (&fst->lock);

	fwrite(&Params, sizeof(float), fst_num_params(fst), fxfile);
}

int fst_save_fxfile ( FST *fst, const char *filename, enum FxFileType fileType )
{
	char prgName[28];

	bool isBank = (fileType == FXBANK) ? TRUE : FALSE;
	bool isChunk = fst_has_chunks(fst);
	enum FxFileType chunkType = fileType;

	FXHeader fxHeader;
        fxHeader.chunkMagic = endian_swap( cMagic );

	// Determine type
	fxHeader.fxMagic = (isBank)
		? ( (isChunk) ? chunkBankMagic : bankMagic )
		: ( (isChunk) ? chunkPresetMagic : fMagic );
	INF_TYPE ( fxHeader.fxMagic );

	fxHeader.fxMagic = endian_swap ( fxHeader.fxMagic );
        fxHeader.version = endian_swap( (isBank) ? 2 : 1 );
        fxHeader.fxID = endian_swap( fst_uid(fst) );
        fxHeader.fxVersion = endian_swap( fst_version(fst) );
        fxHeader.numPrograms = endian_swap( (isBank) ? fst_num_presets(fst) : fst_num_params(fst) );

	size_t headerSize = ( sizeof(FXHeader) - sizeof(fxHeader.chunkMagic) - sizeof(fxHeader.byteSize) );
	size_t paramSize = fst_num_params(fst) * sizeof(float);
	size_t programSize = headerSize + sizeof(prgName) + paramSize;
	int32_t currentProgram = fst->current_program; // used by Banks

	fxHeader.byteSize = headerSize;

        void * chunk = NULL;
	int32_t chunkSize;
	if (isChunk) {
		chunkSize = fst_call_dispatcher( fst, effGetChunk, chunkType, 0, &chunk, 0 );
		INF("Got chunk %zu B", (size_t) chunkSize);
		fxHeader.byteSize += chunkSize + sizeof(chunkSize);
	} else {
		fxHeader.byteSize += (isBank) ? fst_num_presets(fst) * programSize : paramSize;
	}

	// Add current program and blank hole for bank or program name for program
	fxHeader.byteSize += (isBank) ? (sizeof(currentProgram) + 124) : sizeof(prgName);
	fxHeader.byteSize = endian_swap(fxHeader.byteSize);

	FILE * fxfile = fopen( filename, "wb" );
	fwrite(&fxHeader, sizeof(FXHeader), 1, fxfile);

	if (isBank) {
		currentProgram = endian_swap(currentProgram);
		fwrite(&currentProgram, sizeof(currentProgram), 1, fxfile);
		// write blank hole
		fseek ( fxfile , FXB_BLANK_HOLE, SEEK_SET );
	} else { /* isProgram */
//		prgName = endian_swap(prgName);
		fst_get_program_name ( fst, fst->current_program, prgName, sizeof prgName );
		fwrite(&prgName, sizeof prgName, 1, fxfile);
	}

	if (isChunk) {
		// Bank or Program with one chunk
		int32_t swapedChunkSize = endian_swap ( chunkSize );
		fwrite(&swapedChunkSize, sizeof swapedChunkSize, 1, fxfile);
		fwrite(chunk, chunkSize, 1, fxfile);
	} else if (isBank) {
		// Bank with multiple regular programs
		fxHeader.fxMagic = endian_swap( fMagic );
		fxHeader.numPrograms = endian_swap( fst_num_presets(fst) );
		fxHeader.byteSize = endian_swap( programSize );

		int32_t p;
		for (p = 0; p < fst_num_presets(fst); p++) {
			fst_program_change (fst, p);
			fst_get_program_name(fst, fst->current_program, prgName, sizeof prgName);

			fwrite(&fxHeader, sizeof(FXHeader), 1, fxfile);
			fwrite(&prgName, sizeof prgName, 1, fxfile);

			fx_save_params( fst, fxfile );
		}

		fst_program_change (fst, currentProgram);
	} else {
		// Regular program file
		fx_save_params( fst, fxfile );
	}

	fclose(fxfile);

	return 1;
}

