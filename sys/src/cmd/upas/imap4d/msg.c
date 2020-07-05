#include "imap4d.h"

static	char	*headaddrspec(char*, char*);
static	Maddr	*headaddresses(void);
static	Maddr	*headaddress(void);
static	char	*headatom(char*);
static	int	headchar(int eat);
static	char	*headdomain(char*);
static	Maddr	*headmaddr(Maddr*);
static	char	*headphrase(char*, char*);
static	char	*headquoted(int start, int stop);
static	char	*headskipwhite(int);
static	void	headskip(void);
static	char	*headsubdomain(void);
static	char	*headtext(void);
static	void	headtoend(void);
static	char	*headword(void);
static	void	mimedescription(Header*);
static	void	mimedisposition(Header*);
static	void	mimeencoding(Header*);
static	void	mimeid(Header*);
static	void	mimelanguage(Header*);
//static	void	mimemd5(Header*);
static	void	mimetype(Header*);
static	int	msgbodysize(Msg*);
static	int	msgheader(Msg*, Header*, char*);

/*
 * stop list for header fields
 */
static	char	*headfieldstop = ":";
static	char	*mimetokenstop = "()<>@,;:\\\"/[]?=";
static	char	*headatomstop = "()<>@,;:\\\".[]";
static	uchar	*headstr;
static	uchar	*lastwhite;

long
selectfields(char *dst, long n, char *hdr, Slist *fields, int matches)
{
	char *s;
	uchar *start;
	long m, nf;
	Slist *f;

	headstr = (uchar*)hdr;
	m = 0;
	for(;;){
		start = headstr;
		s = headatom(headfieldstop);
		if(s == nil)
			break;
		headskip();
		for(f = fields; f != nil; f = f->next){
			if(cistrcmp(s, f->s) == !matches){
				nf = headstr - start;
				if(m + nf > n)
					return 0;
				memmove(&dst[m], start, nf);
				m += nf;
			}
		}
		free(s);
	}
	if(m + 3 > n)
		return 0;
	dst[m++] = '\r';
	dst[m++] = '\n';
	dst[m] = '\0';
	return m;
}

static Mimehdr*
mkmimehdr(char *s, char *t, Mimehdr *next)
{
	Mimehdr *mh;

	mh = MK(Mimehdr);
	mh->s = s;
	mh->t = t;
	mh->next = next;
	return mh;
}

static void
freemimehdr(Mimehdr *mh)
{
	Mimehdr *last;

	while(mh != nil){
		last = mh;
		mh = mh->next;
		free(last->s);
		free(last->t);
		free(last);
	}
}

static void
freeheader(Header *h)
{
	freemimehdr(h->type);
	freemimehdr(h->id);
	freemimehdr(h->description);
	freemimehdr(h->encoding);
//	freemimehdr(h->md5);
	freemimehdr(h->disposition);
	freemimehdr(h->language);
	free(h->buf);
}

static void
freemaddr(Maddr *a)
{
	Maddr *p;

	while(a != nil){
		p = a;
		a = a->next;
		free(p->personal);
		free(p->box);
		free(p->host);
		free(p);
	}
}

void
freemsg(Box *box, Msg *m)
{
	Msg *k, *last;

	if(box != nil)
		fstreedelete(box, m);
	free(m->ibuf);
	freemaddr(m->to);
	if(m->replyto != m->from)
		freemaddr(m->replyto);
	if(m->sender != m->from)
		freemaddr(m->sender);
	freemaddr(m->from);
	freemaddr(m->cc);
	freemaddr(m->bcc);
	freeheader(&m->head);
	freeheader(&m->mime);
	for(k = m->kids; k != nil; ){
		last = k;
		k = k->next;
		freemsg(0, last);
	}
	free(m->fs);
	free(m);
}

uint
msgsize(Msg *m)
{
	return m->head.size + m->size;
}

char*
maddrstr(Maddr *a)
{
	char *host, *addr;

	host = a->host;
	if(host == nil)
		host = "";
	if(a->personal != nil)
		addr = smprint("%s <%s@%s>", a->personal, a->box, host);
	else
		addr = smprint("%s@%s", a->box, host);
	return addr;
}

int
msgfile(Msg *m, char *f)
{
	if(strlen(f) > Filelen)
		bye("internal error: msgfile name too long");
	strcpy(m->efs, f);
	return cdopen(m->fsdir, m->fs, OREAD);
}

int
msgismulti(Header *h)
{
	return h->type != nil && cistrcmp("multipart", h->type->s) == 0;
}

int
msgis822(Header *h)
{
	Mimehdr *t;

	t = h->type;
	return t != nil && cistrcmp("message", t->s) == 0 && cistrcmp("rfc822", t->t) == 0;
}

/*
 * check if a message has been deleted by someone else
 */
void
msgdead(Msg *m)
{
	if(m->expunged)
		return;
	*m->efs = '\0';
	if(!cdexists(m->fsdir, m->fs))
		m->expunged = 1;
}

static long
msgreadfile(Msg *m, char *file, char **ss)
{
	char *s, buf[Bufsize];
	int fd;
	long n, nn;
	vlong length;
	Dir *d;

	fd = msgfile(m, file);
	if(fd < 0){
		msgdead(m);
		return -1;
	}

	n = read(fd, buf, Bufsize);
	if(n < Bufsize){
		close(fd);
		if(n < 0){
			*ss = nil;
			return -1;
		}
		s = emalloc(n + 1);
		memmove(s, buf, n);
		s[n] = '\0';
		*ss = s;
		return n;
	}

	d = dirfstat(fd);
	if(d == nil){
		close(fd);
		return -1;
	}
	length = d->length;
	free(d);
	nn = length;
	s = emalloc(nn + 1);
	memmove(s, buf, n);
	if(nn > n)
		nn = readn(fd, s + n, nn - n) + n;
	close(fd);
	if(nn != length){
		free(s);
		return -1;
	}
	s[nn] = '\0';
	*ss = s;
	return nn;
}

/*
 * make sure the message has valid associated info
 * used for Isubject, Idigest, Iinreplyto, Imessageid.
 */
int
msginfo(Msg *m)
{
	char *s;
	int i;

	if(m->info[0] != nil)
		return 1;
	if(msgreadfile(m, "info", &m->ibuf) < 0)
		return 0;
	s = m->ibuf;
	for(i = 0; i < Imax; i++){
		m->info[i] = s;
		s = strchr(s, '\n');
		if(s == nil)
			return 0;
		if(s == m->info[i])
			m->info[i] = 0;
		*s++ = '\0';
	}
//	m->lines = strtoul(m->info[Ilines], 0, 0);
//	m->size = strtoull(m->info[Isize], 0, 0);
//	m->size += m->lines;			/* BOTCH: this hack belongs elsewhere */
	return 1;
}

/*
 * make sure the message has valid mime structure
 * and sub-messages
 */
int
msgstruct(Msg *m, int top)
{
	char buf[12];
	int fd, ns, max;
	Msg *k, head, *last;

	if(m->kids != nil)
		return 1;
	if(m->expunged
	|| !msginfo(m)
	|| !msgheader(m, &m->mime, "mimeheader")){
		msgdead(m);
		return 0;
	}
	/* gack.  we need to get the header from the subpart here. */
	if(msgis822(&m->mime)){
		free(m->ibuf);
		m->info[0] = 0;
		m->efs = seprint(m->efs, m->efs + 5, "/1/");
		if(!msginfo(m)){
			msgdead(m);
			return 0;
		}
	}
	if(!msgbodysize(m)
	|| (top || msgis822(&m->mime) || msgismulti(&m->mime)) && !msgheader(m, &m->head, "rawheader")){
		msgdead(m);
		return 0;
	}

	/*
	 * if a message has no kids, it has a kid which is just the body of the real message
	 */
	if(!msgismulti(&m->head) && !msgismulti(&m->mime) && !msgis822(&m->head) && !msgis822(&m->mime)){
		k = MKZ(Msg);
		k->id = 1;
		k->fsdir = m->fsdir;
		k->parent = m->parent;
		ns = m->efs - m->fs;
		k->fs = emalloc(ns + (Filelen + 1));
		memmove(k->fs, m->fs, ns);
		k->efs = k->fs + ns;
		*k->efs = '\0';
		k->size = m->size;
		m->kids = k;
		return 1;
	}

	/*
	 * read in all child messages messages
	 */
	head.next = nil;
	last = &head;
	for(max = 1;; max++){
		snprint(buf, sizeof buf, "%d", max);
		fd = msgfile(m, buf);
		if(fd == -1)
			break;
		close(fd);
		m->efs[0] = 0;		/* BOTCH! */

		k = MKZ(Msg);
		k->id = max;
		k->fsdir = m->fsdir;
		k->parent = m;
		ns = strlen(m->fs) + 2*(Filelen + 1);
		k->fs = emalloc(ns);
		k->efs = seprint(k->fs, k->fs + ns, "%s%d/", m->fs, max);
		k->size = ~0UL;
		k->lines = ~0UL;
		last->next = k;
		last = k;
	}

	m->kids = head.next;

	/*
	 * if kids fail, just whack them
	 */
	top = top && (msgis822(&m->head) || msgismulti(&m->head));
	for(k = m->kids; k != nil; k = k->next)
		if(!msgstruct(k, top)){
			debuglog("kid fail %p %s", k, k->fs);
			for(k = m->kids; k != nil; ){
				last = k;
				k = k->next;
				freemsg(0, last);
			}
			m->kids = nil;
			break;
		}
	return 1;
}

/*
 * read in the message body to count \n without a preceding \r
 */
static int
msgbodysize(Msg *m)
{
	char buf[Bufsize + 2], *s, *se;
	uint length, size, lines, needr;
	int n, fd, c;
	Dir *d;

	if(m->lines != ~0UL)
		return 1;
	fd = msgfile(m, "rawbody");
	if(fd < 0)
		return 0;
	d = dirfstat(fd);
	if(d == nil){
		close(fd);
		return 0;
	}
	length = d->length;
	free(d);

	size = 0;
	lines = 0;
	needr = 0;
	buf[0] = ' ';
	for(;;){
		n = read(fd, &buf[1], Bufsize);
		if(n <= 0)
			break;
		size += n;
		se = &buf[n + 1];
		for(s = &buf[1]; s < se; s++){
			c = *s;
			if(c == '\0')
				*s = ' ';
			if(c != '\n')
				continue;
			if(s[-1] != '\r')
				needr++;
			lines++;
		}
		buf[0] = buf[n];
	}
	if(size != length)
		bye("bad length reading rawbody %d != %d; n %d %s", size, length, n, m->fs);
	size += needr;
	m->size = size;
	m->lines = lines;
	close(fd);
	return 1;
}

/*
 * prepend hdrname: val to the cached header
 */
static void
msgaddhead(Msg *m, char *hdrname, char *val)
{
	char *s;
	long size, n;

	n = strlen(hdrname) + strlen(val) + 4;
	size = m->head.size + n;
	s = emalloc(size + 1);
	snprint(s, size + 1, "%s: %s\r\n%s", hdrname, val, m->head.buf);
	free(m->head.buf);
	m->head.buf = s;
	m->head.size = size;
	m->head.lines++;
}

static void
msgadddate(Msg *m)
{
	char buf[64];
	Tm tm;

	/* don't bother if we don't have a date */
	if(m->info[Idate] == 0)
		return;

	date2tm(&tm, m->info[Idate]);
	snprint(buf, sizeof buf, "%δ", &tm);
	msgaddhead(m, "Date", buf);
}

/*
 * read in the entire header,
 * and parse out any existing mime headers
 */
static int
msgheader(Msg *m, Header *h, char *file)
{
	char *s, *ss, *t, *te;
	int dated, c;
	long ns;
	uint lines, n, nn;

	if(h->buf != nil)
		return 1;

	ns = msgreadfile(m, file, &ss);
	if(ns < 0)
		return 0;
	s = ss;
	n = ns;

	/*
	 * count lines ending with \n and \r\n
	 * add an extra line at the end, since upas/fs headers
	 * don't have a terminating \r\n
	 */
	lines = 1;
	te = s + ns;
	for(t = s; t < te; t++){
		c = *t;
		if(c == '\0')
			*t = ' ';
		if(c != '\n')
			continue;
		if(t == s || t[-1] != '\r')
			n++;
		lines++;
	}
	if(t > s && t[-1] != '\n'){
		if(t[-1] != '\r')
			n++;
		n++;
	}
	if(n > 0)
		n += 2;
	h->buf = emalloc(n + 1);
	h->size = n;
	h->lines = lines;

	/*
	 * make sure all headers end in \r\n
	 */
	nn = 0;
	for(t = s; t < te; t++){
		c = *t;
		if(c == '\n'){
			if(!nn || h->buf[nn - 1] != '\r')
				h->buf[nn++] = '\r';
			lines++;
		}
		h->buf[nn++] = c;
	}
	if(nn && h->buf[nn-1] != '\n'){
		if(h->buf[nn-1] != '\r')
			h->buf[nn++] = '\r';
		h->buf[nn++] = '\n';
	}
	if(nn > 0){
		h->buf[nn++] = '\r';
		h->buf[nn++] = '\n';
	}
	h->buf[nn] = '\0';
	if(nn != n)
		bye("misconverted header %d %d", nn, n);
	free(s);

	/*
	 * and parse some mime headers
	 */
	headstr = (uchar*)h->buf;
	dated = 0;
	while(s = headatom(headfieldstop)){
		if(cistrcmp(s, "content-type") == 0)
			mimetype(h);
		else if(cistrcmp(s, "content-transfer-encoding") == 0)
			mimeencoding(h);
		else if(cistrcmp(s, "content-id") == 0)
			mimeid(h);
		else if(cistrcmp(s, "content-description") == 0)
			mimedescription(h);
		else if(cistrcmp(s, "content-disposition") == 0)
			mimedisposition(h);
//		else if(cistrcmp(s, "content-md5") == 0)
//			mimemd5(h);
		else if(cistrcmp(s, "content-language") == 0)
			mimelanguage(h);
		else if(h == &m->head){
			if(cistrcmp(s, "from") == 0)
				m->from = headmaddr(m->from);
			else if(cistrcmp(s, "to") == 0)
				m->to = headmaddr(m->to);
			else if(cistrcmp(s, "reply-to") == 0)
				m->replyto = headmaddr(m->replyto);
			else if(cistrcmp(s, "sender") == 0)
				m->sender = headmaddr(m->sender);
			else if(cistrcmp(s, "cc") == 0)
				m->cc = headmaddr(m->cc);
			else if(cistrcmp(s, "bcc") == 0)
				m->bcc = headmaddr(m->bcc);
			else if(cistrcmp(s, "date") == 0)
				dated = 1;
		}
		headskip();
		free(s);
	}

	if(h == &m->head){
		if(m->sender == nil)
			m->sender = m->from;
		if(m->replyto == nil)
			m->replyto = m->from;
		if(!dated && m->from != nil)
			msgadddate(m);
	}
	return 1;
}

/*
 * q is a quoted string.  remove enclosing " and and \ escapes
 */
static void
stripquotes(char *q)
{
	char *s;
	int c;

	if(q == nil)
		return;
	s = q++;
	while(c = *q++){
		if(c == '\\'){
			c = *q++;
			if(!c)
				return;
		}
		*s++ = c;
	}
	s[-1] = '\0';
}

/*
 * parser for rfc822 & mime header fields
 */

/*
 * params	:
 *		| params ';' token '=' token
 * 		| params ';' token '=' quoted-str
 */
static Mimehdr*
mimeparams(void)
{
	char *s, *t;
	Mimehdr head, *last;

	head.next = nil;
	last = &head;
	for(;;){
		if(headchar(1) != ';')
			break;
		s = headatom(mimetokenstop);
		if(s == nil || headchar(1) != '='){
			free(s);
			break;
		}
		if(headchar(0) == '"'){
			t = headquoted('"', '"');
			stripquotes(t);
		}else
			t = headatom(mimetokenstop);
		if(t == nil){
			free(s);
			break;
		}
		last->next = mkmimehdr(s, t, nil);
		last = last->next;
	}
	return head.next;
}

/*
 * type		: 'content-type' ':' token '/' token params
 */
static void
mimetype(Header *h)
{
	char *s, *t;

	if(headchar(1) != ':')
		return;
	s = headatom(mimetokenstop);
	if(s == nil || headchar(1) != '/'){
		free(s);
		return;
	}
	t = headatom(mimetokenstop);
	if(t == nil){
		free(s);
		return;
	}
	h->type = mkmimehdr(s, t, mimeparams());
}

/*
 * encoding	: 'content-transfer-encoding' ':' token
 */
static void
mimeencoding(Header *h)
{
	char *s;

	if(headchar(1) != ':')
		return;
	s = headatom(mimetokenstop);
	if(s == nil)
		return;
	h->encoding = mkmimehdr(s, nil, nil);
}

/*
 * mailaddr	: ':' addresses
 */
static Maddr*
headmaddr(Maddr *old)
{
	Maddr *a;

	if(headchar(1) != ':')
		return old;

	if(headchar(0) == '\n')
		return old;

	a = headaddresses();
	if(a == nil)
		return old;

	freemaddr(old);
	return a;
}

/*
 * addresses	: address | addresses ',' address
 */
static Maddr*
headaddresses(void)
{
	Maddr *addr, *tail, *a;

	addr = headaddress();
	if(addr == nil)
		return nil;
	tail = addr;
	while(headchar(0) == ','){
		headchar(1);
		a = headaddress();
		if(a == nil){
			freemaddr(addr);
			return nil;
		}
		tail->next = a;
		tail = a;
	}
	return addr;
}

/*
 * address	: mailbox | group
 * group	: phrase ':' mboxes ';' | phrase ':' ';'
 * mailbox	: addr-spec
 *		| optphrase '<' addr-spec '>'
 *		| optphrase '<' route ':' addr-spec '>'
 * optphrase	: | phrase
 * route	: '@' domain
 *		| route ',' '@' domain
 * personal names are the phrase before '<',
 * or a comment before or after a simple addr-spec
 */
static Maddr*
headaddress(void)
{
	char *s, *e, *w, *personal;
	uchar *hs;
	int c;
	Maddr *addr;

	s = emalloc(strlen((char*)headstr) + 2);
	e = s;
	personal = headskipwhite(1);
	c = headchar(0);
	if(c == '<')
		w = nil;
	else{
		w = headword();
		c = headchar(0);
	}
	if(c == '.' || c == '@' || c == ',' || c == '\n' || c == '\0'){
		lastwhite = headstr;
		e = headaddrspec(s, w);
		if(personal == nil){
			hs = headstr;
			headstr = lastwhite;
			personal = headskipwhite(1);
			headstr = hs;
		}
	}else{
		if(c != '<' || w != nil){
			free(personal);
			if(!headphrase(e, w)){
				free(s);
				return nil;
			}

			/*
			 * ignore addresses with groups,
			 * so the only thing left if <
			 */
			c = headchar(1);
			if(c != '<'){
				free(s);
				return nil;
			}
			personal = estrdup(s);
		}else
			headchar(1);

		/*
		 * after this point, we need to free personal before returning.
		 * set e to nil to everything afterwards fails.
		 *
		 * ignore routes, they are useless, and heavily discouraged in rfc1123.
		 * imap4 reports them up to, but not including, the terminating :
		 */
		e = s;
		c = headchar(0);
		if(c == '@'){
			for(;;){
				c = headchar(1);
				if(c != '@'){
					e = nil;
					break;
				}
				headdomain(e);
				c = headchar(1);
				if(c != ','){
					e = s;
					break;
				}
			}
			if(c != ':')
				e = nil;
		}

		if(e != nil)
			e = headaddrspec(s, nil);
		if(headchar(1) != '>')
			e = nil;
	}

	/*
	 * e points to @host, or nil if an error occured
	 */
	if(e == nil){
		free(personal);
		addr = nil;
	}else{
		if(*e != '\0')
			*e++ = '\0';
		else
			e = site;
		addr = MKZ(Maddr);

		addr->personal = personal;
		addr->box = estrdup(s);
		addr->host = estrdup(e);
	}
	free(s);
	return addr;
}

/*
 * phrase	: word
 *		| phrase word
 * w is the optional initial word of the phrase
 * returns the end of the phrase, or nil if a failure occured
 */
static char*
headphrase(char *e, char *w)
{
	int c;

	for(;;){
		if(w == nil){
			w = headword();
			if(w == nil)
				return nil;
		}
		if(w[0] == '"')
			stripquotes(w);
		strcpy(e, w);
		free(w);
		w = nil;
		e = strchr(e, '\0');
		c = headchar(0);
		if(c <= ' ' || strchr(headatomstop, c) != nil && c != '"')
			break;
		*e++ = ' ';
		*e = '\0';
	}
	return e;
}

/*
 * find the ! in domain!rest, where domain must have at least
 * one internal '.'
 */
static char*
dombang(char *s)
{
	int dot, c;

	dot = 0;
	for(; c = *s; s++){
		if(c == '!'){
			if(!dot || dot == 1 && s[-1] == '.' || s[1] == '\0')
				return nil;
			return s;
		}
		if(c == '"')
			break;
		if(c == '.')
			dot++;
	}
	return nil;
}

/*
 * addr-spec	: local-part '@' domain
 *		| local-part			extension to allow ! and local names
 * local-part	: word
 *		| local-part '.' word
 *
 * if no '@' is present, rewrite d!e!f!u as @d,@e:u@f,
 * where d, e, f are valid domain components.
 * the @d,@e: is ignored, since routes are ignored.
 * perhaps they should be rewritten as e!f!u@d, but that is inconsistent with upas.
 *
 * returns a pointer to '@', the end if none, or nil if there was an error
 */
static char*
headaddrspec(char *e, char *w)
{
	char *s, *at, *b, *bang, *dom;
	int c;

	s = e;
	for(;;){
		if(w == nil){
			w = headword();
			if(w == nil)
				return nil;
		}
		strcpy(e, w);
		free(w);
		w = nil;
		e = strchr(e, '\0');
		lastwhite = headstr;
		c = headchar(0);
		if(c != '.')
			break;
		headchar(1);
		*e++ = '.';
		*e = '\0';
	}

	if(c != '@'){
		/*
		 * extenstion: allow name without domain
		 * check for domain!xxx
		 */
		bang = dombang(s);
		if(bang == nil)
			return e;

		/*
		 * if dom1!dom2!xxx, ignore dom1!
		 */
		dom = s;
		for(; b = dombang(bang + 1); bang = b)
			dom = bang + 1;

		/*
		 * convert dom!mbox into mbox@dom
		 */
		*bang = '@';
		strrev(dom, bang);
		strrev(bang + 1, e);
		strrev(dom, e);
		bang = &dom[e - bang - 1];
		if(dom > s){
			bang -= dom - s;
			for(e = s; *e = *dom; e++)
				dom++;
		}

		/*
		 * eliminate a trailing '.'
		 */
		if(e[-1] == '.')
			e[-1] = '\0';
		return bang;
	}
	headchar(1);

	at = e;
	*e++ = '@';
	*e = '\0';
	if(!headdomain(e))
		return nil;
	return at;
}

/*
 * domain	: sub-domain
 *		| domain '.' sub-domain
 * returns the end of the domain, or nil if a failure occured
 */
static char*
headdomain(char *e)
{
	char *w;

	for(;;){
		w = headsubdomain();
		if(w == nil)
			return nil;
		strcpy(e, w);
		free(w);
		e = strchr(e, '\0');
		lastwhite = headstr;
		if(headchar(0) != '.')
			break;
		headchar(1);
		*e++ = '.';
		*e = '\0';
	}
	return e;
}

/*
 * id		: 'content-id' ':' msg-id
 * msg-id	: '<' addr-spec '>'
 */
static void
mimeid(Header *h)
{
	char *s, *e, *w;

	if(headchar(1) != ':')
		return;
	if(headchar(1) != '<')
		return;

	s = emalloc(strlen((char*)headstr) + 3);
	e = s;
	*e++ = '<';
	e = headaddrspec(e, nil);
	if(e == nil || headchar(1) != '>'){
		free(s);
		return;
	}
	e = strchr(e, '\0');
	*e++ = '>';
	e[0] = '\0';
	w = strdup(s);
	free(s);
	h->id = mkmimehdr(w, nil, nil);
}

/*
 * description	: 'content-description' ':' *text
 */
static void
mimedescription(Header *h)
{
	if(headchar(1) != ':')
		return;
	headskipwhite(0);
	h->description = mkmimehdr(headtext(), nil, nil);
}

/*
 * disposition	: 'content-disposition' ':' token params
 */
static void
mimedisposition(Header *h)
{
	char *s;

	if(headchar(1) != ':')
		return;
	s = headatom(mimetokenstop);
	if(s == nil)
		return;
	h->disposition = mkmimehdr(s, nil, mimeparams());
}

/*
 * md5		: 'content-md5' ':' token
 */
//static void
//mimemd5(Header *h)
//{
//	char *s;
//
//	if(headchar(1) != ':')
//		return;
//	s = headatom(mimetokenstop);
//	if(s == nil)
//		return;
//	h->md5 = mkmimehdr(s, nil, nil);
//}

/*
 * language	: 'content-language' ':' langs
 * langs	: token
 *		| langs commas token
 * commas	: ','
 *		| commas ','
 */
static void
mimelanguage(Header *h)
{
	char *s;
	Mimehdr head, *last;

	head.next = nil;
	last = &head;
	for(;;){
		s = headatom(mimetokenstop);
		if(s == nil)
			break;
		last->next = mkmimehdr(s, nil, nil);
		last = last->next;
		while(headchar(0) != ',')
			headchar(1);
	}
	h->language = head.next;
}

/*
 * token	: 1*<char 33-255, except "()<>@,;:\\\"/[]?=" aka mimetokenstop>
 * atom		: 1*<chars 33-255, except "()<>@,;:\\\".[]" aka headatomstop>
 * note this allows 8 bit characters, which occur in utf.
 */
static char*
headatom(char *disallowed)
{
	char *s;
	int c, ns, as;

	headskipwhite(0);

	s = emalloc(Stralloc);
	as = Stralloc;
	ns = 0;
	for(;;){
		c = *headstr++;
		if(c <= ' ' || strchr(disallowed, c) != nil){
			headstr--;
			break;
		}
		s[ns++] = c;
		if(ns >= as){
			as += Stralloc;
			s = erealloc(s, as);
		}
	}
	if(ns == 0){
		free(s);
		return 0;
	}
	s[ns] = '\0';
	return s;
}

/*
 * sub-domain	: atom | domain-lit
 */
static char *
headsubdomain(void)
{
	if(headchar(0) == '[')
		return headquoted('[', ']');
	return headatom(headatomstop);
}

/*
 * word	: atom | quoted-str
 */
static char *
headword(void)
{
	if(headchar(0) == '"')
		return headquoted('"', '"');
	return headatom(headatomstop);
}

/*
 * quoted-str	: '"' *(any char but '"\\\r', or '\' any char, or linear-white-space) '"'
 * domain-lit	: '[' *(any char but '[]\\\r', or '\' any char, or linear-white-space) ']'
 */
static char *
headquoted(int start, int stop)
{
	char *s;
	int c, ns, as;

	if(headchar(1) != start)
		return nil;
	s = emalloc(Stralloc);
	as = Stralloc;
	ns = 0;
	s[ns++] = start;
	for(;;){
		c = *headstr;
		if(c == stop){
			headstr++;
			break;
		}
		if(c == '\0'){
			free(s);
			return nil;
		}
		if(c == '\r'){
			headstr++;
			continue;
		}
		if(c == '\n'){
			headstr++;
			while(*headstr == ' ' || *headstr == '\t' || *headstr == '\r' || *headstr == '\n')
				headstr++;
			c = ' ';
		}else if(c == '\\'){
			headstr++;
			s[ns++] = c;
			c = *headstr;
			if(c == '\0'){
				free(s);
				return nil;
			}
			headstr++;
		}else
			headstr++;
		s[ns++] = c;
		if(ns + 1 >= as){	/* leave room for \c or "0 */
			as += Stralloc;
			s = erealloc(s, as);
		}
	}
	s[ns++] = stop;
	s[ns] = '\0';
	return s;
}

/*
 * headtext	: contents of rest of header line
 */
static char *
headtext(void)
{
	uchar *v;
	char *s;

	v = headstr;
	headtoend();
	s = emalloc(headstr - v + 1);
	memmove(s, v, headstr - v);
	s[headstr - v] = '\0';
	return s;
}

/*
 * white space is ' ' '\t' or nested comments.
 * skip white space.
 * if com and a comment is seen,
 * return it's contents and stop processing white space.
 */
static char*
headskipwhite(int com)
{
	char *s;
	int c, incom, as, ns;

	s = nil;
	as = Stralloc;
	ns = 0;
	if(com)
		s = emalloc(Stralloc);
	incom = 0;
	for(; c = *headstr; headstr++){
		switch(c){
		case ' ':
		case '\t':
		case '\r':
			c = ' ';
			break;
		case '\n':
			c = headstr[1];
			if(c != ' ' && c != '\t')
				goto done;
			c = ' ';
			break;
		case '\\':
			if(com && incom)
				s[ns++] = c;
			c = headstr[1];
			if(c == '\0')
				goto done;
			headstr++;
			break;
		case '(':
			incom++;
			if(incom == 1)
				continue;
			break;
		case ')':
			incom--;
			if(com && !incom){
				s[ns] = '\0';
				return s;
			}
			break;
		default:
			if(!incom)
				goto done;
			break;
		}
		if(com && incom && (c != ' ' || ns > 0 && s[ns-1] != ' ')){
			s[ns++] = c;
			if(ns + 1 >= as){	/* leave room for \c or 0 */
				as += Stralloc;
				s = erealloc(s, as);
			}
		}
	}
done:
	free(s);
	return nil;
}

/*
 * return the next non-white character
 */
static int
headchar(int eat)
{
	int c;

	headskipwhite(0);
	c = *headstr;
	if(eat && c != '\0' && c != '\n')
		headstr++;
	return c;
}

static void
headtoend(void)
{
	uchar *s;
	int c;

	for(;;){
		s = headstr;
		c = *s++;
		while(c == '\r')
			c = *s++;
		if(c == '\n'){
			c = *s++;
			if(c != ' ' && c != '\t')
				return;
		}
		if(c == '\0')
			return;
		headstr = s;
	}
}

static void
headskip(void)
{
	int c;

	while(c = *headstr){
		headstr++;
		if(c == '\n'){
			c = *headstr;
			if(c == ' ' || c == '\t')
				continue;
			return;
		}
	}
}
