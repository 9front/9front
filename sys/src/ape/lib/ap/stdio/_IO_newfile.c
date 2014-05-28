/*
 * pANS stdio -- fopen
 */
#include "iolib.h"

#define _PLAN9_SOURCE
#include <lock.h>
#include <errno.h>

FILE *_IO_newfile(void)
{
	static FILE *fx=0;
	static Lock fl;
	FILE *f;
	int i;

	lock(&fl);
	for(i=0; i<FOPEN_MAX; i++){
		if(fx==0 || ++fx >= &_IO_stream[FOPEN_MAX]) fx=_IO_stream;
		if(fx->state==CLOSED)
			break;
	}
	f = fx;
	unlock(&fl);
	if(f->state!=CLOSED){
		errno = EMFILE;
		return NULL;
	}
	return f;
}
