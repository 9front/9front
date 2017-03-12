#include "imap4d.h"

static int
filesearch(Msg *m, char *file, char *pat)
{
	char buf[Bufsize + 1];
	int n, nbuf, npat, fd, ok;

	npat = strlen(pat);
	if(npat >= Bufsize/2)
		return 0;

	fd = msgfile(m, file);
	if(fd < 0)
		return 0;
	ok = 0;
	nbuf = 0;
	for(;;){
		n = read(fd, &buf[nbuf], Bufsize - nbuf);
		if(n <= 0)
			break;
		nbuf += n;
		buf[nbuf] = '\0';
		if(cistrstr(buf, pat) != nil){
			ok = 1;
			break;
		}
		if(nbuf > npat){
			memmove(buf, &buf[nbuf - npat], npat);
			nbuf = npat;
		}
	}
	close(fd);
	return ok;
}

static int
headersearch(Msg *m, char *hdr, char *pat)
{
	char *s, *t;
	int ok, n;
	Slist hdrs;

	n = m->head.size + 3;
	s = emalloc(n);
	hdrs.next = nil;
	hdrs.s = hdr;
	ok = 0;
	if(selectfields(s, n, m->head.buf, &hdrs, 1) > 0){
		t = strchr(s, ':');
		if(t != nil && cistrstr(t + 1, pat) != nil)
			ok = 1;
	}
	free(s);
	return ok;
}

static int
addrsearch(Maddr *a, char *s)
{
	char *ok, *addr;

	for(; a != nil; a = a->next){
		addr = maddrstr(a);
		ok = cistrstr(addr, s);
		free(addr);
		if(ok != nil)
			return 1;
	}
	return 0;
}

static int
datecmp(char *date, Search *s)
{
	Tm tm;

	date2tm(&tm, date);
	if(tm.year < s->year)
		return -1;
	if(tm.year > s->year)
		return 1;
	if(tm.mon < s->mon)
		return -1;
	if(tm.mon > s->mon)
		return 1;
	if(tm.mday < s->mday)
		return -1;
	if(tm.mday > s->mday)
		return 1;
	return 0;
}

enum{
	Simp	= 0,
	Sinfo	= 1<<0,
	Sbody	= 1<<2,
};

int
searchld(Search *s)
{
	int r;

	for(r = 0; (r & Sbody) == 0 && s; s = s->next)
	switch(s->key){
	case SKall:
	case SKanswered:
	case SKdeleted:
	case SKdraft:
	case SKflagged:
	case SKkeyword:
	case SKnew:
	case SKold:
	case SKrecent:
	case SKseen:
	case SKunanswered:
	case SKundeleted:
	case SKundraft:
	case SKunflagged:
	case SKunkeyword:
	case SKunseen:
	case SKuid:
	case SKset:
		break;
	case SKlarger:
	case SKsmaller:
	case SKbcc:
	case SKcc:
	case SKfrom:
	case SKto:
	case SKsubject:
	case SKbefore:
	case SKon:
	case SKsince:
	case SKsentbefore:
	case SKsenton:
	case SKsentsince:
		r = Sinfo;
		break;
	case SKheader:
		if(cistrcmp(s->hdr, "message-id") == 0)
			r = Sinfo;
		else
			r = Sbody;
		break;
	case SKbody:
		break;		/* msgstruct doesn't do us any good */
	case SKtext:
	default:
		r = Sbody;
		break;
	case SKnot:
		r = searchld(s->left);
		break;
	case SKor:
		r = searchld(s->left) | searchld(s->right);
		break;
	}
	return 0;
}

/* important speed hack for apple mail */
int
msgidsearch(char *s, char *hdr)
{
	char c;
	int l, r;

	l = strlen(s);
	c = 0;
	if(s[0] == '<' && s[l-1] == '>'){
		l -= 2;
		s += 1;
		c = s[l-1];
	}
	r = hdr && strstr(s, hdr) != nil;
	if(c)
		s[l-1] = c;
	return r;
}

/*
 * free to exit, parseerr, since called before starting any client reply
 *
 * the header and envelope searches should convert mime character set escapes.
 */
int
searchmsg(Msg *m, Search *s, int ld)
{
	uint ok, id;
	Msgset *ms;

	if(m->expunged)
		return 0;
	if(ld & Sbody){
		if(!msgstruct(m, 1))
			return 0;
	}else if (ld & Sinfo){
		if(!msginfo(m))
			return 0;
	}
	for(ok = 1; ok && s != nil; s = s->next){
		switch(s->key){
		default:
			ok = 0;
			break;
		case SKnot:
			ok = !searchmsg(m, s->left, ld);
			break;
		case SKor:
			ok = searchmsg(m, s->left, ld) || searchmsg(m, s->right, ld);
			break;
		case SKall:
			ok = 1;
			break;
		case SKanswered:
			ok = (m->flags & Fanswered) == Fanswered;
			break;
		case SKdeleted:
			ok = (m->flags & Fdeleted) == Fdeleted;
			break;
		case SKdraft:
			ok = (m->flags & Fdraft) == Fdraft;
			break;
		case SKflagged:
			ok = (m->flags & Fflagged) == Fflagged;
			break;
		case SKkeyword:
			ok = (m->flags & s->num) == s->num;
			break;
		case SKnew:
			ok = (m->flags & (Frecent|Fseen)) == Frecent;
			break;
		case SKold:
			ok = (m->flags & Frecent) != Frecent;
			break;
		case SKrecent:
			ok = (m->flags & Frecent) == Frecent;
			break;
		case SKseen:
			ok = (m->flags & Fseen) == Fseen;
			break;
		case SKunanswered:
			ok = (m->flags & Fanswered) != Fanswered;
			break;
		case SKundeleted:
			ok = (m->flags & Fdeleted) != Fdeleted;
			break;
		case SKundraft:
			ok = (m->flags & Fdraft) != Fdraft;
			break;
		case SKunflagged:
			ok = (m->flags & Fflagged) != Fflagged;
			break;
		case SKunkeyword:
			ok = (m->flags & s->num) != s->num;
			break;
		case SKunseen:
			ok = (m->flags & Fseen) != Fseen;
			break;
		case SKlarger:
			ok = msgsize(m) > s->num;
			break;
		case SKsmaller:
			ok = msgsize(m) < s->num;
			break;
		case SKbcc:
			ok = addrsearch(m->bcc, s->s);
			break;
		case SKcc:
			ok = addrsearch(m->cc, s->s);
			break;
		case SKfrom:
			ok = addrsearch(m->from, s->s);
			break;
		case SKto:
			ok = addrsearch(m->to, s->s);
			break;
		case SKsubject:
			ok = cistrstr(m->info[Isubject], s->s) != nil;
			break;
		case SKbefore:
			ok = datecmp(m->info[Iunixdate], s) < 0;
			break;
		case SKon:
			ok = datecmp(m->info[Iunixdate], s) == 0;
			break;
		case SKsince:
			ok = datecmp(m->info[Iunixdate], s) > 0;
			break;
		case SKsentbefore:
			ok = datecmp(m->info[Idate], s) < 0;
			break;
		case SKsenton:
			ok = datecmp(m->info[Idate], s) == 0;
			break;
		case SKsentsince:
			ok = datecmp(m->info[Idate], s) > 0;
			break;
		case SKuid:
			id = m->uid;
			goto set;
		case SKset:
			id = m->seq;
		set:
			for(ms = s->set; ms != nil; ms = ms->next)
				if(id >= ms->from && id <= ms->to)
					break;
			ok = ms != nil;
			break;
		case SKheader:
			if(cistrcmp(s->hdr, "message-id") == 0)
				ok = msgidsearch(s->s, m->info[Imessageid]);
			else
				ok = headersearch(m, s->hdr, s->s);
			break;
		case SKbody:
		case SKtext:
			if(s->key == SKtext && cistrstr(m->head.buf, s->s)){
				ok = 1;
				break;
			}
			ok = filesearch(m, "body", s->s);
			break;
		}
	}
	return ok;
}
