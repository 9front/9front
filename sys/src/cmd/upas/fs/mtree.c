#include "common.h"
#include <libsec.h>
#include "dat.h"

int
mtreecmp(Avl *va, Avl *vb)
{
	Mtree *a, *b;

	a = (Mtree*)va;
	b = (Mtree*)vb;
	return memcmp(a->m->digest, b->m->digest, SHA1dlen);
}

int
mtreeisdup(Mailbox *mb, Message *m)
{
	Mtree t;

	assert(Topmsg(mb, m) && m->digest);
	if(m->digest == nil)
		return 0;
	memset(&t, 0, sizeof t);
	t.m = m;
	if(avllookup(mb->mtree, &t, 0))
		return 1;
	return 0;
}

Message*
mtreefind(Mailbox *mb, uchar *digest)
{
	Message m0;
	Mtree t, *p;

	m0.digest = digest;
	memset(&t, 0, sizeof t);
	t.m = &m0;
	if(p = (Mtree*)avllookup(mb->mtree, &t, 0))
		return p->m;
	return nil;
}

void
mtreeadd(Mailbox *mb, Message *m)
{
	Avl *old;
	Mtree *p;

	assert(Topmsg(mb, m) && m->digest);
	p = emalloc(sizeof *p);
	p->m = m;
	old = avlinsert(mb->mtree, p);
	assert(old == 0);
}

void
mtreedelete(Mailbox *mb, Message *m)
{
	Mtree t, *p;

	assert(Topmsg(mb, m));
	memset(&t, 0, sizeof t);
	t.m = m;
	if(m->deleted & ~Deleted){
		if(m->digest == nil)
			return;
		p = (Mtree*)avllookup(mb->mtree, &t, 0);
		if(p == nil || p->m != m)
			return;
		p = (Mtree*)avldelete(mb->mtree, &t);
		free(p);
		return;
	}
	assert(m->digest);
	p = (Mtree*)avldelete(mb->mtree, &t);
	if(p == nil)
		_assert("mtree delete fails");
	free(p);
}
