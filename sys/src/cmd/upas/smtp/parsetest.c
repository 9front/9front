#include <u.h>
#include <libc.h>
#include <String.h>
#include <bio.h>
#include "smtp.h"

Biobuf o;

void
freefields(void)
{
	Field *f, *fn;
	Node *n, *nn;

	for(f = firstfield; f != nil; f = fn){
		fn = f->next;
		for(n = f->node; n != nil; n = nn){
			nn = n->next;
			s_free(n->s);
			s_free(n->white);
			free(n);
		}
		free(f);
	}
	firstfield = nil;
}

void
printhdr(void)
{
	Field *f;
	Node *n;

	for(f = firstfield; f != nil; f = f->next){
		for(n = f->node; n != nil; n = n->next){
			if(n->s != nil)
				Bprint(&o, "%s", s_to_c(n->s));
			else
				Bprint(&o, "%c", n->c);
			if(n->white != nil)
				Bprint(&o, "%s", s_to_c(n->white));
		}
		Bprint(&o, "\n");
	}
}

void
usage(void)
{
	fprint(2, "usage: parsetest file ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, fd, nbuf;
	char *buf;

	ARGBEGIN{
	default:
		usage();
	}ARGEND

	if(Binit(&o, 1, OWRITE) == -1)
		sysfatal("Binit: %r");
	for(i = 0; i < argc; i++){
		fd = open(argv[i], OREAD);
		if(fd == -1)
			sysfatal("open: %r");
		buf = malloc(128*1024);
		if(buf == nil)
			sysfatal("malloc: %r");
		nbuf = read(fd, buf, 128*1024);
		if(nbuf == -1)
			sysfatal("read: %r");
		close(fd);
		yyinit(buf, nbuf);
		yyparse();
		printhdr();
		freefields();
		free(buf);
		Bflush(&o);
	}
	exits("");
}
