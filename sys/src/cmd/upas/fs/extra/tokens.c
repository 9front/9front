#include <u.h>
#include <libc.h>

/* unfortunately, tokenize insists on collapsing multiple seperators */
static char qsep[] = " \t\r\n";

static char*
qtoken(char *s, char *sep)
{
	int quoting;
	char *t;

	quoting = 0;
	t = s;	/* s is output string, t is input string */
	while(*t!='\0' && (quoting || utfrune(sep, *t)==nil)){
		if(*t != '\''){
			*s++ = *t++;
			continue;
		}
		/* *t is a quote */
		if(!quoting){
			quoting = 1;
			t++;
			continue;
		}
		/* quoting and we're on a quote */
		if(t[1] != '\''){
			/* end of quoted section; absorb closing quote */
			t++;
			quoting = 0;
			continue;
		}
		/* doubled quote; fold one quote into two */
		t++;
		*s++ = *t++;
	}
	if(*s != '\0'){
		*s = '\0';
		if(t == s)
			t++;
	}
	return t;
}

int
getmtokens(char *s, char **args, int maxargs, int multiflag)
{
	int i;

	for(i = 0; i < maxargs; i++){
		if(multiflag)
			while(*s && utfrune(qsep, *s))
				s++;
		else if(*s && utfrune(qsep, *s))
			s++;
		if(*s == 0)
			break;
		args[i] = s;
		s = qtoken(s, qsep);
	}
	return i;
}
