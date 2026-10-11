/*=================================================================
	File created by Yohann NICOLAS.

	Error Logger implementation.

=================================================================*/

#include "parameters.h"
#include "error.h"
#include <windows.h>
#include <stdio.h>			// fopen() fclose() fprintf() vfprintf()
#include <stdarg.h>			// ...
#include <string.h>			// strlen() strcat()
#include <direct.h>			// _getcwd()

int active_logFile = 1;
// Stays 1 until loadParameters reads the ini: boxes raised before that keep the historical behavior.
int active_ShowErrorMessageBox = 1;

static char log_file[MAX_PATH + 16] = "";
static bool log_init = false;
static FILE* fLogFile = NULL;

void log_initfile()
{
	if (log_init) return;

	if (!_getcwd( log_file, MAX_PATH ))
		log_file[0] = 0;
	int len = strlen(log_file);
	if( len > 0 && log_file[len - 1] != '\\')
		strcat(log_file, "\\");
	strcat(log_file, "PlugY.log");

	fLogFile = fopen( log_file, "w" );

	if( fLogFile == NULL )
		return;

	log_init = true;

	log_msg("<----------------------------------------------->\n"
			"\n"
			"\t\tPlugY4Kolbot\n"
			"\t\tbased on PlugY by Yohann\n"
			"\t\tversion %s\n"
			"\n"
			"\n"
			"<---------- Starting Diablo II Plugin ---------->\n\n\n"
			,PLUGY_VERSION);
}

void log_close()
{
	if (fLogFile)
	{
		fclose(fLogFile);
		fLogFile = NULL;
	}
}

void log_box( const char* pFormat, ... )
{
	// Large enough for messages naming two full save paths.
	char buffer[1024];
	va_list lArgs;
	va_start( lArgs, pFormat );
	_vsnprintf( buffer, sizeof(buffer) - 1, pFormat, lArgs );
	buffer[sizeof(buffer) - 1] = 0;
	va_end(lArgs);

	log_msg("%s\n", buffer);
	if (active_ShowErrorMessageBox)
		MessageBox(NULL, buffer, "PlugY4Kolbot", MB_OK|MB_ICONEXCLAMATION);
}


void log_msg( const char* pFormat, ... )
{
	if (!active_logFile)
		return;

	if( !log_init )
		log_initfile();

	if( log_init )
	{
		va_list lArgs;
		va_start( lArgs, pFormat );

		if ( fLogFile == NULL )
			fLogFile = fopen( log_file, "a" );

		if( fLogFile != NULL )
		{
			vfprintf( fLogFile, pFormat, lArgs );
			fflush(fLogFile);
		}
		else
			log_init = false;

		va_end(lArgs);
	}
}

void d2_assert( bool pCondition, char* pMessage, char* pLocation, int pLineNbr )
{
	if( pCondition )
	{
		log_msg("\n"
			"*-----------------------*\n"
			"Assertion fail at line %d of %s :\n"
			"%s\n"
			"*-----------------------*\n",
			pLineNbr, pLocation, pMessage);
		if (active_ShowErrorMessageBox)
			MessageBox(NULL, pMessage, "Diablo 2 Error", MB_OK|MB_ICONEXCLAMATION);
		exit(1);
	}
}

/*================================= END OF FILE =================================*/