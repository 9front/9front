#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"
/*
 * Just a wrapper for all the initialization routines
 */
int plinit(void){
	if(!pl_drawinit()) return 0;
	return 1;
}
