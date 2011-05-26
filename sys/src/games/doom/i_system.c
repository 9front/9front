/* i_system.c */

#include "doomdef.h"
#include "doomtype.h"

#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"

#include "d_net.h"
#include "g_game.h"
#include "m_misc.h"

int mb_used = 6;	/* 6MB heap */

void I_Init (void)
{
	// I_InitSound();
	I_InitGraphics();
}

byte* I_ZoneBase (int *size)
{
	*size = mb_used*1024*1024;
	return (byte *) malloc(*size);
}

/* returns time in 1/70th second tics */
int I_GetTime (void)
{
	return (int)((nsec()*TICRATE)/1000000000);
}

static ticcmd_t emptycmd;
ticcmd_t* I_BaseTiccmd (void)
{
	return &emptycmd;
}

void I_Quit (void)
{
	D_QuitNetGame ();
	I_ShutdownSound();
	I_ShutdownMusic();
	M_SaveDefaults ();
	I_ShutdownGraphics();
	threadexitsall(nil);
}

byte* I_AllocLow (int length)
{
	byte *mem;
        
	mem = (byte *)malloc (length);
	memset (mem,0,length);
	return mem;
}

void I_Tactile(int on, int off, int total)
{
	USED(on, off, total);
}

/*
ticcmd_t	emptycmd;
ticcmd_t*	I_BaseTiccmd(void)
{
    return &emptycmd;
}


int  I_GetHeapSize (void)
{
    return mb_used*1024*1024;
}

byte* I_ZoneBase (int*	size)
{
    *size = mb_used*1024*1024;
    return (byte *) malloc (*size);
}
*/


//
// I_Error
//
extern boolean demorecording;

void I_Error (char *error, ...)
{
    va_list	argptr;

    // Message first.
    va_start (argptr,error);
    fprintf (stderr, "Error: ");
    vfprintf (stderr,error,argptr);
    fprintf (stderr, "\n");
    va_end (argptr);

    fflush( stderr );

    // Shutdown. Here might be other errors.
    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame ();
    I_ShutdownGraphics();

    threadexitsall("I_Error");
}

int I_FileExists (char *filepath)
{
	return (0 == access(filepath, AREAD));
}

int I_Open (char *filepath)
{
	return open(filepath, OREAD);
}

void I_Close (int handle)
{
	close (handle);
}

int I_Seek (int handle, int n)
{
	return seek(handle, n, 0);
}

int I_Read (int handle, void *buf, int n)
{
	return read(handle, buf, n);
}

char* I_IdentifyWAD(char *wadname)
{
	char path[1024];

	/* /sys/lib/doom/... */
	snprintf(path, sizeof path, "/sys/lib/doom/%s", wadname);
	if (I_FileExists (path))
		return path;

	/* $home/lib/doom/... */
	snprintf(path, sizeof path, "%s/lib/doom/%s", getenv("home"), wadname);
	if (I_FileExists (path))
		return path;

	return nil;
}
