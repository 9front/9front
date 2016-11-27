/*
 * pANS stdio -- sclose
 */
#include "iolib.h"
char *sclose(FILE *f){
	switch(f->state){
	default:	/* ERR CLOSED */
	Error:
		if(f->buf && f->flags&BALLOC)
			free(f->buf);
		f->buf=0;
		break;
	case OPEN:
		f->buf=malloc(1);
		f->buf[0]='\0';
		break;
	case RD:
	case END:
		break;
	case RDWR:
	case WR:
		f->rp=f->buf+f->bufl;
		if(f->wp==f->rp){
			if(f->flags&BALLOC){
				char *t = realloc(f->buf, f->bufl+1);
				if(t==NULL)
					goto Error;
				f->buf=t;
				f->wp=t+f->bufl;
			} else {
				if(f->wp > f->buf)
					*(f->wp-1) = '\0';
				goto Error;
			}
		}
		*f->wp='\0';
		break;
	}
	f->state=CLOSED;
	f->flags=0;
	return f->buf;
}
