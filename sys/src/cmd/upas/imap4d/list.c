#include "imap4d.h"

enum{
	Mfolder	= 0,
	Mbox,
	Mdir,
};

	char	subscribed[] = "imap.subscribed";
static	int	ldebug;

#define	dprint(...)	if(ldebug)fprint(2, __VA_ARGS__); else {}

static int	lmatch(char*, char*, char*);

static int
mopen(char *box, int mode)
{
	char buf[Pathlen];

	if(!strcmp(box, "..") || strstr(box, "/.."))
		return -1;
	return cdopen(mboxdir, encfs(buf, sizeof buf, box), mode);
}

static Dir*
mdirstat(char *box)
{
	char buf[Pathlen];

	return cddirstat(mboxdir, encfs(buf, sizeof buf, box));
}

static long
mtime(char *box)
{
	long mtime;
	Dir *d;

	mtime = 0;
	if(d = mdirstat(box))
		mtime = d->mtime;
	free(d);
	return mtime;
}

static int
mokmbox(char *s)
{
	char *p;

	if(p = strrchr(s, '/'))
		s = p + 1;
	if(!strcmp(s, "mbox"))
		return 1;
	return okmbox(s);
}

/*
 * paranoid check to prevent accidents
 */
/*
 * BOTCH: we're taking it upon ourselves to
 * identify mailboxes.  this is a bad idea.
 * keep in sync with ../fs/mdir.c
 */
static int
dirskip(Dir *a, uvlong *uv)
{
	char *p;

	if(a->length == 0)
		return 1;
	*uv = strtoul(a->name, &p, 0);
	if(*uv < 1000000 || *p != '.')
		return 1;
	*uv = *uv<<8 | strtoul(p+1, &p, 10);
	if(*p)
		return 1;
	return 0;
}

static int
chkmbox(char *path, int mode)
{
	char buf[32];
	int i, r, n, fd, type;
	uvlong uv;
	Dir *d;

	type = Mbox;
	if(mode & DMDIR)
		type = Mdir;
	fd = mopen(path, OREAD);
	if(fd == -1)
		return -1;
	r = -1;
	if(type == Mdir && (n = dirread(fd, &d)) > 0){
		r = Mfolder;
		for(i = 0; i < n; i++)
			if(!dirskip(d + i, &uv)){
				r = Mdir;
				break;
			}
		free(d);
	}else if(type == Mdir)
		r = Mdir;
	else if(type == Mbox){
		if(pread(fd, buf, sizeof buf, 0) == sizeof buf)
		if(!strncmp(buf, "From ", 5))
			r = Mbox;
	}
	close(fd);
	return r;
}

static int
chkmboxpath(char *f)
{
	int r;
	Dir *d;

	r = -1;
	if(d = mdirstat(f))
		r = chkmbox(f, d->mode);
	free(d);
	return r;
}

static char*
appendwd(char *nwd, int n, char *wd, char *a)
{
	if(wd[0] && a[0] != '/')
		snprint(nwd, n, "%s/%s", wd, a);
	else
		snprint(nwd, n, "%s", a);
	return nwd;
}

static int
output(char *cmd, char *wd, Dir *d, int term)
{
	char path[Pathlen], dec[Pathlen], *s, *flags;

	appendwd(path, sizeof path, wd, d->name);
	dprint("Xoutput %s %s %d\n", wd, d->name, term);
	switch(chkmbox(path, d->mode)){
	default:
		return 0;
	case Mfolder:
		flags = "(\\Noselect)";
		break;
	case Mdir:
	case Mbox:
		s = impname(path);
		if(s != nil && mtime(s) < d->mtime)
			flags = "(\\Noinferiors \\Marked)";
		else
			flags = "(\\Noinferiors)";
		break;
	}

	if(!term)
		return 1;

	if(s = strmutf7(decfs(dec, sizeof dec, path)))
		Bprint(&bout, "* %s %s \"/\" %#Z\r\n", cmd, flags, s);
	return 1;
}

static int
rematch(char *cmd, char *wd, char *pat, Dir *d)
{
	char nwd[Pathlen];

	appendwd(nwd, sizeof nwd, wd, d->name);
	if(d->mode & DMDIR)
	if(chkmbox(nwd, d->mode) == Mfolder)
	if(lmatch(cmd, pat, nwd))
		return 1;
	return 0;
}

static int
match(char *cmd, char *wd, char *pat, Dir *d, int i)
{
	char *p, *p1;
	int m, n;
	Rune r, r1;

	m = 0;
	for(p = pat; ; p = p1){
		n = chartorune(&r, p);
		p1 = p + n;
		dprint("r = %C [%.2ux]\n", r, r);
		switch(r){
		case '*':
		case '%':
			for(r1 = 1; r1;){
				if(match(cmd, wd, p1, d, i))
				if(output(cmd, wd, d, 0)){
					m++;
					break;
				}
				i += chartorune(&r1, d->name + i);
			}
			if(r == '*' && rematch(cmd, wd, p, d))
				return 1;
			if(m > 0)
				return 1;
			break;
		case '/':
			return rematch(cmd, wd, p1, d);
		default:
			chartorune(&r1, d->name + i);
			if(r1 != r)
				return 0;
			if(r == 0)
				return output(cmd, wd, d, 1);
			dprint("  r %C ~ %C [%.2ux]\n", r, r1, r1);
			i += n;
			break;
		}
	}
}

static int
lmatch(char *cmd, char *pat, char *wd)
{
	char dec[Pathlen];
	int fd, n, m, i;
	Dir *d;

	if((fd = mopen(wd[0]? wd: ".", OREAD)) == -1)
		return -1;
	if(wd[0])
		dprint("wd %s\n", wd);
	m = 0;
	for(;;){
		n = dirread(fd, &d);
		if(n <= 0)
			break;
		for(i = 0; i < n; i++)
			if(mokmbox(d[i].name)){
				d[i].name = decfs(dec, sizeof dec, d[i].name);
				m += match(cmd, wd, pat, d + i, 0);
			}
		free(d);
	}
	close(fd);
	return m;
}

int
listboxes(char *cmd, char *ref, char *pat)
{
	char buf[Pathlen];

	pat = appendwd(buf, sizeof buf, ref, pat);
	return lmatch(cmd, pat, "") > 0;
}

static int
opensubscribed(void)
{
	int fd;

	fd = cdopen(mboxdir, subscribed, ORDWR);
	if(fd >= 0)
		return fd;
	fd = cdcreate(mboxdir, subscribed, ORDWR, 0664);
	if(fd < 0)
		return -1;
	fprint(fd, "#imap4 subscription list\nINBOX\n");
	seek(fd, 0, 0);
	return fd;
}

/*
 * resistance to hand-edits
 */
static char*
trim(char *s, int l)
{
	int c;

	for(;; l--){
		if(l == 0)
			return 0;
		c = s[l - 1];
		if(c != '\t' && c != ' ')
			break;
	}
	for(s[l] = 0; c = *s; s++)
		if(c != '\t' && c != ' ')
			break;
	if(c == 0 || c == '#')
		return 0;
	return s;
}

static int
poutput(char *cmd, char *f, int term)
{
	char *p, *wd;
	int r;
	Dir *d;

	if(!mokmbox(f) || !(d = mdirstat(f)))
		return 0;
	wd = "";
	if(p = strrchr(f, '/')){
		*p = 0;
		wd = f;
	}
	r = output(cmd, wd, d, term);
	if(p)
		*p = '/';
	free(d);
	return r;
}

static int
pmatch(char *cmd, char *pat, char *f, int i)
{
	char *p, *p1;
	int m, n;
	Rune r, r1;

	dprint("pmatch pat[%s] f[%s]\n", pat, f + i);
	m = 0;
	for(p = pat; ; p = p1){
		n = chartorune(&r, p);
		p1 = p + n;
		switch(r){
		case '*':
		case '%':
			for(r1 = 1; r1;){
				if(pmatch(cmd, p1, f, i))
				if(poutput(cmd, f, 0)){
					m++;
					break;
				}
				i += chartorune(&r1, f + i);
				if(r == '%' && r1 == '/')
					break;
			}
			if(m > 0)
				return 1;
			break;
		default:
			chartorune(&r1, f + i);
			if(r1 != r)
				return 0;
			if(r == 0)
				return poutput(cmd, f, 1);
			i += n;
			break;
		}
	}
}

int
lsubboxes(char *cmd, char *ref, char *pat)
{
	char *s, buf[Pathlen];
	int r, fd;
	Biobuf b;
	Mblock *l;

	pat = appendwd(buf, sizeof buf, ref, pat);
	if((l = mblock()) == nil)
		return 0;
	fd = opensubscribed();
	r = 0;
	Binit(&b, fd, OREAD);
	while(s = Brdline(&b, '\n'))
		if(s = trim(s, Blinelen(&b) - 1))
			r += pmatch(cmd, pat, s, 0);
	Bterm(&b);
	close(fd);
	mbunlock(l);
	return r;
}

int
subscribe(char *mbox, int how)
{
	char *s, *in, *ein;
	int fd, tfd, ok, l;
	Mblock *mb;

	if(cistrcmp(mbox, "inbox") == 0)
		mbox = "INBOX";
	if((mb = mblock()) == nil)
		return 0;
	fd = opensubscribed();
	if(fd < 0 || (in = readfile(fd)) == nil){
		close(fd);
		mbunlock(mb);
		return 0;
	}
	l = strlen(mbox);
	s = strstr(in, mbox);
	while(s != nil && (s != in && s[-1] != '\n' || s[l] != '\n'))
		s = strstr(s + 1, mbox);
	ok = 0;
	if(how == 's' && s == nil){
		if(chkmboxpath(mbox) > 0)
		if(fprint(fd, "%s\n", mbox) > 0)
			ok = 1;
	}else if(how == 'u' && s != nil){
		ein = strchr(s, 0);
		memmove(s, &s[l+1], ein - &s[l+1]);
		ein -= l + 1;
		tfd = cdopen(mboxdir, subscribed, OWRITE|OTRUNC);
		if(tfd >= 0 && pwrite(fd, in, ein - in, 0) == ein - in)
			ok = 1;
		close(tfd);
	}else
		ok = 1;
	close(fd);
	mbunlock(mb);
	return ok;
}
