#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

int
z80step(void)
{
	if((z80bus & RESET) != 0){
		return 1;
	}
	if((z80bus & BUSACK) != 0){
		if((z80bus & BUSREQ) == 0)
			z80bus &= ~BUSACK;
		return 1;
	}
	if((z80bus & BUSREQ) != 0){
		z80bus |= BUSACK;
		return 1;
	}
	return 0;
}
