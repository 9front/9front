#include "common.h"
#include <libsec.h>
#include "dat.h"

#define messageof(p)	((Message*)(((uchar*)&(p)->digest) - offsetof(Message, digest)))

static int
mtreecmp(Avl *va, Avl *vb)
{
	return memcmp(((Mtree*)va)->digest, ((Mtree*)vb)->digest, SHA1dlen);
}

void
mtreeinit(Mailbox *mb)
{
	mb->mtree = avlcreate(mtreecmp);
}

void
mtreefree(Mailbox *mb)
{
	free(mb->mtree);
	mb->mtree = nil;
}

Message*
mtreefind(Mailbox *mb, uchar *digest)
{
	Mtree t, *p;

	t.digest = digest;
	if((p = (Mtree*)avllookup(mb->mtree, &t, 0)) == nil)
		return nil;
	return messageof(p);
}

Message*
mtreeadd(Mailbox *mb, Message *m)
{
	Mtree *old;

	assert(Topmsg(mb, m) && m->digest != nil);
	if((old = (Mtree*)avlinsert(mb->mtree, m)) == nil)
		return nil;
	return messageof(old);
}

void
mtreedelete(Mailbox *mb, Message *m)
{
	Mtree *old;

	assert(Topmsg(mb, m));
	if(m->digest == nil)
		return;
	if(m->deleted & ~Deleted){
		old = (Mtree*)avllookup(mb->mtree, m, 0);
		if(old == nil || messageof(old) != m)
			return;
	}
	old = (Mtree*)avldelete(mb->mtree, m);
	assert(messageof(old) == m);
}
