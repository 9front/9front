#include <u.h>
#include <libc.h>
#include <thread.h>
#include <ctype.h>
#include "dat.h"
#include "fns.h"

static char *pos;
static int lineno;

static void
skipempty(void)
{
	char *s;

	for(;;){
		s = pos;
		if(*s == 0)
			return;
		while(*s != '\n' && isspace(*s))
			s++;
		if(*s == '#')
			while(*s != 0 && *s != '\n')
				s++;
		if(*s != 0 && *s != '\n')
			return;
		pos = s;
		if(*pos != 0){
			pos++;
			lineno++;
		}
	}
}

static void
parsesh(int *argc, char ***argv)
{
	char *e;

	*argc = 0;
	*argv = nil;
	for(;;){
		while(isspace(*pos) && *pos != '\n')
			pos++;
		if(*pos == '\n' || *pos == 0 || *pos == '#')
			break;
		e = pos;
		while(*e != 0 && *e != '#' && !isspace(*e))
			e++;
		(*argc)++;
		*argv = realloc(*argv, (*argc + 2) * sizeof(char *));
		if(*argv == nil)
			sysfatal("realloc: %r");
		(*argv)[*argc - 1] = mallocz(e - pos + 1, 1);
		if((*argv)[*argc - 1] == nil)
			sysfatal("malloc: %r");
		memmove((*argv)[*argc - 1], pos, e - pos);
		pos = e;
	}
	if(*argv != nil){
		(*argv)[*argc] = nil;
		(*argv)[*argc + 1] = nil;
	}
}

static Dev dummy;

struct field {
	char *s;
	void* v;
} fields[] = {
	"class", &dummy.class,
	"vid", &dummy.vid,
	"did", &dummy.did,
	nil, nil,
};

static int
parsecond(Rule *r, Cond **last)
{
	Cond *c, *cc, **l;
	char *e;
	struct field *f;

	skipempty();
	if(!isspace(*pos))
		return 0;
	l = nil;
	for(;;){
		while(isspace(*pos) && *pos != '\n')
			pos++;
		if(*pos == '\n' || *pos == '#')
			return 1;
		e = pos;
		while(*e != 0 && *e != '\n' && *e != '=')
			e++;
		if(*e != '=')
			return -1;
		c = mallocz(sizeof(*c), 1);
		if(c == nil)
			sysfatal("malloc: %r");
		for(f = fields; f->s != nil; f++)
			if(strlen(f->s) == e - pos && strncmp(pos, f->s, e - pos) == 0){
				c->field = (int)((char*)f->v - (char*)&dummy);
				break;
			}
		if(f->s == nil)
			goto Error;
		pos = e + 1;
		c->value = strtol(pos, &e, 0);
		if(pos == e)
			goto Error;
		pos = e;
		if(l != nil)
			*l = c;
		else if(*last){
			for(cc = *last; cc != nil; cc = cc->and)
				cc->or = c;
			*last = c;
		}else
			*last = r->cond = c;
		l = &c->and;
	}
Error:
	free(c);
	return -1;
}

static int
parserule(void)
{
	Rule *r;
	int rc;
	Cond *c;
	
	skipempty();
	if(*pos == 0)
		return 0;
	if(isspace(*pos))
		return -1;
	r = mallocz(sizeof(*r), 1);
	if(r == nil)
		sysfatal("malloc: %r");
	parsesh(&r->argc, &r->argv);
	c = nil;
	do
		rc = parsecond(r, &c);
	while(rc > 0);
	if(rc < 0)
		return -1;
	if(rulefirst != nil)
		rulelast->next = r;
	else
		rulefirst = r;
	rulelast = r;
	return 1;
}

static void
freerules(void)
{
	Rule *r, *rr;
	Cond *c, *cc;
	
	wlock(&rulelock);
	for(r = rulefirst; r != nil; r = rr){
		for(c = r->cond; c != nil; c = cc){
			cc = c->and;
			if(cc == nil)
				cc = c->or;
			free(c);
		}
		rr = r->next;
		free(r);
	}
	rulefirst = rulelast = nil;
	wunlock(&rulelock);
}

static void
printrules(void)
{
	Rule *r;
	Cond *c;
	int i;

	for(r = rulefirst; r != nil; r = r->next){
		for(i = 0; i < r->argc; i++)
			print("[%s] ", r->argv[i]);
		print("\n\t");
		for(c = r->cond; c != nil; ){
			print("%d=%ud", c->field, c->value);
			if(c->and == nil){
				print("\n\t");
				c = c->or;
			}else{
				print(" ");
				c = c->and;
			}
		}
		print("\n");
	}
}

void
parserules(char *s)
{
	int rc;

	freerules();
	lineno = 1;
	pos = s;
	do
		rc = parserule();
	while(rc > 0);
	if(rc < 0)
		sysfatal("syntax error in line %d", lineno);
}

Rule *
rulesmatch(Dev *dev)
{
	Rule *r;
	Cond *c;

	for(r = rulefirst; r != nil; r = r->next){
		c = r->cond;
		while(c){
			if(*(u32int*)((char*)dev + c->field) == c->value){
				if(c->and == nil)
					goto yes;
				c = c->and;
			}else
				c = c->or;
		}
	}
yes:
	return r;
}
