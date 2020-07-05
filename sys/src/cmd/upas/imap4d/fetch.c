#include "imap4d.h"

char *fetchpartnames[FPmax] =
{
	"",
	"HEADER",
	"HEADER.FIELDS",
	"HEADER.FIELDS.NOT",
	"MIME",
	"TEXT",
};

/*
 * implicitly set the \seen flag.  done in a separate pass
 * so the .imp file doesn't need to be open while the
 * messages are sent to the client.
 */
int
fetchseen(Box *box, Msg *m, int uids, void *vf)
{
	Fetch *f;

	if(m->expunged)
		return uids;
	for(f = vf; f != nil; f = f->next){
		switch(f->op){
		case Frfc822:
		case Frfc822text:
		case Fbodysect:
			msgseen(box, m);
			return 1;
		}
	}
	return 1;
}

/*
 * fetch messages
 *
 * imap4 body[] requests get translated to upas/fs files as follows
 *	body[id.header] == id/rawheader file + extra \r\n
 *	body[id.text] == id/rawbody
 *	body[id.mime] == id/mimeheader + extra \r\n
 *	body[id] === body[id.header] + body[id.text]
*/
int
fetchmsg(Box *, Msg *m, int uids, void *vf)
{
	char *sep;
	Fetch *f;
	Tm tm;

	if(m->expunged)
		return uids;
	for(f = vf; f != nil; f = f->next)
		switch(f->op){
		case Fflags:
			break;
		case Fuid:
			break;
		case Finternaldate:
		case Fenvelope:
		case Frfc822:
		case Frfc822head:
		case Frfc822text:
		case Frfc822size:
		case Fbodysect:
		case Fbodypeek:
		case Fbody:
		case Fbodystruct:
			if(!msgstruct(m, 1)){
				msgdead(m);
				return uids;
			}
			break;
		default:
			bye("bad implementation of fetch");
			return 0;
		}
	if(m->expunged)
		return uids;
	if(vf == 0)
		return 1;

	/*
	 * note: it is allowed to send back the responses one at a time
	 * rather than all together.  this is exploited to send flags elsewhere.
	 */
	Bprint(&bout, "* %ud FETCH (", m->seq);
	sep = "";
	if(uids){
		Bprint(&bout, "UID %ud", m->uid);
		sep = " ";
	}
	for(f = vf; f != nil; f = f->next){
		switch(f->op){
		default:
			bye("bad implementation of fetch");
			break;
		case Fflags:
			Bprint(&bout, "%sFLAGS (", sep);
			writeflags(&bout, m, 1);
			Bprint(&bout, ")");
			break;
		case Fuid:
			if(uids)
				continue;
			Bprint(&bout, "%sUID %ud", sep, m->uid);
			break;
		case Fenvelope:
			Bprint(&bout, "%sENVELOPE ", sep);
			fetchenvelope(m);
			break;
		case Finternaldate:
			Bprint(&bout, "%sINTERNALDATE %#D", sep, date2tm(&tm, m->info[Iunixdate]));
			break;
		case Fbody:
			Bprint(&bout, "%sBODY ", sep);
			fetchbodystruct(m, &m->head, 0);
			break;
		case Fbodystruct:
			Bprint(&bout, "%sBODYSTRUCTURE ", sep);
			fetchbodystruct(m, &m->head, 1);
			break;
		case Frfc822size:
			Bprint(&bout, "%sRFC822.SIZE %ud", sep, msgsize(m));
			break;
		case Frfc822:
			f->part = FPall;
			Bprint(&bout, "%sRFC822", sep);
			fetchbody(m, f);
			break;
		case Frfc822head:
			f->part = FPhead;
			Bprint(&bout, "%sRFC822.HEADER", sep);
			fetchbody(m, f);
			break;
		case Frfc822text:
			f->part = FPtext;
			Bprint(&bout, "%sRFC822.TEXT", sep);
			fetchbody(m, f);
			break;
		case Fbodysect:
		case Fbodypeek:
			Bprint(&bout, "%sBODY", sep);
			fetchbody(fetchsect(m, f), f);
			break;
		}
		sep = " ";
	}
	Bprint(&bout, ")\r\n");

	return 1;
}

/*
 * print out section, part, headers;
 * find and return message section
 */
Msg *
fetchsect(Msg *m, Fetch *f)
{
	Bputc(&bout, '[');
	Bnlist(&bout, f->sect, ".");
	if(f->part != FPall){
		if(f->sect != nil)
			Bputc(&bout, '.');
		Bprint(&bout, "%s", fetchpartnames[f->part]);
		if(f->hdrs != nil){
			Bprint(&bout, " (");
			Bslist(&bout, f->hdrs, " ");
			Bputc(&bout, ')');
		}
	}
	Bprint(&bout, "]");
	return findmsgsect(m, f->sect);
}

/*
 * actually return the body pieces
 */
void
fetchbody(Msg *m, Fetch *f)
{
	char *s, *t, *e, buf[Bufsize + 2];
	uint start, stop, pos;
	int fd, n, nn;
	Pair p;

	if(m == nil){
		fetchbodystr(f, "", 0);
		return;
	}
	switch(f->part){
	case FPheadfields:
	case FPheadfieldsnot:
		n = m->head.size + 3;
		s = emalloc(n);
		n = selectfields(s, n, m->head.buf, f->hdrs, f->part == FPheadfields);
		fetchbodystr(f, s, n);
		free(s);
		return;
	case FPhead:
//ilog("head.size %d", m->head.size);
		fetchbodystr(f, m->head.buf, m->head.size);
		return;
	case FPmime:
		fetchbodystr(f, m->mime.buf, m->mime.size);
		return;
	case FPall:
		fd = msgfile(m, "rawbody");
		if(fd < 0){
			msgdead(m);
			fetchbodystr(f, "", 0);
			return;
		}
		p = fetchbodypart(f, msgsize(m));
		start = p.start;
//ilog("head.size %d", m->head.size);
		if(start < m->head.size){
			stop = p.stop;
			if(stop > m->head.size)
				stop = m->head.size;
//ilog("fetch header %ld.%ld (%ld)", start, stop, m->head.size);
			Bwrite(&bout, m->head.buf + start, stop - start);
			start = 0;
			stop = p.stop;
			if(stop <= m->head.size){
				close(fd);
				return;
			}
		}else
			start -= m->head.size;
		stop = p.stop - m->head.size;
		break;
	case FPtext:
		fd = msgfile(m, "rawbody");
		if(fd < 0){
			msgdead(m);
			fetchbodystr(f, "", 0);
			return;
		}
		p = fetchbodypart(f, m->size);
		start = p.start;
		stop = p.stop;
		break;
	default:
		fetchbodystr(f, "", 0);
		return;
	}

	/*
	 * read in each block, convert \n without \r to \r\n.
	 * this means partial fetch requires fetching everything
	 * through stop, since we don't know how many \r's will be added
	 */
	buf[0] = ' ';
	for(pos = 0; pos < stop; ){
		n = Bufsize;
		if(n > stop - pos)
			n = stop - pos;
		n = read(fd, &buf[1], n);
//ilog("read %ld at %d stop %ld\n", n, pos, stop);
		if(n <= 0){
//ilog("must fill %ld bytes\n", stop - pos);
			fetchbodyfill(stop - pos);
			break;
		}
		e = &buf[n + 1];
		*e = 0;
		for(s = &buf[1]; s < e && pos < stop; s = t + 1){
			t = memchr(s, '\n', e - s);
			if(t == nil)
				t = e;
			n = t - s;
			if(pos < start){
				if(pos + n <= start){
					s = t;
					pos += n;
				}else{
					s += start - pos;
					pos = start;
				}
				n = t - s;
			}
			nn = n;
			if(pos + nn > stop)
				nn = stop - pos;
			if(Bwrite(&bout, s, nn) != nn)
				writeerr();
//ilog("w %ld at %ld->%ld stop %ld\n", nn, pos, pos + nn, stop);
			pos += n;
			if(*t == '\n'){
				if(t[-1] != '\r'){
					if(pos >= start && pos < stop)
						Bputc(&bout, '\r');
					pos++;
				}
				if(pos >= start && pos < stop)
					Bputc(&bout, '\n');
				pos++;
			}
		}
		buf[0] = e[-1];
	}
	close(fd);
}

/*
 * resolve the actual bounds of any partial fetch,
 * and print out the bounds & size of string returned
 */
Pair
fetchbodypart(Fetch *f, uint size)
{
	uint start, stop;
	Pair p;

	start = 0;
	stop = size;
	if(f->partial){
		start = f->start;
		if(start > size)
			start = size;
		stop = start + f->size;
		if(stop > size)
			stop = size;
		Bprint(&bout, "<%ud>", start);
	}
	Bprint(&bout, " {%ud}\r\n", stop - start);
	p.start = start;
	p.stop = stop;
	return p;
}

/*
 * something went wrong fetching data
 * produce fill bytes for what we've committed to produce
 */
void
fetchbodyfill(uint n)
{
	while(n-- > 0)
		if(Bputc(&bout, ' ') < 0)
			writeerr();
}

/*
 * return a simple string
 */
void
fetchbodystr(Fetch *f, char *buf, uint size)
{
	Pair p;

	p = fetchbodypart(f, size);
	Bwrite(&bout, buf + p.start, p.stop - p.start);
}

char*
printnlist(Nlist *sect)
{
	static char buf[100];
	char *p;

	for(p = buf; sect; sect = sect->next){
		p += sprint(p, "%ud", sect->n);
		if(sect->next)
			*p++ = '.';
	}
	*p = 0;
	return buf;
}

/*
 * find the numbered sub-part of the message
 */
Msg*
findmsgsect(Msg *m, Nlist *sect)
{
	uint id;

	for(; sect != nil; sect = sect->next){
		id = sect->n;
		for(m = m->kids; m != nil; m = m->next)
			if(m->id == id)
				break;
		if(m == nil)
			return nil;
	}
	return m;
}

void
fetchenvelope(Msg *m)
{
	Tm tm;

	Bprint(&bout, "(%#D %Z ", date2tm(&tm, m->info[Idate]), m->info[Isubject]);
	Bimapaddr(&bout, m->from);
	Bputc(&bout, ' ');
	Bimapaddr(&bout, m->sender);
	Bputc(&bout, ' ');
	Bimapaddr(&bout, m->replyto);
	Bputc(&bout, ' ');
	Bimapaddr(&bout, m->to);
	Bputc(&bout, ' ');
	Bimapaddr(&bout, m->cc);
	Bputc(&bout, ' ');
	Bimapaddr(&bout, m->bcc);
	Bprint(&bout, " %Z %Z)", m->info[Iinreplyto], m->info[Imessageid]);
}

static int
Bmime(Biobuf *b, Mimehdr *mh)
{
	char *sep;

	if(mh == nil)
		return Bprint(b, "NIL");
	sep = "(";
	for(; mh != nil; mh = mh->next){
		Bprint(b, "%s%Z %Z", sep, mh->s, mh->t);
		sep = " ";
	}
	Bputc(b, ')');
	return 0;
}

static void
fetchext(Biobuf *b, Header *h)
{
	Bputc(b, ' ');
	if(h->disposition != nil){
		Bprint(b, "(%Z ", h->disposition->s);
		Bmime(b, h->disposition->next);
		Bputc(b, ')');
	}else
		Bprint(b, "NIL");
	Bputc(b, ' ');
	if(h->language != nil){
		if(h->language->next != nil)
			Bmime(b, h->language->next);
		else
			Bprint(&bout, "%Z", h->language->s);
	}else
		Bprint(b, "NIL");
}

void
fetchbodystruct(Msg *m, Header *h, int extensions)
{
	uint len;
	Msg *k;

	if(msgismulti(h)){
		Bputc(&bout, '(');
		for(k = m->kids; k != nil; k = k->next)
			fetchbodystruct(k, &k->mime, extensions);
		if(m->kids)
			Bputc(&bout, ' ');
		Bprint(&bout, "%Z", h->type->t);
		if(extensions){
			Bputc(&bout, ' ');
			Bmime(&bout, h->type->next);
			fetchext(&bout, h);
		}

		Bputc(&bout, ')');
		return;
	}

	Bputc(&bout, '(');
	if(h->type != nil){
		Bprint(&bout, "%Z %Z ", h->type->s, h->type->t);
		Bmime(&bout, h->type->next);
	}else
		Bprint(&bout, "\"text\" \"plain\" NIL");

	Bputc(&bout, ' ');
	if(h->id != nil)
		Bprint(&bout, "%Z", h->id->s);
	else
		Bprint(&bout, "NIL");

	Bputc(&bout, ' ');
	if(h->description != nil)
		Bprint(&bout, "%Z", h->description->s);
	else
		Bprint(&bout, "NIL");

	Bputc(&bout, ' ');
	if(h->encoding != nil)
		Bprint(&bout, "%Z", h->encoding->s);
	else
		Bprint(&bout, "NIL");

	/*
	 * this is so strange: return lengths for a body[text] response,
	 * except in the case of a multipart message, when return lengths for a body[] response
	 */
	len = m->size;
	if(h == &m->mime)
		len += m->head.size;
	Bprint(&bout, " %ud", len);

	len = m->lines;
	if(h == &m->mime)
		len += m->head.lines;

	if(h->type == nil || cistrcmp(h->type->s, "text") == 0)
		Bprint(&bout, " %ud", len);
	else if(msgis822(h)){
		Bputc(&bout, ' ');
		k = m;
		if(h != &m->mime)
			k = m->kids;
		if(k == nil)
			Bprint(&bout, "(NIL NIL NIL NIL NIL NIL NIL NIL NIL NIL) (\"text\" \"plain\" NIL NIL NIL NIL 0 0) 0");
		else{
			fetchenvelope(k);
			Bputc(&bout, ' ');
			fetchbodystruct(k, &k->head, extensions);
			Bprint(&bout, " %ud", len);
		}
	}

	if(extensions){
		Bprint(&bout, " NIL");	/* md5 */
		fetchext(&bout, h);
	}
	Bputc(&bout, ')');
}

/*
 * print a list of addresses;
 * each address is printed as '(' personalname atdomainlist mboxname hostname ')'
 * the atdomainlist is always NIL
 */
int
Bimapaddr(Biobuf *b, Maddr *a)
{
	char *host, *sep;

	if(a == nil)
		return Bprint(b, "NIL");
	Bputc(b, '(');
	sep = "";
	for(; a != nil; a = a->next){
		/*
		 * can't send NIL as hostname, since that is code for a group
		 */
		host = a->host? a->host: "";
		Bprint(b, "%s(%Z NIL %Z %Z)", sep, a->personal, a->box, host);
		sep = " ";
	}
	return Bputc(b, ')');
}
