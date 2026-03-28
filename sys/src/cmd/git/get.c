#include <u.h>
#include <libc.h>

#include "git.h"

char *fetchbranch;
char *upstream = "origin";
Hash heads[64];
int nheads;
int listonly;

/*
 * Checks the rules for a refname at
 * git/Documentation/protocol-common.txt
 */
int
okrefname(char *s)
{
	if(strcmp(s, "HEAD") == 0)
		return 1;
	if(strncmp(s, "refs/", 5) == 0)
		return okref(s);
	return 0;
}

int
resolveremote(Hash *h, char *ref)
{
	char buf[128], *s;
	int r, f;

	ref = strip(ref);
	if((r = hparse(h, ref)) != -1)
		return r;
	/* Slightly special handling: translate remote refs to local ones. */
	if(strcmp(ref, "HEAD") == 0){
		snprint(buf, sizeof(buf), ".git/HEAD");
	}else if(strstr(ref, "refs/heads") == ref){
		ref += strlen("refs/heads");
		snprint(buf, sizeof(buf), ".git/refs/remotes/%s/%s", upstream, ref);
	}else if(strstr(ref, "refs/tags") == ref){
		ref += strlen("refs/tags");
		snprint(buf, sizeof(buf), ".git/refs/tags/%s/%s", upstream, ref);
	}else{
		return -1;
	}

	r = -1;
	s = strip(buf);
	if((f = open(s, OREAD)) == -1)
		return -1;
	if(readn(f, buf, sizeof(buf)) >= 40)
		r = hparse(h, buf);
	close(f);

	if(r == -1 && strstr(buf, "ref:") == buf)
		return resolveremote(h, buf + strlen("ref:"));
	return r;
}

int
rename(char *pack, char *idx, Hash h)
{
	char name[128];
	Dir st;

	nulldir(&st);
	st.name = name;
	snprint(name, sizeof(name), "%H.pack", h);
	if(access(name, AEXIST) == 0)
		fprint(2, "warning, pack %s already fetched\n", name);
	else if(dirwstat(pack, &st) == -1)
		return -1;
	snprint(name, sizeof(name), "%H.idx", h);
	if(access(name, AEXIST) == 0)
		fprint(2, "warning, pack %s already indexed\n", name);
	else if(dirwstat(idx, &st) == -1)
		return -1;
	return 0;
}

int
checkhash(int fd, vlong sz, Hash *hcomp)
{
	DigestState *st;
	Hash hexpect;
	char buf[Pktmax];
	vlong n, r;
	int nr;
	
	if(sz < 28){
		werrstr("undersize packfile");
		return -1;
	}

	st = nil;
	n = 0;
	while(n != sz - 20){
		nr = sizeof(buf);
		if(sz - n - 20 < sizeof(buf))
			nr = sz - n - 20;
		r = readn(fd, buf, nr);
		if(r != nr)
			return -1;
		st = sha1((uchar*)buf, nr, nil, st);
		n += r;
	}
	sha1(nil, 0, hcomp->h, st);
	if(readn(fd, hexpect.h, sizeof(hexpect.h)) != sizeof(hexpect.h))
		sysfatal("truncated packfile");
	if(!hasheq(hcomp, &hexpect)){
		werrstr("bad hash: %H != %H", *hcomp, hexpect);
		return -1;
	}
	return 0;
}

int
mkoutpath(char *path)
{
	char s[128];
	char *p;
	int fd;

	snprint(s, sizeof(s), "%s", path);
	for(p=strchr(s+1, '/'); p; p=strchr(p+1, '/')){
		*p = 0;
		if(access(s, AEXIST) != 0){
			fd = create(s, OREAD, DMDIR | 0775);
			if(fd == -1)
				return -1;
			close(fd);
		}		
		*p = '/';
	}
	return 0;
}

int
prefixed(char *s, char *pfx)
{
	return strncmp(s, pfx, strlen(pfx)) == 0;
}

int
branchmatch(char *br, char *pat)
{
	char name[128];

	if(prefixed(pat, "refs/heads"))
		snprint(name, sizeof(name), "%s", pat);
	else if(prefixed(pat, "heads/"))
		snprint(name, sizeof(name), "refs/%s", pat);
	else
		snprint(name, sizeof(name), "refs/heads/%s", pat);
	return strcmp(br, name) == 0;
}

void
fail(char *pack, char *idx, char *msg, ...)
{
	char buf[ERRMAX];
	va_list ap;

	va_start(ap, msg);
	snprint(buf, sizeof(buf), msg, ap);
	va_end(ap);

	remove(pack);
	remove(idx);
	fprint(2, "%s", buf);
	exits(buf);
}

void
enqueueparent(Objq *q, Object *o)
{
	Object *p;
	int i;

	if(o->type != GCommit)
		return;
	for(i = 0; i < o->commit->nparent; i++){
		if((p = readobject(o->commit->parent[i])) == nil)
			continue;
		qput(q, p, 0);
		unref(p);
	}
}

void
fmtcaps(Conn *c, char *caps, int ncaps)
{
	char *p, *e;

	p = caps;
	e = caps + ncaps;
	*p = 0;
	if(c->multiack)
		p = seprint(p, e, " multi_ack");
	if(c->sideband64k)
		p = seprint(p, e, " side-band-64k");
	else if(c->sideband)
		p = seprint(p, e, " side-band");
	assert(p != e);
}

int
sbread(Conn *c, char *buf, int nbuf, char **pbuf)
{
	int n;

	assert(nbuf >= Pktmax);
	if(!c->sideband && !c->sideband64k){
		*pbuf = buf;
		return readn(c->rfd, buf, nbuf);
	}else{
		*pbuf = buf+1;
		while(1){
			n = readpkt(c, buf, nbuf);
			if(n <= 0)
				return n;
			else if(buf[0] == 1 && n > 1)
				return n - 1;
			else if(buf[0] == 3)
				fprint(2, "error: %s\n", buf+1);
			else if(buf[0] < 1 || buf[0] > 3)
				fprint(2, "unknown sideband(%c:%d) data: %s\n", buf[0], buf[0], buf+1);
			
		}
	}
}

int
fetchpack(Conn *c)
{
	char spinner[] = {'|', '/', '-', '\\'};
	char buf[Pktmax], caps[512], *sp[3], *ep;
	char *packtmp, *idxtmp, **ref, *rp;
	Hash h, *have, *want;
	int nref, refsz, first, nsent;
	int i, j, l, n, spin, req, pfd;
	vlong packsz;
	Objset hadobj;
	Object *o;
	Objq haveq;
	Qelt e;

	nref = 0;
	refsz = 16;
	first = 1;
	have = eamalloc(refsz, sizeof(have[0]));
	want = eamalloc(refsz, sizeof(want[0]));
	ref = eamalloc(refsz, sizeof(ref[0]));
	while(1){
		n = readpkt(c, buf, sizeof(buf));
		if(n == -1)
			return -1;
		if(n == 0)
			break;

		if(first && n > strlen(buf)){
			parsecaps(buf + strlen(buf) + 1, c);
			if(c->symfrom[0] != 0)
				print("symref %s %s\n", c->symfrom, c->symto);
		}
		first = 0;

		if(getfields(buf, sp, nelem(sp), 1, " \t\n\r") < 2)
			sysfatal("invalid ref line");
		if(strstr(sp[1], "^{}"))
			continue;
		if(!okrefname(sp[1]))
			sysfatal("remote side sent invalid ref: %s", sp[1]);
		if(fetchbranch && !branchmatch(sp[1], fetchbranch))
			continue;
		else if(strcmp(sp[1], "HEAD") != 0
		&& !prefixed(sp[1], "refs/heads/")
		&& !prefixed(sp[1], "refs/tags/"))
			continue;
		if(refsz == nref + 1){
			refsz *= 2;
			have = earealloc(have, refsz, sizeof(have[0]));
			want = earealloc(want, refsz, sizeof(want[0]));
			ref = earealloc(ref, refsz, sizeof(ref[0]));
		}
		if(hparse(&want[nref], sp[0]) == -1)
			sysfatal("invalid hash %s", sp[0]);
		if (resolveremote(&have[nref], sp[1]) == -1)
			memset(&have[nref], 0, sizeof(have[nref]));
		ref[nref] = estrdup(sp[1]);
		nref++;
	}
	if(listonly){
		flushpkt(c);
		goto showrefs;
	}

	if(writephase(c) == -1)
		sysfatal("write: %r");
	req = 0;
	fmtcaps(c, caps, sizeof(caps));
	for(i = 0; i < nref; i++){
		if(hasheq(&have[i], &want[i]))
			goto skip;
		for(j = 0; j < i; j++)
			if(hasheq(&want[i], &want[j]))
				goto skip;
		if((o = readobject(want[i])) != nil){
			unref(o);
			continue;
		}
		if(fmtpkt(c, "want %H%s\n", want[i], caps) == -1)
			sysfatal("could not send want for %H", want[i]);
		caps[0] = 0;
		req = 1;
skip:;
	}
	flushpkt(c);

	nsent = 0;
	qinit(&haveq);
	osinit(&hadobj);
	/*
	 * We know we have these objects, and we want to make sure that
	 * they end up at the front of the queue. Send the 'have lines'
	 * first, and then enqueue their parents for a second round of
	 * sends.
	 */
	for(i = 0; i < nref; i++){
		if(hasheq(&have[i], &Zhash) || oshas(&hadobj, have[i]))
			continue;
		if((o = readobject(have[i])) == nil)
			sysfatal("missing exected object: %H", have[i]);
		if(fmtpkt(c, "have %H", o->hash) == -1)
			sysfatal("write: %r");
		enqueueparent(&haveq, o);
		osadd(&hadobj, o);
		unref(o);
		nsent++;
	}
	/*
	 * The other branches we have probably make sense to send,
	 * since often we'll be pulling a new branch with objects
	 * that we already have; it's not entirely clear what we
	 * want to do here.
	 */
	for(i = 0; i < nheads; i++){
		if((o = readobject(heads[i])) == nil)
			sysfatal("missing exected object: %H", have[i]);
		if(fmtpkt(c, "have %H", o->hash) == -1)
			sysfatal("write: %r");
		enqueueparent(&haveq, o);
		osadd(&hadobj, o);
		unref(o);
		nsent++;
	}
	/*
	 * While we could short circuit this and check if upstream has
	 * acked our objects, for the first 256 haves, this is simple
	 * enough.
	 *
	 * Also, doing multiple rounds of reference discovery breaks
	 * when using smart http.
	 */
	while(req && qpop(&haveq, &e) && nsent < 256){
		if(oshas(&hadobj, e.o->hash))
			continue;
		if((o = readobject(e.o->hash)) == nil)
			sysfatal("missing object we should have: %H", e.o->hash);
		if(fmtpkt(c, "have %H", o->hash) == -1)
			sysfatal("write: %r");
		enqueueparent(&haveq, o);
		osadd(&hadobj, o);
		unref(o);
		nsent++;
	}
	osclear(&hadobj);
	qclear(&haveq);
	if(!req)
		flushpkt(c);
	if(fmtpkt(c, "done\n") == -1)
		sysfatal("write: %r");
	if(!req)
		goto showrefs;
	if(readphase(c) == -1)
		sysfatal("read: %r");

	if((packtmp = smprint(".git/objects/pack/fetch.%d.pack", getpid())) == nil)
		sysfatal("smprint: %r");
	if((idxtmp = smprint(".git/objects/pack/fetch.%d.idx", getpid())) == nil)
		sysfatal("smprint: %r");
	if(mkoutpath(packtmp) == -1)
		sysfatal("could not create %s: %r", packtmp);
	if((pfd = create(packtmp, ORDWR, 0664)) == -1)
		sysfatal("could not create %s: %r", packtmp);

	fprint(2, "fetching...  ");
	packsz = 0;
	if(c->multiack){
		for(i = 0; i < nsent; i++){
			if(readpkt(c, buf, sizeof(buf)) == -1)
				sysfatal("read: %r");
			if(strncmp(buf, "NAK\n", 4) == 0)
				break;
			if(strncmp(buf, "ACK ", 4) == 0){
				if(getfields(buf, sp, nelem(sp), 1, " \t") == 2)
					break;
				continue;
			}
			sysfatal("bad response: '%s'", buf);
		}
	} 
	if(readpkt(c, buf, sizeof(buf)) == -1)
		sysfatal("read: %r");
	if(!c->sideband && !c->sideband64k && !c->multiack){
		/*
		 * Work around torvalds git bug: we get duplicate have lines
		 * somtimes, even though the protocol is supposed to start the
		 * pack file immediately.
		 *
		 * Skip ahead until we read 'PACK' off the wire
		 */
		while(1){
			if(readn(c->rfd, buf, 4) != 4)
				sysfatal("fetch packfile: short read");
			if(strncmp(buf, "PACK", 4) == 0)
				break;
			buf[4] = 0;
			l = strtol(buf, &ep, 16);
			if(ep != buf + 4)
				sysfatal("fetch packfile: junk pktline");
			if(readn(c->rfd, buf, l-4) != l-4)
				sysfatal("fetch packfile: short read");
		}
		if(write(pfd, "PACK", 4) != 4)
			sysfatal("write pack header: %r");
		packsz = 4;
	}
	spin = 0;
	while(1){
		n = sbread(c, buf, sizeof buf, &rp);
		if(n == 0)
			break;
		if(n == -1 || write(pfd, rp, n) != n)
			sysfatal("fetch packfile: %r");
		if(interactive && spin++ % 100 == 0)
			fprint(2, "\b%c", spinner[spin/100 % nelem(spinner)]);
		packsz += n;
	}
	fprint(2, "\n");
	closeconn(c);
	if(seek(pfd, 0, 0) == -1)
		fail(packtmp, idxtmp, "packfile seek: %r");
	if(checkhash(pfd, packsz, &h) == -1)
		fail(packtmp, idxtmp, "corrupt packfile: %r");
	close(pfd);
	if(indexpack(packtmp, idxtmp, h) == -1)
		fail(packtmp, idxtmp, "could not index fetched pack: %r");
	if(rename(packtmp, idxtmp, h) == -1)
		fail(packtmp, idxtmp, "could not rename indexed pack: %r");

showrefs:
	for(i = 0; i < nref; i++){
		print("remote %s %H local %H\n", ref[i], want[i], have[i]);
		free(ref[i]);
	}
	free(ref);
	free(want);
	free(have);
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: %s [-dl] [-b br] [-u upstream] remote\n", argv0);
	fprint(2, "\t-b br:	only fetch matching branch 'br'\n");
	fprint(2, "remote:	fetch from this repository\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *s;
	Conn c;

	ARGBEGIN{
	case 'b':	fetchbranch=EARGF(usage());	break;
	case 'u':	upstream=EARGF(usage());	break;
	case 'd':	chattygit++;			break;
	case 'l':	listonly++;			break;
	case 'h':
		s = EARGF(usage());
		if(nheads < nelem(heads))
			if(hparse(&heads[nheads], s) == 0)
				nheads++;
		break;
	default:
		usage();
		break;
	}ARGEND;

	gitinit(nil, 0, nil);
	if(argc != 1)
		usage();

	if(gitconnect(&c, argv[0], "upload") == -1)
		sysfatal("could not dial %s: %r", argv[0]);
	if(fetchpack(&c) == -1)
		sysfatal("fetch failed: %r");
	closeconn(&c);
	exits(nil);
}
