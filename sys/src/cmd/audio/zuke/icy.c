#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include "plist.h"
#include "icy.h"

typedef struct Icyaux Icyaux;

struct Icyaux
{
	Channel *newtitle;
	int outfd;
	int metaint;
};

static long
Breadn(Biobufhdr *bp, void *addr, long nbytes)
{
	long n, r;
	u8int *p;

	for(n = 0, p = addr; n < nbytes; n += r, p += r){
		if((r = Bread(bp, p, nbytes-n)) < 1)
			break;
	}

	return n;
}

static void
icyproc(void *b_)
{
	char *p, *s, *e;
	Biobuf *b, *out;
	int n, r, sz;
	Icyaux *aux;

	threadsetname("icy/pull");
	b = b_;
	aux = b->aux;
	out = Bfdopen(aux->outfd, OWRITE);
	sz = aux->metaint > 4096 ? aux->metaint : 4096;
	p = malloc(sz);
	for(;;){
		r = Breadn(b, p, aux->metaint > 0 ? aux->metaint : sz);
		if(r < 1 || Bwrite(out, p, r) != r)
			break;
		if(aux->metaint > 0){
			if((n = 16*Bgetc(b)) < 0)
				break;
			if(Breadn(b, p, n) != n)
				break;
			p[n] = 0;
			if((s = strstr(p, "StreamTitle='")) != nil && (e = strstr(s+13, "';")) != nil && e != s+13){
				*e = 0;
				if(sendp(aux->newtitle, strdup(s+13)) != 1){
					free(s);
					break;
				}
			}
		}
	}
	free(p);
	Bterm(b);
	Bterm(out);
	chanclose(aux->newtitle);

	threadexits(nil);
}

int
icyget(Meta *m, int outfd, Channel **newtitle)
{
	char *s, *e, *p, *path, *d;
	int f, r, n;
	Icyaux *aux;
	Biobuf *b;

	path = strdup(m->path);
	s = strchr(path, ':')+3;
	if((e = strchr(s, '/')) != nil)
		*e++ = 0;
	if((p = strchr(s, ':')) != nil)
		*p = '!';
	p = smprint("tcp!%s", s);
	free(path);
	f = -1;
	if((d = netmkaddr(p, "tcp", "80")) != nil)
		f = dial(d, nil, nil, nil);
	free(p);
	if(f < 0)
		return -1;
	fprint(f, "GET /%s HTTP/0.9\r\nIcy-MetaData: 1\r\n\r\n", e ? e : "");
	b = Bfdopen(f, OREAD);
	aux = mallocz(sizeof(*aux), 1);
	aux->outfd = outfd;
	aux->newtitle = chancreate(sizeof(char*), 2);
	for(r = -1;;){
		if((s = Brdline(b, '\n')) == nil)
			break;
		if((n = Blinelen(b)) < 2)
			break;
		if(n == 2 && *s == '\r'){ /* eof */
			r = 0;
			break;
		}
		s[n-2] = 0;
		if(strncmp(s, "icy-name:", 9) == 0){
			s += 9;
			if(newtitle != nil)
				sendp(aux->newtitle, strdup(s));
			else if(m->title == nil)
				m->title = strdup(s);
		}else if(newtitle == nil && strncmp(s, "icy-url:", 8) == 0 && m->numartist == 0){
			s += 8;
			m->artist[m->numartist++] = strdup(s);
		}else if(strncmp(s, "icy-metaint:", 12) == 0){
			s += 12;
			aux->metaint = atoi(s);
		}
	}
	if(r < 0 || outfd < 0){
		Bterm(b);
		b = nil;
		free(aux);
	}
	if(b != nil){
		assert(aux->newtitle != nil);
		b->aux = aux;
		*newtitle = aux->newtitle;
		proccreate(icyproc, b, mainstacksize);
	}

	return r;
}
