/* i_main.c */

#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"

void main(int argc, char **argv)
{
	myargc = argc; 
	myargv = argv; 
	D_DoomMain ();
} 
