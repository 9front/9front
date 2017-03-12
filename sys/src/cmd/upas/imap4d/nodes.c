#include "imap4d.h"

int
inmsgset(Msgset *ms, uint id)
{
	for(; ms; ms = ms->next)
		if(ms->from <= id && ms->to >= id)
			return 1;
	return 0;
}

/*
 * we can't rely on uids being in order, but short-circuting saves us
 * very little.  we have a few tens of thousands of messages at most.
 * also use the msg list as the outer loop to avoid 1:5,3:7 returning
 * duplicates.  this is allowed, but silly.  and could be a problem for
 * internal uses that aren't idempotent, like (re)moving messages.
 */
static int
formsgsu(Box *box, Msgset *s, uint max, int (*f)(Box*, Msg*, int, void*), void *rock)
{
	int ok;
	Msg *m;
	Msgset *ms;

	ok = 1;
	for(m = box->msgs; m != nil && m->seq <= max; m = m->next)
		for(ms = s; ms != nil; ms = ms->next)
			if(m->uid >= ms->from && m->uid <= ms->to){
				if(!f(box, m, 1, rock))
					ok = 0;
				break;
			}
	return ok;
}

int
formsgsi(Box *box, Msgset *ms, uint max, int (*f)(Box*, Msg*, int, void*), void *rock)
{
	int ok, rok;
	uint id;
	Msg *m;

	ok = 1;
	for(; ms != nil; ms = ms->next){
		id = ms->from;
		rok = 0;
		for(m = box->msgs; m != nil && m->seq <= max; m = m->next){
			if(m->seq > id)
				break;	/* optimization */
			if(m->seq == id){
				if(!f(box, m, 0, rock))
					ok = 0;
				if(id >= ms->to){
					rok = 1;
					break;	/* optimization */
				}
				if(ms->to == ~0UL)
					rok = 1;
				id++;
			}
		}
		if(!rok)
			ok = 0;
	}
	return ok;
}

/*
 * iterated over all of the items in the message set.
 * errors are accumulated, but processing continues.
 * if uids, then ignore non-existent messages.
 * otherwise, that's an error.  additional note from the
 * rfc:
 *
 * “Servers MAY coalesce overlaps and/or execute the
 * sequence in any order.”
 */
int
formsgs(Box *box, Msgset *ms, uint max, int uids, int (*f)(Box*, Msg*, int, void*), void *rock)
{
	if(uids)
		return formsgsu(box, ms, max, f, rock);
	else
		return formsgsi(box, ms, max, f, rock);
}

Store*
mkstore(int sign, int op, int flags)
{
	Store *st;

	st = binalloc(&parsebin, sizeof *st, 1);
	if(st == nil)
		parseerr("out of memory");
	st->sign = sign;
	st->op = op;
	st->flags = flags;
	return st;
}

Fetch *
mkfetch(int op, Fetch *next)
{
	Fetch *f;

	f = binalloc(&parsebin, sizeof *f, 1);
	if(f == nil)
		parseerr("out of memory");
	f->op = op;
	f->next = next;
	return f;
}

Fetch*
revfetch(Fetch *f)
{
	Fetch *last, *next;

	last = nil;
	for(; f != nil; f = next){
		next = f->next;
		f->next = last;
		last = f;
	}
	return last;
}

Slist*
mkslist(char *s, Slist *next)
{
	Slist *sl;

	sl = binalloc(&parsebin, sizeof *sl, 0);
	if(sl == nil)
		parseerr("out of memory");
	sl->s = s;
	sl->next = next;
	return sl;
}

Slist*
revslist(Slist *sl)
{
	Slist *last, *next;

	last = nil;
	for(; sl != nil; sl = next){
		next = sl->next;
		sl->next = last;
		last = sl;
	}
	return last;
}

int
Bnlist(Biobuf *b, Nlist *nl, char *sep)
{
	char *s;
	int n;

	s = "";
	n = 0;
	for(; nl != nil; nl = nl->next){
		n += Bprint(b, "%s%ud", s, nl->n);
		s = sep;
	}
	return n;
}

int
Bslist(Biobuf *b, Slist *sl, char *sep)
{
	char *s;
	int n;

	s = "";
	n = 0;
	for(; sl != nil; sl = sl->next){
		n += Bprint(b, "%s%Z", s, sl->s);
		s = sep;
	}
	return n;
}
