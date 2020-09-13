#include <u.h>
#include <libc.h>
#include <draw.h>
#include <plumb.h>
#include <regexp.h>
#include <bio.h>
#include "faces.h"

static int	showfd = -1;
static int	seefd = -1;
static char	*user;

char		**maildirs;
int		nmaildirs;

void
initplumb(void)
{
	if((showfd = plumbopen("send", OWRITE)) == -1)
		sysfatal("plumbopen send: %r");
	if((seefd = plumbopen("seemail", OREAD)) == -1)
		sysfatal("plumbopen seemail: %r");
}

void
addmaildir(char *dir)
{
	maildirs = erealloc(maildirs, (nmaildirs+1)*sizeof(char*));
	maildirs[nmaildirs++] = dir;
}

char*
attr(Face *f)
{
	static char buf[128];

	if(f->str[Sdigest]){
		snprint(buf, sizeof buf, "digest=%s", f->str[Sdigest]);
		return buf;
	}
	return nil;
}

void
showmail(Face *f)
{
	char *s;
	int n;

	if(showfd<0 || f->str[Sshow]==nil || f->str[Sshow][0]=='\0')
		return;
	s = emalloc(128+strlen(f->str[Sshow])+1);
	n = sprint(s, "faces\nshowmail\n/mail/fs/\ntext\n%s\n%ld\n%s", attr(f), strlen(f->str[Sshow]), f->str[Sshow]);
	write(showfd, s, n);
	free(s);
}

char*
value(Plumbattr *attr, char *key, char *def)
{
	char *v;

	v = plumblookup(attr, key);
	if(v)
		return v;
	return def;
}

void
setname(Face *f, char *sender)
{
	char *at, *bang;
	char *p;

	/* works with UTF-8, although it's written as ASCII */
	for(p=sender; *p!='\0'; p++)
		*p = tolower(*p);
	f->str[Suser] = sender;
	at = strchr(sender, '@');
	if(at){
		*at++ = '\0';
		f->str[Sdomain] = estrdup(at);
		return;
	}
	bang = strchr(sender, '!');
	if(bang){
		*bang++ = '\0';
		f->str[Suser] = estrdup(bang);
		f->str[Sdomain] = sender;
		return;
	}
}

ulong
parsedate(char *s)
{
	char **f, *fmt[] = {
		"WW MMM DD hh:mm:ss ?Z YYYY",
		"?WW ?DD ?MMM ?YYYY hh:mm:ss ?Z",
		"?WW ?DD ?MMM ?YYYY hh:mm:ss",
		"?WW, DD-?MM-YY",
		"?DD ?MMM ?YYYY hh:mm:ss ?Z",
		"?DD ?MMM ?YYYY hh:mm:ss",
		"?DD-?MM-YY hh:mm:ss ?Z",
		"?DD-?MM-YY hh:mm:ss",
		"?DD-?MM-YY",
		"?MMM/?DD/?YYYY hh:mm:ss ?Z",
		"?MMM/?DD/?YYYY hh:mm:ss",
		"?MMM/?DD/?YYYY",
		nil,
	};
	Tzone *tz;
	Tm tm;

	if((tz = tzload("local")) == nil)
		sysfatal("tzload: %r");
	for(f = fmt; *f; f++)
		if(tmparse(&tm, *f, s, tz, nil) != nil)
			return tmnorm(&tm);
	return time(0);
}

Face*
nextface(void)
{
	int i;
	Face *f;
	Plumbmsg *m;
	char *t, *senderp, *showmailp, *digestp;
	ulong xtime;

	f = emalloc(sizeof(Face));
	for(;;){
		m = plumbrecv(seefd);
		if(m == nil)
			killall("error on seemail plumb port");
		t = value(m->attr, "mailtype", "");
		if(strcmp(t, "modify") == 0)
			goto Ignore;
		else if(strcmp(t, "delete") == 0)
			delete(m->data, value(m->attr, "digest", nil));
		else if(strcmp(t, "new") == 0)
			for(i=0; i<nmaildirs; i++){
				if(strncmp(m->data, maildirs[i], strlen(maildirs[i])) == 0)
					goto Found;
			}
		else
			fprint(2, "faces: unknown plumb message type %s\n", t);
	Ignore:
		plumbfree(m);
		continue;

	Found:
		xtime = parsedate(value(m->attr, "date", date));
		digestp = value(m->attr, "digest", nil);
		if(alreadyseen(digestp)){
			/* duplicate upas/fs can send duplicate messages */
			plumbfree(m);
			continue;
		}
		senderp = estrdup(value(m->attr, "sender", "???"));
		showmailp = estrdup(m->data);
		if(digestp)
			digestp = estrdup(digestp);
		plumbfree(m);
		setname(f, senderp);
		f->time = xtime;
		f->tm = *localtime(xtime);
		f->str[Sshow] = showmailp;
		f->str[Sdigest] = digestp;
		return f;
	}
}

char*
iline(char *data, char **pp)
{
	char *p;

	for(p=data; *p!='\0' && *p!='\n'; p++)
		;
	if(*p == '\n')
		*p++ = '\0';
	*pp = p;
	return data;
}

Face*
dirface(char *dir, char *num)
{
	Face *f;
	char *from, *date;
	char buf[1024], pwd[1024], *info, *p, *digest;
	int n, fd;
	ulong len;

	/*
	 * loadmbox leaves us in maildir, so we needn't
	 * walk /mail/fs/mbox for each face; this makes startup
	 * a fair bit quicker.
	 */
	if(getwd(pwd, sizeof pwd) != nil && strcmp(pwd, dir) == 0)
		sprint(buf, "%s/info", num);
	else
		sprint(buf, "%s/%s/info", dir, num);
	len = dirlen(buf);
	if(len <= 0)
		return nil;
	fd = open(buf, OREAD);
	if(fd < 0)
		return nil;
	info = emalloc(len+1);
	n = readn(fd, info, len);
	close(fd);
	if(n < 0){
		free(info);
		return nil;
	}
	info[n] = '\0';
	f = emalloc(sizeof(Face));
	from = iline(info, &p);	/* from */
	iline(p, &p);	/* to */
	iline(p, &p);	/* cc */
	iline(p, &p);	/* replyto */
	date = iline(p, &p);	/* date */
	setname(f, estrdup(from));
	f->time = parsedate(date);
	f->tm = *localtime(f->time);
	sprint(buf, "%s/%s", dir, num);
	f->str[Sshow] = estrdup(buf);
	iline(p, &p);	/* subject */
	iline(p, &p);	/* mime content type */
	iline(p, &p);	/* mime disposition */
	iline(p, &p);	/* filename */
	digest = iline(p, &p);	/* digest */
	f->str[Sdigest] = estrdup(digest);
	free(info);
	return f;
}
