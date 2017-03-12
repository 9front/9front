#include "common.h"
#include "send.h"

#define isspace(c) ((c)==' ' || (c)=='\t' || (c)=='\n')

static int
okfile(char *s, Biobuf *b)
{
	char *buf, *p, *e;
	int len, c;

	len = strlen(s);
	Bseek(b, 0, 0);
	
	/* one iteration per system name in the file */
	while(buf = Brdline(b, '\n')) {
		e = buf + Blinelen(b);
		for(p = buf; p < e;){
			while(isspace(*p) || *p==',')
				p++;
			if(strncmp(p, s, len) == 0) {
				c = p[len];
				if(isspace(c) || c==',')
					return 1;
			}
			while(p < e && (!isspace(*p)) && *p!=',')
				p++;
		}
	}
	/* didn't find it, prohibit forwarding */
	return 0;
}

/* return 1 if name found in file
 *	  0 if name not found
 *	  -1 if
 */
static int
lookup(char *s, char *local, Biobuf **b)
{
	char file[Pathlen];

	snprint(file, sizeof file, "%s/%s", UPASLIB, local);
	if(*b != nil || (*b = sysopen(file, "r", 0)) != nil)
		return okfile(s, *b);
	return 0;
}

/*
 *  skip past all systems in equivlist
 */
char*
skipequiv(char *base)
{
	char *sp;
	static Biobuf *fp;

	while(*base){
		sp = strchr(base, '!');
		if(sp==0)
			break;
		*sp = '\0';
		if(lookup(base, "equivlist", &fp)==1){
			/* found or us, forget this system */
			*sp='!';
			base=sp+1;
		} else {
			/* no files or system is not found, and not us */
			*sp='!';
			break;
		}
	}
	return base;
}
