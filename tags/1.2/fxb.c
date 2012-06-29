#include "fst.h"

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

static unsigned int endian_swap(unsigned int x)
{
//	return (x>>24) | ((x<<8) & 0x00FF0000) | ((x>>8) & 0x0000FF00) | (x<<24);
	return __builtin_bswap32 (x);
}

static void fx_load_chunk ( FST *fst, FILE *fxfile, enum FxFileType chunkType )
{
	void * chunk = NULL;
	size_t chunkSize;
	size_t br;

	br = fread (&chunkSize, sizeof(size_t), 1, fxfile);
	chunkSize = endian_swap(chunkSize);
	printf("Chunk size: %d\n", chunkSize);

	// FIXME: are we should call this also for regular bank ? if not then why there is numElements ?
	VstPatchChunkInfo chunkInfo;
	chunkInfo.version = 1;
	chunkInfo.pluginUniqueID = fst->plugin->uniqueID;
	chunkInfo.pluginVersion = fst->plugin->version;
	chunkInfo.numElements = 1;
	if ( chunkType == FXBANK) {
		fst->plugin->dispatcher(fst->plugin, effBeginLoadBank, 0, 0, &chunkInfo, 0);
	} else if (chunkType == FXPROGRAM) {
		fst->plugin->dispatcher(fst->plugin, effBeginLoadProgram, 0, 0, &chunkInfo, 0);
	}

	chunk = malloc ( chunkSize );
	br = fread (chunk, 1, chunkSize, fxfile);

	printf("SetChunk type : %d\n", chunkType);


	fst->plugin->dispatcher(fst->plugin, effSetChunk, chunkType, chunkSize, chunk, 0);
	free(chunk);
}

static void fx_load_current_program( FST *fst, FILE *fxfile)
{
	unsigned int currentProgram;
	size_t br;

	br = fread ( &currentProgram, sizeof(currentProgram), 1, fxfile );
	currentProgram = endian_swap( currentProgram );
	fst_program_change(fst, (short) currentProgram);
}

// NOTE: Program numbers -1 and -2 mean we are not in Bank
static void fx_load_program ( FST *fst, FILE *fxfile, short programNumber )
{
	FXHeader fxHeader;
	char prgName[28];
	unsigned short i, isChunk;
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
			printf("FX File: program %d is unknown type", programNumber);
			return;
		}
	} else if (programNumber == -1) {
		isChunk=FALSE;
	} else if (programNumber == -2) {
		isChunk=TRUE;
	}

	br = fread ( &prgName, sizeof(prgName), 1, fxfile);
//	prgName = endian_swap(prgName);
	fst->plugin->dispatcher(fst->plugin, effSetProgramName, 0, 0, prgName, 0);

	if (isChunk) {
		fx_load_chunk(fst, fxfile, FXPROGRAM);
	} else {
		float Params[fxHeader.numPrograms];
		float v;
		br = fread(&Params, sizeof(float), fxHeader.numPrograms, fxfile);

		pthread_mutex_lock (&fst->lock);
		for (i = 0; i < fxHeader.numPrograms; i++ ) {
			v = (float) endian_swap( (unsigned int) Params[i] );
			fst->plugin->setParameter( fst->plugin, i, v );
		}
		pthread_mutex_unlock (&fst->lock);
	}
}

int fst_load_fxfile ( FST *fst, const char *filename )
{
	FXHeader fxHeader;
	unsigned short i;
        size_t br;

	FILE * fxfile = fopen( filename, "rb" );

	br = fread ( &fxHeader, sizeof(FXHeader), 1, fxfile );
        fxHeader.fxID = endian_swap( fxHeader.fxID );
	fxHeader.numPrograms = endian_swap( fxHeader.numPrograms );
	fxHeader.chunkMagic = endian_swap( fxHeader.chunkMagic );
	fxHeader.fxMagic = endian_swap( fxHeader.fxMagic );
	fxHeader.version = endian_swap( fxHeader.version );

	printf("Numprograms: %d\n", fxHeader.numPrograms);

	if (fxHeader.chunkMagic != cMagic) {
		printf("FX File is corupted\n");
		fclose(fxfile);
		return 0;
	}

	printf("Compare: Plugin UniqueID (%d) to Bank fxID (%d)\n", fst->plugin->uniqueID, fxHeader.fxID);
	if (fst->plugin->uniqueID != fxHeader.fxID) {
		printf( "Error: Plugin UniqID not match\n");
		fclose( fxfile );
		return 0;
	}

	printf("FX File type is: ");
	switch (fxHeader.fxMagic) {
		// Preset file with float parameters
		case fMagic:
			printf("fMagic\n");
			fx_load_program(fst, fxfile, -1);
			break;
		// Preset file with one chunk
		case chunkPresetMagic:
			printf("chunkPresetMagic\n");
			fx_load_program(fst, fxfile, -2);
			break;
		// Bank file with programs
		case bankMagic:
			printf("bankMagic\n");
			// For version 2 read current program
			if (fxHeader.version == 2)
				fx_load_current_program(fst, fxfile);
			// skip blank hole
			fseek ( fxfile , 156 , SEEK_SET );
			for (i=0; i < fxHeader.numPrograms; i++)
				fx_load_program(fst, fxfile, i);
			break;
		// Bank file with one chunk
		case chunkBankMagic:
			printf("chunkBankMagic\n");
			// For version 2 read current program
			if (fxHeader.version == 2)
				fx_load_current_program(fst, fxfile);
			// skip blank hole
			fseek ( fxfile , 156 , SEEK_SET );
			fx_load_chunk(fst, fxfile, FXBANK);
			break;
		default:
			printf("Unknown\n");
			fclose(fxfile);
			return 0;
	}

	fclose(fxfile);
	return 1;
}

static int fx_save_params ( FST *fst, FILE *fxfile )
{
	float Params[fst->plugin->numParams];
	unsigned short i;
	float v;

	pthread_mutex_lock (&fst->lock);
	for (i = 0; i < fst->plugin->numParams; i++ ) {
		v = endian_swap( fst->plugin->getParameter( fst->plugin, i ) );
		Params[i] = (float) endian_swap( (unsigned int) v );
	}
	pthread_mutex_unlock (&fst->lock);

	fwrite(&Params, sizeof(float), fst->plugin->numParams, fxfile);
}

int fst_save_fxfile ( FST *fst, const char *filename, enum FxFileType fileType )
{
	FXHeader fxHeader;
        void * chunk = NULL;
	size_t chunkSize;
	size_t swapedChunkSize;
	char prgName[28];
	short p;

	bool isBank = (fileType == FXBANK) ? TRUE : FALSE;
	bool isChunk = (fst->plugin->flags & effFlagsProgramChunks);
	enum FxFileType chunkType = fileType;

        fxHeader.chunkMagic = endian_swap( cMagic );

	// Determine type
	printf ("Save FX files type: ");
	if (isBank) {
		if (isChunk) {
			fxHeader.fxMagic = chunkBankMagic;
                        printf("chunkBankMagic\n");
		} else {
			fxHeader.fxMagic = bankMagic;
                        printf("bankMagic\n");
		}
	} else {
		if (isChunk) {
			fxHeader.fxMagic = chunkPresetMagic;
                        printf("chunkPresetMagic\n");
		} else {
			fxHeader.fxMagic = fMagic;
                        printf("fMagic\n");
		}
	}

	fxHeader.fxMagic = endian_swap ( fxHeader.fxMagic );
        fxHeader.version = endian_swap( 2 );
        fxHeader.fxID = endian_swap( fst->plugin->uniqueID );
        fxHeader.fxVersion = endian_swap( fst->plugin->version );
        fxHeader.numPrograms = endian_swap( (isBank) ? fst->plugin->numPrograms : fst->plugin->numParams );

	unsigned int headerSize = ( sizeof(FXHeader) - sizeof(fxHeader.chunkMagic) - sizeof(fxHeader.byteSize) );
	unsigned int paramSize = fst->plugin->numParams * sizeof(float);
	unsigned int programSize = headerSize + sizeof(prgName) + paramSize;
	unsigned int currentProgram = fst->current_program; // used by Banks

	fxHeader.byteSize = headerSize;

	if (isChunk) {
		printf("Getting chunk ...");
		chunkSize = fst->plugin->dispatcher( fst->plugin, effGetChunk, chunkType, 0, &chunk, 0 );
		printf("%d B -  DONE\n", chunkSize);
		fxHeader.byteSize += chunkSize + sizeof(int);
	} else {
		fxHeader.byteSize += (isBank) ? fst->plugin->numPrograms * programSize : paramSize;
	}

	// Add current program and blank hole for bank or program name for program
	fxHeader.byteSize += (isBank) ? (sizeof(currentProgram) + 124) : sizeof(prgName);
	fxHeader.byteSize = endian_swap(fxHeader.byteSize);

	FILE * fxfile = fopen( filename, "wb" );
	fwrite(&fxHeader, sizeof(FXHeader), 1, fxfile);

	if (isBank) {
		currentProgram = endian_swap(currentProgram);
		fwrite(&currentProgram, sizeof(currentProgram), 1, fxfile);
		char blank[124];
		memset(blank, 0, sizeof(blank));
		fwrite(&blank, sizeof(blank), 1, fxfile);
	} else {
//		prgName = endian_swap(prgName);
		fst_get_program_name(fst, fst->current_program, prgName, sizeof(prgName));
		fwrite(&prgName, sizeof(prgName), 1, fxfile);
	}

	if (isChunk) {
		// Bank or Program with one chunk
		swapedChunkSize = endian_swap(chunkSize);
		fwrite(&swapedChunkSize, sizeof(swapedChunkSize), 1, fxfile);
		fwrite(chunk, chunkSize, 1, fxfile);
	} else if (isBank) {
		// Bank conatins multiple regular programs
		fxHeader.fxMagic = endian_swap( fMagic );
		fxHeader.numPrograms = endian_swap( fst->plugin->numParams );
		fxHeader.byteSize = endian_swap( programSize );

		for (p = 0; p < fst->plugin->numPrograms; p++) {
			fst_program_change (fst, p);
			fst_get_program_name(fst, fst->current_program, prgName, sizeof(prgName));

			fwrite(&fxHeader, sizeof(FXHeader), 1, fxfile);
			fwrite(&prgName, sizeof(prgName), 1, fxfile);

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

