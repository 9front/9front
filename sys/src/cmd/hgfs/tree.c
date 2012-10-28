#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

char*
nodepath(char *s, char *e, Revnode *nd, int mangle)
{
	static char *frogs[] = {
		"con", "prn", "aux", "nul",
		"com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
		"lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
	};
	char *p;
	int i, l;

	if(nd == nil || nd->name == nil)
		return s;

	s = seprint(nodepath(s, e, nd->up, mangle), e, "/");
	p = nd->name;
	if(!mangle)
		return seprint(s, e, "%s", p);

	for(i=0; i<nelem(frogs); i++){
		l = strlen(frogs[i]);
		if((strncmp(frogs[i], p, l) == 0) && (p[l] == 0 || p[l] == '.'))
			return seprint(s, e, "%.2s~%.2x%s", p, (uchar)p[2], p+3);
	}
	for(; s+4 < e && *p; p++){
		if(*p == '_'){
			*s++ = '_';
			*s++ = '_';
		} else if(*p >= 'A' && *p <= 'Z'){
			*s++ = '_';
			*s++ = 'a' + (*p - 'A');
		} else if((uchar)*p >= 126 || strchr("\\:*?\"<>|", *p)){
			*s++ = '~';
			s = seprint(s, e, "%.2x", (uchar)*p);
		} else
			*s++ = *p;
	}
	*s = 0;

	return s;
}

Revnode*
mknode(char *name, uchar *hash, char mode)
{
	Revnode *d;
	char *s;

	d = malloc(sizeof(*d) + (hash ? HASHSZ : 0) + (name ? strlen(name)+1 : 0));
	memset(d, 0, sizeof(*d));
	s = (char*)&d[1];
	if(hash){
		d->path = hash2qid(hash);
		memmove(d->hash = (uchar*)s, hash, HASHSZ);
		s += HASHSZ;
	}else
		d->path = 1;
	if(name)
		strcpy(d->name = s, name);
	d->mode = mode;
	return d;
}

static void
addnode(Revnode *d, char *path, uchar *hash, char mode)
{
	char *slash;
	Revnode *c, *p;

	while(path && *path){
		if(slash = strchr(path, '/'))
			*slash++ = 0;
		p = nil;
		for(c = d->down; c; p = c, c = c->next)
			if(strcmp(c->name, path) == 0)
				break;
		if(c == nil){
			c = mknode(path, slash ? nil : hash, slash ? 0 : mode);
			c->up = d;
			if(p){
				c->next = p->next;
				p->next = c;
			} else {
				c->next = d->down;
				d->down = c;
			}
			if(!slash){
				p = c;
				while(d){
					d->path += p->path;
					p = d;
					d = d->up;
				}
			}
		}
		d = c;
		path = slash;
	}
}

typedef struct Hashstr Hashstr;
struct Hashstr
{
	Hashstr	*next;
	char	str[];
};

static int
loadmanifest(Revnode *root, int fd, Hashstr **ht, int nh)
{
	char buf[BUFSZ], *p, *e, *x;
	uchar hash[HASHSZ];
	int n;

	p = buf;
	e = buf + BUFSZ;
	while((n = read(fd, p, e - p)) > 0){
		p += n;
		while((p > buf) && (e = memchr(buf, '\n', p - buf))){
			*e++ = 0;

			x = buf;
			x += strlen(x) + 1;
			hex2hash(x, hash);
			x += HASHSZ*2;

			if(ht == nil)
				addnode(root, buf, hash, *x);
			else {
				Hashstr *he;

				for(he = ht[hashstr(buf) % nh]; he; he = he->next){
					if(strcmp(he->str, buf) == 0){
						addnode(root, buf, hash, *x);
						break;
					}
				}
			}

			p -= e - buf;
			if(p > buf)
				memmove(buf, e, p - buf);
		}
		e = buf + BUFSZ;
	}
	return 0;
}

static Revtree*
loadtree(Revlog *manifest, Revinfo *ri, Hashstr **ht, int nh)
{
	Revtree *t;
	int fd;

	if((fd = revlogopentemp(manifest, hashrev(manifest, ri->mhash))) < 0)
		return nil;

	t = malloc(sizeof(*t));
	memset(t, 0, sizeof(*t));
	incref(t);
	t->root = mknode(nil, nil, 0);
	if(loadmanifest(t->root, fd, ht, nh) < 0){
		close(fd);
		closerevtree(t);
		return nil;
	}
	close(fd);

	return t;
}

Revtree*
loadfilestree(Revlog *, Revlog *manifest, Revinfo *ri)
{
	return loadtree(manifest, ri, nil, 0);
}

Revtree*
loadchangestree(Revlog *changelog, Revlog *manifest, Revinfo *ri)
{
	char buf[BUFSZ], *p, *e;
	Hashstr *ht[256], *he, **hp;
	int fd, n;
	Revtree *t;
	vlong off;

	if((fd = revlogopentemp(changelog, hashrev(changelog, ri->chash))) < 0)
		return nil;

	off = seek(fd, ri->logoff, 0);
	if(off < 0){
		close(fd);
		return nil;
	}

	memset(ht, 0, sizeof(ht));

	p = buf;
	e = buf + BUFSZ;
	while((off - ri->logoff) < ri->loglen){
		if((n = read(fd, p, e - p)) <= 0)
			break;
		p += n;
		while((p > buf) && (e = memchr(buf, '\n', p - buf))){
			*e++ = 0;

			he = malloc(sizeof(*he) + strlen(buf)+1);
			hp = &ht[hashstr(strcpy(he->str, buf)) % nelem(ht)];
			he->next = *hp;
			*hp = he;

			n = e - buf;
			p -= n;
			if(p > buf)
				memmove(buf, e, p - buf);
			off += n;
		}
		e = buf + BUFSZ;
	}
	close(fd);

	t = loadtree(manifest, ri, ht, nelem(ht));

	for(hp = ht; hp != &ht[nelem(ht)]; hp++){
		while(he = *hp){
			*hp = he->next;
			free(he);
		}
	}

	return t;
}

static void
freenode(Revnode *nd)
{
	if(nd == nil)
		return;
	freenode(nd->down);
	freenode(nd->next);
	freenode(nd->before);
	free(nd);
}

void
closerevtree(Revtree *t)
{
	if(t == nil || decref(t))
		return;
	freenode(t->root);
	free(t);
}
