#include <u.h>
#include <libc.h>
#include <thread.h>
#include <auth.h>
#include <fcall.h>
#include <9p.h>
#include <ip.h>

enum {
	Qdata = 1,

	Tftp_READ	= 1,
	Tftp_WRITE	= 2,
	Tftp_DATA	= 3,
	Tftp_ACK	= 4,
	Tftp_ERROR	= 5,
	Tftp_OACK	= 6,

	TftpPort	= 69,

	Segsize		= 512,
	Maxpath		= 2+2+Segsize-8,
};

typedef struct Tfile Tfile;
struct Tfile
{
	int	id;
	uchar	addr[IPaddrlen];
	char	path[Maxpath];
	Channel	*c;
	Tfile	*next;
	Ref;
};

char net[Maxpath];
uchar ipaddr[IPaddrlen];
static ulong time0;
Tfile *files;

static Tfile*
tfileget(uchar *addr, char *path)
{
	Tfile *f;
	static int id;

	for(f = files; f; f = f->next){
		if(memcmp(addr, f->addr, IPaddrlen) == 0 && strcmp(path, f->path) == 0){
			incref(f);
			return f;
		}
	}
	f = emalloc9p(sizeof *f);
	memset(f, 0, sizeof(*f));
	ipmove(f->addr, addr);
	strncpy(f->path, path, Maxpath-1);
	f->ref = 1;
	f->id = id++;
	f->next = files;
	files = f;

	return f;
}

static void
tfileput(Tfile *f)
{
	Channel *c;
	Tfile **pp;

	if(f==nil || decref(f))
		return;
	if(c = f->c){
		f->c = nil;
	 	sendp(c, nil);
	}
	for(pp = &files; *pp; pp = &(*pp)->next){
		if(*pp == f){
			*pp = f->next;
			break;
		}
	}
	free(f);
}

static char*
basename(char *p)
{
	char *b;

	for(b = p; *p; p++)
		if(*p == '/')
			b = p+1;
	return b;
}

static void
tfilestat(Req *r, char *path, vlong length)
{
	memset(&r->d, 0, sizeof(r->d));
	r->d.uid = estrdup9p("tftp");
	r->d.gid = estrdup9p("tftp");
	r->d.name = estrdup9p(basename(path));
	r->d.atime = r->d.mtime = time0;
	r->d.length = length;
	r->d.qid.path = r->fid->qid.path;
	if(r->fid->qid.path & Qdata){
		r->d.qid.type = 0;
		r->d.mode = 0555;
	} else {
		r->d.qid.type = QTDIR;
		r->d.mode = DMDIR|0555;
	}
	respond(r, nil);
}

static void
catch(void *, char *msg)
{
	if(strstr(msg, "alarm"))
		noted(NCONT);
	noted(NDFLT);
}

static int
filereq(uchar *buf, char *path)
{
	uchar *p;
	int n;

	hnputs(buf, Tftp_READ);
	p = buf+2;
	n = strlen(path);

	/* hack: remove the trailing dot */
	if(path[n-1] == '.')
		n--;

	memcpy(p, path, n);
	p += n;
	*p++ = 0;
	memcpy(p, "octet", 6);
	p += 6;
	return p - buf;
}

static void
download(void *aux)
{
	int fd, cfd, last, block, seq, n, ndata;
	char *err, adir[40], buf[256];
	uchar *data;
	Channel *c;
	Tfile *f;
	Req *r;

	struct {
		Udphdr;
		uchar buf[2+2+Segsize+1];
	} msg;

	c = nil;
	r = nil;
	fd = cfd = -1;
	err = nil;
	data = nil;
	ndata = 0;

	if((f = aux) == nil)
		goto out;
	if((c = f->c) == nil)
		goto out;

	threadsetname("%s", f->path);

	snprint(buf, sizeof(buf), "%s/udp!*!0", net);
	if((cfd = announce(buf, adir)) < 0){
		err = "announce: %r";
		goto out;
	}
	if(write(cfd, "headers", 7) < 0){
		err = "write ctl: %r";
		goto out;
	}
	strcat(adir, "/data");
	if((fd = open(adir, ORDWR)) < 0){
		err = "open: %r";
		goto out;
	}

	n = filereq(msg.buf, f->path);
	ipmove(msg.raddr, f->addr);
	hnputs(msg.rport, TftpPort);
	if(write(fd, &msg, sizeof(Udphdr) + n) < 0){
		err = "send read request: %r";
		goto out;
	}

	notify(catch);

	seq = 1;
	last = 0;
	while(!last){
		alarm(5000);
		if((n = read(fd, &msg, sizeof(Udphdr) + sizeof(msg.buf)-1)) < 0){
			err = "receive response: %r";
			goto out;
		}
		alarm(0);

		n -= sizeof(Udphdr);
		msg.buf[n] = 0;
		switch(nhgets(msg.buf)){
		case Tftp_ERROR:
			werrstr("%s", (char*)msg.buf+4);
			err = "%r";
			goto out;

		case Tftp_DATA:
			if(n < 4)
				continue;
			block = nhgets(msg.buf+2);
			if(block > seq)
				continue;
			hnputs(msg.buf, Tftp_ACK);
			if(write(fd, &msg, sizeof(Udphdr) + 4) < 0){
				err = "send acknowledge: %r";
				goto out;
			}
			if(block < seq)
				continue;
			seq = block+1;
			n -= 4;
			if(n < Segsize)
				last = 1;
			data = erealloc9p(data, ndata + n);
			memcpy(data + ndata, msg.buf+4, n);
			ndata += n;

		rloop:	/* hanlde read request while downloading */
			if((r != nil) && (r->ifcall.type == Tread) && (r->ifcall.offset < ndata)){
				readbuf(r, data, ndata);
				respond(r, nil);
				r = nil;
			}
			if((r == nil) && (nbrecv(c, &r) == 1)){
				if(r == nil){
					chanfree(c);
					c = nil;
					goto out;
				}
				goto rloop;
			}
			break;
		}
	}

out:
	alarm(0);
	if(cfd >= 0)
		close(cfd);
	if(fd >= 0)
		close(fd);

	if(c){
		while((r != nil) || (r = recvp(c))){
			if(err){
				snprint(buf, sizeof(buf), err);
				respond(r, buf);
			} else {
				switch(r->ifcall.type){
				case Tread:
					readbuf(r, data, ndata);
					respond(r, nil);
					break;
				case Tstat:
					tfilestat(r, f->path, ndata);
					break;
				default:
					respond(r, "bug in fs");
				}
			}
			r = nil;
		}
		chanfree(c);
	}
	free(data);
}

static void
fsattach(Req *r)
{
	Tfile *f;

	if(r->ifcall.aname && r->ifcall.aname[0]){
		uchar addr[IPaddrlen];

		if(parseip(addr, r->ifcall.aname) == -1){
			respond(r, "bad ip specified");
			return;
		}
		f = tfileget(addr, "/");
	} else {
		if(ipcmp(ipaddr, IPnoaddr) == 0){
			respond(r, "no ipaddr specified");
			return;
		}
		f = tfileget(ipaddr, "/");
	}
	r->fid->aux = f;
	r->fid->qid.type = QTDIR;
	r->fid->qid.path = f->id<<1;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	Tfile *f;
	char *t;

	f = fid->aux;
	t = smprint("%s/%s", f->path, name);
	f = tfileget(f->addr, cleanname(t));
	free(t);
	tfileput(fid->aux); fid->aux = f;
	fid->qid.type = QTDIR;
	fid->qid.path = f->id<<1;

	/* hack:
	 * a dot in the path means the path element is not
	 * a directory. to force download of files containing
	 * no dot, a trailing dot can be appended that will
	 * be stripped out in the tftp read request.
	 */
	if(strchr(f->path, '.') != nil){
		fid->qid.type = 0;
		fid->qid.path |= Qdata;
	}

	if(qid)
		*qid = fid->qid;
	return nil;
}

static char*
fsclone(Fid *oldfid, Fid *newfid)
{
	Tfile *f;

	f = oldfid->aux;
	incref(f);
	newfid->aux = f;
	return nil;
}

static void
fsdestroyfid(Fid *fid)
{
	tfileput(fid->aux);
	fid->aux = nil;
}

static void
fsopen(Req *r)
{
	int m;

	m = r->ifcall.mode & 3;
	if(m != OREAD && m != OEXEC){
		respond(r, "permission denied");
		return;
	}
	respond(r, nil);
}

static void
dispatch(Req *r)
{
	Tfile *f;

	f = r->fid->aux;
	if(f->c == nil){
		f->c = chancreate(sizeof(r), 0);
		proccreate(download, f, 16*1024);
	}
	sendp(f->c, r);
}

static void
fsread(Req *r)
{
	if(r->fid->qid.path & Qdata){
		dispatch(r);
	} else {
		respond(r, nil);
	}
}

static void
fsstat(Req *r)
{
	if(r->fid->qid.path & Qdata){
		dispatch(r);
	} else {
		tfilestat(r, ((Tfile*)r->fid->aux)->path, 0);
	}
}

Srv fs = 
{
.attach=	fsattach,
.destroyfid=	fsdestroyfid,
.walk1=		fswalk1,
.clone=		fsclone,
.open=		fsopen,
.read=		fsread,
.stat=		fsstat,
};

void
usage(void)
{
	fprint(2, "usage: tftpfs [-D] [-s srvname] [-m mtpt] [-x net] [ipaddr]\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	char *srvname = nil;
	char *mtpt = "/n/tftp";

	time0 = time(0);
	strcpy(net, "/net");
	ipmove(ipaddr, IPnoaddr);

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 's':
		srvname = EARGF(usage());
		mtpt = nil;
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	case 'x':
		setnetmtpt(net, sizeof net, EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND;

	switch(argc){
	case 0:
		break;
	case 1:
		if(parseip(ipaddr, *argv) == -1)
			usage();
		break;
	default:
		usage();
	}

	if(srvname==nil && mtpt==nil)
		usage();

	threadpostmountsrv(&fs, srvname, mtpt, MREPL|MCREATE);
}
