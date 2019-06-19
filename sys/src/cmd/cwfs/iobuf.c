#include	"all.h"
#include	"io.h"

extern	uint nhiob;
extern	Hiob *hiob;

Iobuf*
getbuf(Device *d, Off addr, int flag)
{
	Iobuf *p, *s;
	Hiob *hp;
	Off h;

	if(chatty > 1)
		fprint(2, "getbuf %Z(%lld) f=%x\n", d, (Wideoff)addr, flag);
	h = addr + (Off)(uintptr)d*1009;
	if(h < 0)
		h = ~h;
	h %= nhiob;
	hp = &hiob[h];

loop:
	lock(hp);

/*
 * look for it in the active list
 */
	s = hp->link;
	for(p=s;;) {
		if(p->addr == addr && p->dev == d) {
			if(p != s) {
				p->back->fore = p->fore;
				p->fore->back = p->back;
				p->fore = s;
				p->back = s->back;
				s->back = p;
				p->back->fore = p;
				hp->link = p;
			}
			unlock(hp);
			qlock(p);
			if(p->addr != addr || p->dev != d || iobufmap(p) == 0) {
				qunlock(p);
				goto loop;
			}
			p->flags |= flag;
			return p;
		}
		p = p->fore;
		if(p == s)
			break;
	}
	if(flag & Bprobe) {
		unlock(hp);
		return 0;
	}

/*
 * not found
 * take oldest unlocked entry in this queue
 */
xloop:
	p = s->back;
	if(!canqlock(p)) {
		if(p == hp->link) {
			unlock(hp);
			fprint(2, "iobuf all locked\n");
			goto loop;
		}
		s = p;
		goto xloop;
	}

	/*
	 * its dangerous to flush the pseudo
	 * devices since they recursively call
	 * getbuf/putbuf. deadlock!
	 */
	if(p->flags & Bres) {
		qunlock(p);
		if(p == hp->link) {
			unlock(hp);
			fprint(2, "iobuf all reserved\n");
			goto loop;
		}
		s = p;
		goto xloop;
	}
	if(p->flags & Bmod) {
		unlock(hp);
		if(iobufmap(p)) {
			if(!devwrite(p->dev, p->addr, p->iobuf))
				p->flags &= ~(Bimm|Bmod);
			iobufunmap(p);
		}
		qunlock(p);
		goto loop;
	}
	hp->link = p;
	p->addr = addr;
	p->dev = d;
	p->flags = flag;
	unlock(hp);
	if(iobufmap(p))
		if(flag & Brd) {
			if(!devread(p->dev, p->addr, p->iobuf))
				return p;
			iobufunmap(p);
		} else
			return p;
	else
		fprint(2, "iobuf cant map buffer %Z(%lld)\n", p->dev, (Wideoff)p->addr);
	p->flags = 0;
	p->dev = devnone;
	p->addr = -1;
	qunlock(p);
	return 0;
}

/*
 * syncblock tries to put out a block per hashline
 * returns 0 all done,
 * returns 1 if it missed something
 */
int
syncblock(void)
{
	Iobuf *p, *s, *q;
	Hiob *hp;
	long h;
	int flag;

	flag = 0;
	for(h=0; h<nhiob; h++) {
		q = 0;
		hp = &hiob[h];
		lock(hp);
		s = hp->link;
		for(p=s;;) {
			if(p->flags & Bmod) {
				if(q)
					flag = 1;	/* more than 1 mod/line */
				q = p;
			}
			p = p->fore;
			if(p == s)
				break;
		}
		unlock(hp);
		if(q) {
			if(!canqlock(q)) {
				flag = 1;		/* missed -- was locked */
				continue;
			}
			if(!(q->flags & Bmod)) {
				qunlock(q);
				continue;
			}
			if(iobufmap(q)) {
				if(!devwrite(q->dev, q->addr, q->iobuf))
					q->flags &= ~(Bmod|Bimm);
				iobufunmap(q);
			} else
				flag = 1;
			qunlock(q);
		}
	}
	return flag;
}

void
sync(char *reason)
{
	long i;

	if(chatty)
		fprint(2, "sync: %s\n", reason);
	for(i=10*nhiob; i>0; i--)
		if(!syncblock())
			return;
}

void
putbuf(Iobuf *p)
{

	if(canqlock(p))
		fprint(2, "buffer not locked %Z(%lld)\n", p->dev, (Wideoff)p->addr);
	if(p->flags & Bimm) {
		if(!(p->flags & Bmod))
			fprint(2, "imm and no mod %Z(%lld)\n",
				p->dev, (Wideoff)p->addr);
		if(!devwrite(p->dev, p->addr, p->iobuf))
			p->flags &= ~(Bmod|Bimm);
	}
	iobufunmap(p);
	qunlock(p);
}

int
checktag(Iobuf *p, int tag, Off qpath)
{
	Tag *t;
	uintptr pc;

	t = (Tag*)(p->iobuf+BUFSIZE);
	if(tag != t->tag || qpath != QPNONE && qpath != t->path){
		pc = getcallerpc(&p);

		if(qpath == QPNONE){
			fprint(2, "checktag pc=%p %Z(%llux) tag/path=%G/%llud; expected %G\n",
				pc, p->dev, (Wideoff)p->addr, t->tag, (Wideoff)t->path, tag);
		} else {
			fprint(2, "checktag pc=%p %Z(%llux) tag/path=%G/%llud; expected %G/%llud\n",
				pc, p->dev, (Wideoff)p->addr, t->tag, (Wideoff)t->path, tag, (Wideoff)qpath);
		}
		return 1;
	}	
	return 0;
}

void
settag(Iobuf *p, int tag, Off qpath)
{
	Tag *t;

	t = (Tag*)(p->iobuf+BUFSIZE);
	t->tag = tag;
	if(qpath != QPNONE)
		t->path = qpath;
	p->flags |= Bmod;
}

int
qlmatch(QLock *q1, QLock *q2)
{

	return q1 == q2;
}

int
iobufql(QLock *q)
{
	Iobuf *p, *s;
	Hiob *hp;
	long h;

	for(h=0; h<nhiob; h++) {
		hp = &hiob[h];
		lock(hp);
		s = hp->link;
		for(p=s;;) {
			if(qlmatch(q, p)) {
				unlock(hp);
				return 1;
			}
			p = p->fore;
			if(p == s)
				break;
		}
		unlock(hp);
	}
	return 0;
}
