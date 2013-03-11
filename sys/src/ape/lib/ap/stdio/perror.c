/*
 * pANS stdio -- perror
 */
#include "iolib.h"
#include <errno.h>

void perror(const char *s){
	if(s!=NULL && *s != '\0') fputs(s, stderr), fputs(": ", stderr);
	fputs(strerror(errno), stderr);
	putc('\n', stderr);
	fflush(stderr);
}
