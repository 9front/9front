#include "imap4d.h"

static Namedint	flagmap[] =
{
	{"\\Seen",	Fseen},
	{"\\Answered",	Fanswered},
	{"\\Flagged",	Fflagged},
	{"\\Deleted",	Fdeleted},
	{"\\Draft",	Fdraft},
	{"\\Recent",	Frecent},
	{nil,		0}
};

int
storemsg(Box *box, Msg *m, int uids, void *vst)
{
	int f, flags;
	Store *st;

	if(m->expunged)
		return uids;
	st = vst;
	flags = st->flags;

	f = m->flags;
	if(st->sign == '+')
		f |= flags;
	else if(st->sign == '-')
		f &= ~flags;
	else
		f = flags;

	/*
	 * not allowed to change the recent flag
	 */
	f = (f & ~Frecent) | (m->flags & Frecent);
	setflags(box, m, f);

	if(st->op != Stflagssilent){
		m->sendflags = 1;
		box->sendflags = 1;
	}

	return 1;
}

/*
 * update flags & global flag counts in box
 */
void
setflags(Box *box, Msg *m, int f)
{
	if(f == m->flags)
		return;
	box->dirtyimp = 1;
	if((f & Frecent) != (m->flags & Frecent)){
		if(f & Frecent)
			box->recent++;
		else
			box->recent--;
	}
	m->flags = f;
}

void
sendflags(Box *box, int uids)
{
	Msg *m;

	if(!box->sendflags)
		return;

	box->sendflags = 0;
	for(m = box->msgs; m != nil; m = m->next){
		if(!m->expunged && m->sendflags){
			Bprint(&bout, "* %ud FETCH (", m->seq);
			if(uids)
				Bprint(&bout, "uid %ud ", m->uid);
			Bprint(&bout, "FLAGS (");
			writeflags(&bout, m, 1);
			Bprint(&bout, "))\r\n");
			m->sendflags = 0;
		}
	}
}

void
writeflags(Biobuf *b, Msg *m, int recentok)
{
	char *sep;
	int f;

	sep = "";
	for(f = 0; flagmap[f].name != nil; f++){
		if((m->flags & flagmap[f].v)
		&& (flagmap[f].v != Frecent || recentok)){
			Bprint(b, "%s%s", sep, flagmap[f].name);
			sep = " ";
		}
	}
}

int
msgseen(Box *box, Msg *m)
{
	if(m->flags & Fseen)
		return 0;
	m->flags |= Fseen;
	box->sendflags = 1;
	m->sendflags = 1;
	box->dirtyimp = 1;
	return 1;
}

uint
mapflag(char *name)
{
	return mapint(flagmap, name);
}
