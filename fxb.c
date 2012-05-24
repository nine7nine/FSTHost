#include <stdio.h>
#include <pthread.h>

#include <fst.h>

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

#define FXBANK    0
#define FXPROGRAM 1

#if 0
void RemoteVstPlugin::savePreset( const std::string & _file )
{
	unsigned int chunk_size = 0;
	FSTBank * fxHeader = ( FSTBank* ) new char[ sizeof( FSTBank ) ];
	char progName[ 128 ] = { 0 };
	char* data = NULL;
	const bool chunky = ( m_plugin->flags & ( 1 << 5 ) ) != 0;
	bool isPreset = _file.substr( _file.find_last_of( "." ) + 1 )  == "fxp";
	int presNameLen = _file.find_last_of( "/" ) + _file.find_last_of( "\\" ) + 2;

	if (isPreset) {
		for (int i = 0; i < _file.length() - 4 - presNameLen; i++) 
			progName[i] = i < 23 ? _file[presNameLen + i] : 0;
		m_plugin->dispatcher(m_plugin, 4, 0, 0, progName, 0);
	} //	m_plugin->dispatcher( m_plugin, effGetProgramName, 0, 0, progName, 0.0f );
	if ( chunky )
		chunk_size = m_plugin->dispatcher( m_plugin, 23, isPreset, 0, &data, false );
	else {
		if (isPreset) {
			chunk_size = m_plugin->numParams * sizeof( float );
			data = new char[ chunk_size ];
			unsigned int* toUIntArray = reinterpret_cast<unsigned int*>( data );
			for ( int i = 0; i < m_plugin->numParams; i++ )
			{
				float value = m_plugin->getParameter( m_plugin, i );
				unsigned int * pValue = ( unsigned int * ) &value;
				toUIntArray[ i ] = endian_swap( *pValue );
			}
		} else chunk_size = (((m_plugin->numParams * sizeof( float )) + 56)*m_plugin->numPrograms);
	}

	fxHeader->chunkMagic = 0x4B6E6343;
	fxHeader->byteSize = chunk_size + ( chunky ? sizeof( int ) : 0 ) + 48;
	if (!isPreset) fxHeader->byteSize += 100;
	fxHeader->byteSize = endian_swap( fxHeader->byteSize );
	fxHeader->fxMagic = chunky ? 0x68435046 : 0x6B437846;
	if (!isPreset && chunky) fxHeader->fxMagic = 0x68434246;
	if (!isPreset &&!chunky) fxHeader->fxMagic = 0x6B427846;

	fxHeader->version = 0x01000000;
	unsigned int uIntToFile = (unsigned int) m_plugin->uniqueID;
	fxHeader->fxID = endian_swap( uIntToFile );
	uIntToFile = (unsigned int) pluginVersion();
	fxHeader->fxVersion = endian_swap( uIntToFile );
	uIntToFile = (unsigned int) chunky ? m_plugin->numPrograms : m_plugin->numParams;
	if (!isPreset &&!chunky) uIntToFile = (unsigned int) m_plugin->numPrograms;
	fxHeader->numPrograms = endian_swap( uIntToFile );

	FILE * fxfile = fopen( _file.c_str(), "w" );
	fwrite ( fxHeader, 1, 28, fxfile );
	fwrite ( progName, 1, isPreset ? 28 : 128, fxfile );
	if ( chunky ) {
		uIntToFile = endian_swap( chunk_size );
		fwrite ( &uIntToFile, 1, 4, fxfile );
	}
	if (fxHeader->fxMagic != 0x6B427846 )
		fwrite ( data, 1, chunk_size, fxfile );
	else {
		int numPrograms = m_plugin->numPrograms;
		int currProgram = m_plugin->dispatcher(m_plugin, effGetProgram, 0, 0, 0, 0);
		chunk_size = (m_plugin->numParams * sizeof( float ));
		fxHeader->byteSize = chunk_size + 48;
		fxHeader->byteSize = endian_swap( fxHeader->byteSize );
		fxHeader->fxMagic = 0x6B437846;
		uIntToFile = (unsigned int) m_plugin->numParams;
		fxHeader->numPrograms = endian_swap( uIntToFile );
		data = new char[ chunk_size ];
		unsigned int* pValue,* toUIntArray = reinterpret_cast<unsigned int*>( data );
		float value;
		for (int j = 0; j < numPrograms; j++) {
			m_plugin->dispatcher(m_plugin, effSetProgram, 0, j, 0, 0);
			m_plugin->dispatcher(m_plugin, effGetProgramName, 0, 0, fxHeader->prgName, 0);
			fwrite ( fxHeader, 1, 56, fxfile );
			for ( int i = 0; i < m_plugin->numParams; i++ )
			{
				value = m_plugin->getParameter( m_plugin, i );
				pValue = ( unsigned int * ) &value;
				toUIntArray[ i ] = endian_swap( *pValue );
			}
			fwrite ( data, 1, chunk_size, fxfile );
		}
		m_plugin->dispatcher(m_plugin, effSetProgram, 0, currProgram, 0, 0);
	}
	fclose( fxfile );

	if ( !chunky ) 
		delete[] data;
	delete[] (FSTBank*)fxHeader;

}

#endif

unsigned int endian_swap(unsigned int x)
{
//	return (x>>24) | ((x<<8) & 0x00FF0000) | ((x>>8) & 0x0000FF00) | (x<<24);
	return __builtin_bswap32 (x);
}

void fx_load_chunk ( FST *fst, FILE *fxfile, int chunkType )
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
		fst_call_dispatcher(fst, effBeginLoadBank, 0, 0, &chunkInfo, 0);
	} else if (chunkType == FXPROGRAM) {
		fst_call_dispatcher(fst, effBeginLoadProgram, 0, 0, &chunkInfo, 0);
	}

	chunk = malloc ( chunkSize );
	br = fread (chunk, 1, chunkSize, fxfile);

	printf("SetChunk type : %d\n", chunkType);


	fst_call_dispatcher(fst, effSetChunk, chunkType, chunkSize, chunk, 0);
	free(chunk);
}

// NOTE: Program numbers -1 and -2 mean we are not in Bank
void fx_load_program ( FST *fst, FILE *fxfile, int programNumber )
{
	FXHeader fxHeader;
	char prgName[28];
	int i, isChunk;
        size_t br;

	// if we in Bank
	if (programNumber >= 0) {
		br = fread ( &fxHeader, sizeof(FXHeader), 1, fxfile );

		// Note program is actually param
		fxHeader.numPrograms = endian_swap( fxHeader.numPrograms );
		fxHeader.fxMagic = endian_swap( fxHeader.fxMagic );

		fst_call_dispatcher(fst, effSetProgram, 0, programNumber, 0, 0);

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
	fst_call_dispatcher(fst, effSetProgramName, 0, 0, prgName, 0);

	if (isChunk) {
		fx_load_chunk(fst, fxfile, FXPROGRAM);
	} else {
		float Params[fxHeader.numPrograms];
		br = fread(&Params, sizeof(float), fxHeader.numPrograms, fxfile);

		pthread_mutex_lock (&fst->lock);
		for (i = 0; i < fxHeader.numPrograms; i++ )
			fst->plugin->setParameter( fst->plugin, i, Params[i] );
		pthread_mutex_unlock (&fst->lock);
	}
}

int fst_load_fxfile ( FST *fst, char *filename )
{
	FXHeader fxHeader;
	int i;
        size_t br;

	FILE * fxfile = fopen( filename, "rb" );

	br = fread ( &fxHeader, sizeof(FXHeader), 1, fxfile );
        fxHeader.fxID = endian_swap( fxHeader.fxID );
	fxHeader.numPrograms = endian_swap( fxHeader.numPrograms );
	fxHeader.chunkMagic = endian_swap( fxHeader.chunkMagic );
	fxHeader.fxMagic = endian_swap( fxHeader.fxMagic );

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
			// skip blank hole
			fseek ( fxfile , 156 , SEEK_SET );
			for (i=0; i < fxHeader.numPrograms; i++)
				fx_load_program(fst, fxfile, i);
			break;
		// Bank file with one chunk
		case chunkBankMagic:
			printf("chunkBankMagic\n");
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

int fst_save_fxfile ( FST *fst, char *filename, int isBank )
{
	FXHeader fxHeader;
        void * chunk = NULL;
	size_t chunkSize;
	size_t swapedChunkSize;
	char prgName[28];
	int isChunk = (fst->plugin->flags & effFlagsProgramChunks);
	int chunkType = (isBank) ? FXBANK : FXPROGRAM;

	if (! isChunk) {
		printf("This type of plugin are currently not supported :-(\n");
		return 0;
	}

        fxHeader.chunkMagic = endian_swap( cMagic );
        fxHeader.fxMagic = endian_swap( chunkBankMagic );
        fxHeader.version = endian_swap( 1 );
        fxHeader.fxID = endian_swap( fst->plugin->uniqueID );
        fxHeader.fxVersion = endian_swap( fst->plugin->version );
        fxHeader.numPrograms = endian_swap( fst->plugin->numPrograms );
	fxHeader.byteSize = ( sizeof(FXHeader) - sizeof(fxHeader.chunkMagic) - sizeof(fxHeader.byteSize) );

	if (isChunk) {
		printf("Getting chunk ...");
		chunkSize = fst_call_dispatcher( fst, effGetChunk, chunkType, 0, &chunk, 0 );
		printf(" DONE\n");
		fxHeader.byteSize += chunkSize + sizeof(int);
	}

	// Add hole for bank or program name for program
	fxHeader.byteSize += (isBank) ? 128 : sizeof(prgName);
	fxHeader.byteSize = endian_swap(fxHeader.byteSize);

	FILE * fxfile = fopen( filename, "wb" );
	fwrite(&fxHeader, sizeof(FXHeader), 1, fxfile);

	if (isBank) {
		char blank[128];
		memset(blank, 0, sizeof(blank));
		fwrite(&blank, sizeof(blank), 1, fxfile);
	} else {
//		prgName = endian_swap(prgName);
		fwrite(&prgName, sizeof(prgName), 1, fxfile);
	}

	// Save Chunk
	swapedChunkSize = endian_swap(chunkSize);
	fwrite(&swapedChunkSize, sizeof(swapedChunkSize), 1, fxfile);
	fwrite(chunk, chunkSize, 1, fxfile);

	fclose(fxfile);	

	return 1;
}
