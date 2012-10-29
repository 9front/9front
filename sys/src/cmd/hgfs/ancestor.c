#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

typedef struct XNode XNode;
struct XNode
{
	XNode	*next;
	XNode	*queue;
	char	mark;
	uchar	hash[HASHSZ];
};

static XNode*
hnode(XNode *ht[], uchar hash[])
{
	XNode *h;

	for(h = ht[hash[0]]; h; h = h->next)
		if(memcmp(h->hash, hash, HASHSZ) == 0)
			return h;

	h = malloc(sizeof(*h));
	memmove(h->hash, hash, HASHSZ);
	h->mark = 0;
	h->queue = nil;
	h->next = ht[hash[0]];
	ht[hash[0]] = h;
	return h;
}

/*
 * find common ancestor revision ahash for xhash and yhash
 * in the give hgfs mount point. sets ahash to nullid if
 * no common ancestor.
 */
void
ancestor(char *mtpt, uchar xhash[], uchar yhash[], uchar ahash[])
{
	XNode *ht[256], *h, *q, *q1, *q2;
	char buf[MAXPATH], rev[6];
	int i;

	if(memcmp(xhash, yhash, HASHSZ) == 0){
		memmove(ahash, xhash, HASHSZ);
		return;
	}
	if(memcmp(xhash, nullid, HASHSZ) == 0){
		memmove(ahash, nullid, HASHSZ);
		return;
	}
	if(memcmp(yhash, nullid, HASHSZ) == 0){
		memmove(ahash, nullid, HASHSZ);
		return;
	}

	memset(ht, 0, sizeof(ht));
	q1 = nil;

	h = hnode(ht, xhash);
	h->mark = 'x';
	h->queue = q1;
	q1 = h;

	h = hnode(ht, yhash);
	h->mark = 'y';
	h->queue = q1;
	q1 = h;

	for(;;){
		q2 = nil;
		while(q = q1){
			q1 = q->queue;
			q->queue = nil;
			snprint(buf, sizeof(buf), "%s/%H", mtpt, q->hash);
			for(i=1; i<=2; i++){
				sprint(rev, "rev%d", i);
				if(readhash(buf, rev, ahash) != 0)
					continue;
				if(memcmp(ahash, nullid, HASHSZ) == 0)
					continue;
				h = hnode(ht, ahash);
				if(h->mark){
					if(h->mark != q->mark)
						goto Done;
				} else {
					h->mark = q->mark;
					h->queue = q2;
					q2 = h;
				}
			}
		}
		if(q2 == nil){
			memmove(ahash, nullid, HASHSZ);
			break;
		}
		q1 = q2;
	}

Done:
	for(i=0; i<nelem(ht); i++)
		while(h = ht[i]){
			ht[i] = h->next;
			free(h);
		}
}
