#include <u.h>
#include <libc.h>
#include "hash.h"

typedef struct Hnode Hnode;
struct Hnode {
	int filled;
	int next;
	void *key;
};

enum{
	Tagsize = sizeof(Hnode),
};

uvlong
shash(char *s)
{
	uvlong hash;

	hash = 7;
	for(; *s; s++)
		hash = hash*31  + *s;
	return hash;
}

Hmap*
hmapalloc(int nbuckets, int size)
{
	void *store;
	Hmap *h;
	int nsz;

	nsz = Tagsize + size;
	store = mallocz(sizeof(*h) + (nbuckets * nsz), 1);
	if(store == nil)
		sysfatal("hmapalloc: out of memory");

	h = store;
	h->nbs = nbuckets;
	h->nsz = nsz;
	h->len = h->cap = nbuckets;

	h->nodes = store;
	h->nodes += sizeof(*h);
	return store;
}

int
hmaprepl(Hmap **store, char *key, void *new, void *old, int freekeys)
{
	Hnode *n;
	uchar *v;
	uchar *oldv;
	Hmap *h;
	int next;
	vlong diff;

	h = *store;
	oldv = nil;
	v = h->nodes + (shash(key)%h->nbs) * h->nsz;
	for(;;){
		n = (Hnode*)v;
		next = n->next;

		if(n->filled == 0)
			goto replace;
		if(strcmp(n->key, key) == 0){
			if(freekeys)
				free(n->key);
			oldv = v + Tagsize;
			goto replace;
		}
		if(next == 0)
			break;
		v = h->nodes + next*h->nsz;
	}

	if(h->cap == h->len){
		/* figure out way back from a relocation */
		diff = v - h->nodes;

		h->cap *= 2;
		*store = realloc(*store, sizeof(*h) + h->cap*h->nsz);
		if(*store == nil)
			sysfatal("hmaprepl: out of memory");
		h = *store;
		h->nodes = (uchar*)*store + sizeof(*h);
		memset(h->nodes + h->len*h->nsz, 0, h->nsz);

		v = h->nodes + diff;
		n = (Hnode*)v;
	}
	n->next = h->len;
	h->len++;
	assert(h->len <= h->cap);
	v = h->nodes + n->next*h->nsz;
	n = (Hnode*)v;

replace:
	memmove(v + Tagsize, new, h->nsz - Tagsize);
	n->filled++;
	n->key = key;
	n->next = next;
	if(old != nil && oldv != nil){
		memmove(old, oldv, h->nsz - Tagsize);
		return 1;
	}
	return 0;
}

int
hmapupd(Hmap **h, char *key, void *new)
{
	char *prev;

	prev = hmapkey(*h, key);
	if(prev == nil)
		prev = key;

	return hmaprepl(h, prev, new, nil, 0);
}

void*
_hmapget(Hmap *h, char *key)
{
	Hnode *n;
	uchar *v;

	v = h->nodes + (shash(key)%h->nbs)*h->nsz;
	for(;;){
		n = (Hnode*)v;
		if(n->filled != 0 && strcmp(n->key, key) == 0)
			return v;
		if(n->next == 0)
			break;
		v = h->nodes + n->next*h->nsz;
	}
	return nil;
}

int
hmapget(Hmap *h, char *key, void *dst)
{
	uchar *v;

	v = _hmapget(h, key);
	if(v == nil)
		return -1;
	if(dst != nil)
		memmove(dst, v + Tagsize, h->nsz - Tagsize);
	return 0;
}

int
hmapdel(Hmap *h, char *key, void *dst, int freekey)
{
	uchar *v;
	Hnode *n;

	v = _hmapget(h, key);
	if(v == nil)
		return -1;

	n = (Hnode*)v;
	n->filled = 0;
	if(freekey)
		free(n->key);
	if(dst != nil)
		memmove(dst, v + Tagsize, h->nsz - Tagsize);
	return 0;
}

char*
hmapkey(Hmap *h, char *key)
{
	uchar *v;
	Hnode *n;

	v = _hmapget(h, key);
	if(v == nil)
		return nil;

	n = (Hnode*)v;
	return n->key;
}

Hmap*
hmaprehash(Hmap *old, int buckets)
{
	int i;
	uchar *v;
	Hnode *n;
	Hmap *new;

	if(buckets == 0)
		buckets = old->len;

	new = hmapalloc(buckets, old->nsz - Tagsize);
	for(i=0 ; i < old->len; i++){
		v = old->nodes + i*old->nsz;
		n = (Hnode*)v;
		hmaprepl(&new, n->key, v + Tagsize, nil, 0);
	}
	free(old);
	return new;
}

void
hmapreset(Hmap *h, int freekeys)
{
	Hnode *n;
	uchar *v;
	int i;

	for(i=0; i < h->len; i++){
		v = h->nodes + i*h->nsz;
		n = (Hnode*)v;
		if(n->filled == 0)
			continue;
		if(freekeys)
			free(n->key);
		n->filled = 0;
	}
	h->len = 0;
}
