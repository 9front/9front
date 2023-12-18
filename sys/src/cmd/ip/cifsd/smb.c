#include <u.h>
#include <libc.h>
#include <auth.h>
#include "dat.h"
#include "fns.h"

enum {
	NEGOTIATE_USER_SECURITY = 1,
	NEGOTIATE_ENCRYPT_PASSWORDS = 2,
};

static Chalstate *smbcs;
static int sessionkey;
static int sessionuid;
static int negotiated;

void
smbnegotiate(Req *r, uchar *h, uchar *p, uchar *e)
{
	uchar *d, *de, *c, *ce, dom[256];
	int i, x, mode;
	char *s;

	if(!unpack(h, p, e, "#0b{*2}#1w[]", &d, &de)){
err:
		r->respond(r, STATUS_INVALID_SMB);
		return;
	}
	i = 0;
	x = -1;
	while(unpack(h, d, de, "_f.", smbstrunpack8, &s, &d)){
		if(debug)
			fprint(2, "[%d] %s\n", i, s);
		if(x < 0 && !cistrcmp(s, "NT LM 0.12"))
			x = i;
		free(s);
		i++;
	}
	if(x < 0)
		x = i-1;
	if(x < 0)
		x = 0;
	sessionkey = rand();
	c = ce = nil;
	mode = 0;
	if(needauth){
		if(smbcs != nil)
			auth_freechal(smbcs);
		if(smbcs = auth_challenge("proto=ntlm role=server")){
			c = (uchar*)smbcs->chal;
			ce = c + smbcs->nchal;
			mode = NEGOTIATE_USER_SECURITY | NEGOTIATE_ENCRYPT_PASSWORDS;
		} else
			logit("auth_challenge: %r");
	}

	/*
	 * <89> Section 2.2.4.52.2:  Windows NT servers always send the DomainName
	 *	field in Unicode characters and never add a padding byte for alignment.
	 *	Windows clients ignore the DomainName field in the server response.
	 */
	d = dom;
	de = dom + smbstrpack16(d, d, d + sizeof(dom), domain);
	if(!pack(r->rh, r->rp, r->re, "#0b{*2wbwwllllvw#2b}#1w{[][]}.",
		x, mode, 50, 1, BUFFERSIZE, 0x10000, sessionkey,
		CAP_UNIX | CAP_UNICODE | CAP_LARGEFILES | 
		CAP_NT_FIND | CAP_NT_SMBS | CAP_NT_STATUS,
		tofiletime(time(0)), -tzoff/60, c, ce, d, de, &r->rp))
		goto err;
	negotiated = 1;
	r->respond(r, 0);
}

enum {
	SMB_SETUP_GUEST = 1,
	SMB_SETUP_USE_LANMAN_KEY = 2,
};

void
smbsessionsetupandx(Req *r, uchar *h, uchar *p, uchar *e)
{
	uchar *lm, *lme, *nt, *nte, *xp;
	char *user, *dom, *os, *lanman;
	int xcmd, cap, bs, sk;
	AuthInfo *ai;

	user = dom = os = lanman = nil;
	if(!unpack(h, p, e, "#0b{*2b_@4ww____l#2w#3w____l}#1w{[][]ffff}{?.}",
		&xcmd, &bs, &sk, &cap, &lm, &lme, &nt, &nte,
		r->o->strunpack, &user, r->o->strunpack, &dom,
		r->o->strunpack, &os, r->o->strunpack, &lanman, &xp)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if(debug)
		fprint(2, "bs=%x cap=%x user=%s dom=%s os=%s lanman=%s\n",
			bs, cap, user, dom, os, lanman);
	if(sk != sessionkey)
		logit("ignoring bad session key");
	while(!remoteuser){
		if(needauth){
			MSchapreply *mcr;

			if(smbcs == nil || strlen(user) == 0)
				break;
			smbcs->user = user;
			smbcs->dom = dom;
			smbcs->nresp = (nte - nt)+sizeof(*mcr)-sizeof(mcr->NTresp);
			if(smbcs->nresp < sizeof(*mcr))
				smbcs->nresp = sizeof(*mcr);
			mcr = mallocz(smbcs->nresp, 1);
			if((lme - lm) <= sizeof(mcr->LMresp))
				memmove(mcr->LMresp, lm, lme - lm);
			if((nte - nt) > 0)
				memmove(mcr->NTresp, nt, nte - nt);
			smbcs->resp = mcr;
			ai = auth_response(smbcs);
			if(ai == nil){
				logit("auth_response: %r");
				free(mcr);
				break;	/* allow retry with the same challenge */
			}
			if(auth_chuid(ai, nil) < 0)
				logit("auth_chuid: %r");
			else {	/* chown network connection */
				Dir nd;
				nulldir(&nd);
				nd.mode = 0660;
				nd.uid = ai->cuid;
				dirfwstat(0, &nd);
			}
			auth_freeAI(ai);
			auth_freechal(smbcs);
			smbcs = nil;
			free(mcr);
		}
		remoteuser = getuser();
		logit("auth successful");
		break;
	}
	sessionuid = (namehash(getuser()) & 0x7FFF) | 1;
	r->uid = sessionuid;
	if(bs >= 1024 || bs <= BUFFERSIZE)
		remotebuffersize = bs;
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_@2ww}#1w{fff}{.}",
		xcmd, remoteuser ? 0 : SMB_SETUP_GUEST,
		r->o->strpack, osname, r->o->strpack, progname,
		r->o->strpack, domain, &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		smbcmd(r, xcmd, h, xp, e);
out:
	free(user);
	free(dom);
	free(lanman);
	free(os);
}

void
smblogoffandx(Req *r, uchar *h, uchar *p, uchar *e)
{
	int xcmd;
	uchar *xp;

	if(!unpack(h, p, e, "#0b{*2b_}#1w{}{?.}", &xcmd, &xp)){
unsup:
		r->respond(r, STATUS_NOT_SUPPORTED);
		return;
	}
	logit("logoff");
	if(remoteuser && needauth)
		goto unsup;
	logoff();
	remoteuser = nil;
	r->tid = 0xFFFF;
	r->uid = 0;
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_}#1w{}{.}", xcmd, &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		smbcmd(r, xcmd, h, xp, e);
}

enum {
	SMB_SUPPORT_SEARCH_BITS = 1,
};

void
smbtreeconnectandx(Req *r, uchar *h, uchar *p, uchar *e)
{
	int err, xcmd, flags;
	char *path, *service;
	uchar *pw, *pwe, *xp;
	Tree *t;

	path = service = nil;
	if(!unpack(h, p, e, "#0b{*2b_@3ww#2w}#1w{[]ff}{?.}",
		&xcmd, &flags, &pw, &pwe, r->o->strunpack, &path,
		smbstrunpack8, &service, &xp)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if(r->flags & 1){
		disconnecttree(r->tid);
		r->tid = 0xFFFF;
	}
	if((t = connecttree(service, path, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_@2ww}#1w{ff}{.}",
		xcmd, SMB_SUPPORT_SEARCH_BITS, 
		smbstrpack8, t->share->service,
		r->o->strpack, t->share->fsname, &r->rp)){
		disconnecttree(t->tid);
		r->respond(r, STATUS_INVALID_SMB);
	} else {
		r->tid = t->tid;
		smbcmd(r, xcmd, h, xp, e);
	}
out:
	free(service);
	free(path);
}

enum {
	READ_WRITE_LOCK = 0x00,
	SHARED_LOCK = 0x01,
	OPLOCK_RELEASE = 0x02,
	CHANGE_LOCKTYPE = 0x04,
	CANCEL_LOCK = 0x08,
	LARGE_FILES = 0x10,
};

void
smblockingandx(Req *r, uchar *h, uchar *p, uchar *e)
{
	int i, err, xcmd, fid, tol, timeout, nunlock, nlock, pid;
	unsigned int loff, hoff, llen, hlen;
	uchar *d, *de, *xp;
	vlong off, len;
	File *f;

	f = nil;
	if(!unpack(h, p, e, "#0b{*2b_@2wwb_lww}#1w[]{?.}",
		&xcmd, &fid, &tol, &timeout, &nunlock, &nlock, &d, &de, &xp)){
unsup:
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((f = getfile(r->tid, fid, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if(debug)
		fprint(2, "tol %x\ntimeout %d\nunnlock %d\nnlock %d\n", tol, timeout, nunlock, nlock);
	if(tol & (SHARED_LOCK | CHANGE_LOCKTYPE))
		goto unsup;
	for(i=0; i<nunlock+nlock; i++){
		if(tol & LARGE_FILES){
			if(!unpack(d, d, de, "w__llll[]", &pid, &hoff, &loff, &hlen, &llen, &d, nil))
				goto unsup;
		} else {
			if(!unpack(d, d, de, "wll[]", &pid, &loff, &llen, &d, nil))
				goto unsup;
			hoff = hlen = 0;
		}
		off = (vlong)hoff<<32 | loff;
		len = (vlong)hlen<<32 | llen;
		if(debug)
			fprint(2, "%s %x %llux %llux\n", (i < nunlock) ? "unlock" : "lock", pid, off, len);
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_@2w}#1w{}{.}", xcmd, &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		smbcmd(r, xcmd, h, xp, e);
out:
	putfile(f);
}

enum {
	REQ_ATTRIB = 0x01,
	REQ_OPLOCK = 0x02,
	REQ_OPLOCK_BATCH = 0x04,
};

void
smbopenandx(Req *r, uchar *h, uchar *p, uchar *e)
{
	int err, nfid, xcmd, flags, amode, omode, fattr, act, csize, ctime;
	char *name, *path;
	uchar *xp;
	Tree *t;
	File *f;
	Dir *d;

	static int amode2dacc[] = {
		[0x00] GENERIC_READ,
		[0x01] GENERIC_WRITE,
		[0x02] GENERIC_READ | GENERIC_WRITE,
		[0x03] GENERIC_EXECUTE,
	}, amode2sacc[] = {
		[0x00] FILE_SHARE_COMPAT, /* compat */
		[0x01] FILE_SHARE_NONE, /* exclusive use */
		[0x02] FILE_SHARE_READ, /* deny write */
		[0x03] FILE_SHARE_WRITE, /* deny read */
		[0x04] FILE_SHARE_READ | FILE_SHARE_WRITE, /* shared read/write */
		[0x05] -1,
		[0x06] -1,
		[0x07] -1,
	}, omode2cdisp[] = {
		[0x00] -1,
		[0x01] FILE_OPEN,
		[0x02] FILE_OVERWRITE,
		[0x03] -1,
		[0x10] FILE_CREATE,
		[0x11] FILE_OPEN_IF,
		[0x12] FILE_OVERWRITE_IF,
		[0x13] -1,
	};

	f = nil;
	d = nil;
	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2b_@2www__wlwl________}#1w{f}{?.}",
		&xcmd, &flags, &amode, &fattr, &ctime, &omode,
		&csize, r->o->nameunpack, &name, &xp)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(r->tid, name, &t, &err)) == nil)
		goto errout;
	if((f = createfile(path, r->namecmp,
		amode2dacc[amode & 3],  amode2sacc[(amode>>4) & 7],  omode2cdisp[omode & 0x13],
		FILE_NON_DIRECTORY_FILE, (ulong)csize, fattr, 
		&act, (flags & REQ_ATTRIB) ? &d : nil, &err)) == nil){
errout:
		r->respond(r, err);
		goto out;
	}
	nfid = newfid(t, f);
	amode = -1;
	if(f->dacc & READMASK)
		amode += 1;
	if(f->dacc & WRITEMASK)
		amode += 2;
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_@2wwwllww__w______}#1w{}{.}",
		xcmd, nfid,
		!d ? 0 : dosfileattr(d),
		!d ? 0 : d->mtime+tzoff,
		!d ? 0 : filesize32(d->length),
		!d ? 0 : amode, 
		!d ? 0 : f->rtype,
		!d ? 0 : act, &r->rp)){
		delfid(t, nfid);
		r->respond(r, STATUS_INVALID_SMB);
	} else
		smbcmd(r, xcmd, h, xp, e);
out:
	free(name);
	free(path);
	putfile(f);
	free(d);
}

enum {
	NT_CREATE_REQUEST_OPLOCK = 0x02,
	NT_CREATE_REQUEST_OPBATCH = 0x04,
	NT_CREATE_OPEN_TARGET_DIR = 0x08,
};

void
smbntcreatendx(Req *r, uchar *h, uchar *p, uchar *e)
{
	int err, nfid, xcmd, flags, rootfid, fattr, dacc, sacc, cdisp, copt, act;
	char *name, *path;
	vlong csize;
	uchar *xp;
	Tree *t;
	File *f;
	Dir *d;

	f = nil;
	d = nil;
	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2b_@2w___lllvllll_____}#1w{f}{?.}",
		&xcmd, &flags, &rootfid, &dacc, &csize, &fattr, &sacc, &cdisp, &copt,
		r->o->nameunpack, &name, &xp)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if(rootfid){
		if((f = getfile(r->tid, rootfid, &t, &err)) == nil)
			goto errout;
		path = conspath(f->path, name);
		putfile(f);
	} else if((path = getpath(r->tid, name, &t, &err)) == nil)
		goto errout;
	if((f = createfile(path, r->namecmp, dacc, sacc, cdisp, copt, csize, fattr, &act, &d, &err)) == nil){
errout:
		r->respond(r, err);
		goto out;
	}
	nfid = newfid(t, f);
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_@2wbwlvvvvlvvw__b}#1w{}{.}",
		xcmd, 0, nfid, act, tofiletime(d->mtime), tofiletime(d->atime),
		tofiletime(d->mtime), tofiletime(d->mtime), extfileattr(d),
		allocsize(d->length, t->share->blocksize), 
		d->length, f->rtype, (d->qid.type & QTDIR) != 0, &r->rp)){
		delfid(t, nfid);
		r->respond(r, STATUS_INVALID_SMB);
	} else
		smbcmd(r, xcmd, h, xp, e);
out:
	free(name);
	free(path);
	putfile(f);
	free(d);
}

void
smbreadandx(Req *r, uchar *h, uchar *p, uchar *e)
{
	int n, xcmd, fid, mincount, maxcount;
	unsigned int loff, hoff;
	uchar *rb, *rp, *re, *xp;
	vlong off;
	File *f;

	f = nil;
	hoff = 0;
	if((unpack(h, p, e, "#0b{*2b_@2wwlww______l}#1w{}{?.}",
		&xcmd, &fid, &loff, &mincount, &maxcount, &hoff, &xp) == 0) &&
	   (unpack(h, p, e, "#0b{*2b_@2wwlww______}#1w{}{?.}",
		&xcmd, &fid, &loff, &mincount, &maxcount, &xp) == 0)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((f = getfile(r->tid, fid, nil, &n)) == nil){
		r->respond(r, n);
		goto out;
	}
	if((f->fd < 0) || (f->dacc & READMASK) == 0){
		r->respond(r, STATUS_ACCESS_DENIED);
		goto out;
	}
	/* dont really pack, just to get the pointer to the response data */
	if(!pack(r->rh, r->rp, r->re, "#0b{*2________________________}#1w{%2.}", &rb)){
badsmb:
		r->respond(r, STATUS_INVALID_SMB);
		goto out;
	}
	re = rb + mincount;
	if(re > r->re)
		goto badsmb;
	if(maxcount > mincount){
		re = rb + maxcount;
		if(re > r->re)
			re = r->re;
	}
	n = 0;
	rp = rb;
	off = (vlong)hoff<<32 | loff;
	while(rp < re){
		if((n = pread(f->fd, rp, re - rp, off)) <= 0)
			break;
		off += n;
		rp += n;
	}
	if(n < 0){
		r->respond(r, smbmkerror());
		goto out;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_@3www__#2w@2w__________}#1w{%2[]}{.}",
		xcmd, 0xFFFF, 0x0000, rb, rp, &r->rp))
		goto badsmb;
	smbcmd(r, xcmd, h, xp, e);
out:
	putfile(f);
}

void
smbwriteandx(Req *r, uchar *h, uchar *p, uchar *e)
{
	int n, xcmd, fid, bufoff, buflen;
	unsigned int loff, hoff;
	uchar *d, *de, *xp;
	File *f;

	f = nil;
	hoff = 0;
	if((unpack(h, p, e, "#0b{*2b_@2wwl__________wwl}#1w{}{?.}",
		&xcmd, &fid, &loff, &buflen, &bufoff, &hoff, &xp) == 0) &&
	   (unpack(h, p, e, "#0b{*2b_@2wwl__________ww}#1w{}{?.}",
		&xcmd, &fid, &loff, &buflen, &bufoff, &xp) == 0)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	d = h + bufoff;
	de = d + buflen;
	if(d < h || de > e){
badsmb:
		r->respond(r, STATUS_INVALID_SMB);
		goto out;
	}
	if((f = getfile(r->tid, fid, nil, &n)) == nil){
		r->respond(r, n);
		goto out;
	}
	if((f->fd < 0) || (f->dacc & WRITEMASK) == 0){
		r->respond(r, STATUS_ACCESS_DENIED);
		goto out;
	}
	if((n = pwrite(f->fd, d, de - d, (vlong)hoff<<32 | loff)) < 0){
		r->respond(r, smbmkerror());
		goto out;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2b_@2www____}#1w{}{.}", xcmd, n, 0xFFFF, &r->rp))
		goto badsmb;
	smbcmd(r, xcmd, h, xp, e);
out:
	putfile(f);
}

void
smbwrite(Req *r, uchar *h, uchar *p, uchar *e)
{
	int n, fid, count, bf;
	unsigned int off;
	uchar *d, *de;
	File *f;

	f = nil;
	if(!unpack(h, p, e, "#0b{*2wwl__}#1w{b#2w[]}", &fid, &count, &off, &bf, &d, &de)){
unsup:
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if(bf != 0x1)
		goto unsup;
	if((f = getfile(r->tid, fid, nil, &n)) == nil){
		r->respond(r, n);
		goto out;
	}
	if((f->fd < 0) || (f->dacc & WRITEMASK) == 0){
		r->respond(r, STATUS_ACCESS_DENIED);
		goto out;
	}
	if(count != (de - d)){
		r->respond(r, STATUS_INVALID_SMB);
		goto out;
	}
	if((n = pwrite(f->fd, d, count, off)) < 0){
		r->respond(r, smbmkerror());
		goto out;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2w}#1w{}.", n, &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	putfile(f);
}

void
smbcloseflush(Req *r, uchar *h, uchar *p, uchar *e)
{
	int err, fid;
	Tree *t;
	Find *s;
	File *f;

	f = nil;
	s = nil;
	if(!unpack(h, p, e, "#0b{*2w}#1w{}", &fid)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	switch(r->cmd){
	case 0x05: /* SMB_COM_FLUSH */
		if(fid == 0xFFFF){
			if(gettree(r->tid) == nil){
				r->respond(r, STATUS_SMB_BAD_TID);
				goto out;
			}
			break;
		}
		/* no break */
	case 0x04: /* SMB_COM_CLOSE */
		if((f = getfile(r->tid, fid, &t, &err)) == nil){
			r->respond(r, err);
			goto out;
		}
		if(r->cmd == 0x04) 
			delfid(t, fid);
		break;
	case 0x34: /* SMB_COM_FIND_CLOSE2 */
		if((s = getfind(r->tid, fid, &t, &err)) == nil){
			r->respond(r, err);
			goto out;
		}
		delsid(t, fid);
		break;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	putfile(f);
	putfind(s);
}

void
smbcreatedirectory(Req *r, uchar *h, uchar *p, uchar *e)
{
	char *name, *path;
	int err, fd;

	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2}#1w{_f}", r->o->nameunpack, &name)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(r->tid, name, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if(access(path, AEXIST) == 0){
		r->respond(r, STATUS_OBJECT_NAME_COLLISION);
		goto out;
	}
	if((fd = create(path, OREAD, DMDIR | 0777)) < 0){
		r->respond(r, smbmkerror());
		goto out;
	}
	close(fd);
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	free(name);
	free(path);
}

void
smbrename(Req *r, uchar *h, uchar *p, uchar *e)
{
	char *name1, *name2, *path1, *path2, *x, *y;
	int err, sattr;
	Dir *d, nd;

	d = nil;
	name1 = name2 = path1 = path2 = nil;
	if(!unpack(h, p, e, "#0b{*2w}#1w{_f_f}", &sattr,
		r->o->nameunpack, &name1, r->o->nameunpack, &name2)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path1 = getpath(r->tid, name1, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if((path2 = getpath(r->tid, name2, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if((d = xdirstat(&path1, r->namecmp)) == nil){
		r->respond(r, smbmkerror());
		goto out;
	}
	if(!matchattr(d, sattr)){
		r->respond(r, STATUS_NO_SUCH_FILE);
		goto out;
	}

	if(x = strrchr(path1, '/')){
		*x = 0;
	} else {
badpath:
		r->respond(r, STATUS_OBJECT_PATH_SYNTAX_BAD);
		goto out;
	}
	if(y = strrchr(path2, '/'))
		*y++ = 0;
	else
		goto badpath;
	if(r->namecmp(path1, path2)){
		r->respond(r, STATUS_NOT_SAME_DEVICE);
		goto out;
	}
	*x = '/';
	nulldir(&nd);
	nd.name = y;
	if(dirwstat(path1, &nd) < 0){
		r->respond(r, smbmkerror());
		goto out;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
	xdirflush(path1, r->namecmp);
out:
	free(name1);
	free(name2);
	free(path1);
	free(path2);
	free(d);
}

void
smbdelete(Req *r, uchar *h, uchar *p, uchar *e)
{
	char *name, *path, *tmp;
	int n, err, i, sattr;
	Find *f;
	Dir *d;

	f = nil;
	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2w}#1w{_f}", &sattr, r->o->nameunpack, &name)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(r->tid, name, nil, &err)) == nil){
errout:
		r->respond(r, err);
		goto out;
	}
	if((f = openfind(path, r->namecmp, sattr, 0, &err)) == nil)
		goto errout;
	n = 0;
	while((i = readfind(f, f->index, &d)) >= 0){
		tmp = conspath(f->base, d->name);
		if(remove(tmp) < 0){
			err = smbmkerror();
			free(tmp);
			goto errout;
		}
		free(tmp);
		f->index = i+1;
		n++;
	}
	if(n == 0){
		err = STATUS_NO_SUCH_FILE;
		goto errout;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	free(name);
	free(path);
	putfind(f);
}

void
smbdeletedirectory(Req *r, uchar *h, uchar *p, uchar *e)
{
	char *name, *path;
	Dir *d;
	int err;

	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2}#1w{_f}", r->o->nameunpack, &name)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(r->tid, name, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if(remove(path) < 0){
		err = smbmkerror();
		if((d = xdirstat(&path, r->namecmp)) == nil){
			r->respond(r, err);
			goto out;
		}
		free(d);
		if(remove(path) < 0){
			r->respond(r, smbmkerror());
			goto out;
		}
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	free(name);
	free(path);
}

void
smbstatusnotimplemented(Req *r, uchar *, uchar *, uchar *)
{
	r->respond(r, STATUS_NOT_IMPLEMENTED);
}

void
smbecho(Req *r, uchar *h, uchar *p, uchar *e)
{
	uchar *t, *d, *de;
	int i, n;

	if(!unpack(h, p, e, "#0b{*2w}#1w[]", &n, &d, &de)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		return;
	}
	if((r->tid != 0xFFFF) && (gettree(r->tid) == nil)){
		r->respond(r, STATUS_SMB_BAD_TID);
		return;
	}
	t = r->rp;
	for(i=0; i < n; i++){
		if(!pack(r->rh, r->rp, r->re, "#0b{*2w}#1w[].", i, d, de, &r->rp)){
			r->respond(r, STATUS_INVALID_SMB);
			break;
		}
		r->respond(r, 0);
		r->rp = t;
	}
}

void
smbdisconnecttree(Req *r, uchar *h, uchar *p, uchar *e)
{
	int err;

	if(!unpack(h, p, e, "#0b{*2}#1w{}")){
		r->respond(r, STATUS_NOT_SUPPORTED);
		return;
	}
	if(err = disconnecttree(r->tid)){
		r->respond(r, err);
		return;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
}

void
smbqueryinformation(Req *r, uchar *h, uchar *p, uchar *e)
{
	char *name, *path;
	int err, mtime;
	Dir *d;

	d = nil;
	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2}#1w{_f}", r->o->nameunpack, &name)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(r->tid, name, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if((d = xdirstat(&path, r->namecmp)) == nil){
		r->respond(r, smbmkerror());
		goto out;
	}
	mtime = d->mtime + tzoff;
	if(!pack(r->rh, r->rp, r->re, "#0b{*2wll__________}#1w{}.",
		dosfileattr(d), mtime, filesize32(d->length), &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	free(name);
	free(path);
	free(d);
}

void
smbsetinformation(Req *r, uchar *h, uchar *p, uchar *e)
{
	char *name, *path;
	int err, attr, mtime;
	Dir *d, nd;

	d = nil;
	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2wl__________}#1w{_f}", &attr, &mtime, r->o->nameunpack, &name)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(r->tid, name, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if((d = xdirstat(&path, r->namecmp)) == nil){
		r->respond(r, smbmkerror());
		goto out;
	}
	nulldir(&nd);
	if(mtime)
		nd.mtime = mtime-tzoff;
	nd.mode = d->mode;
	if(attr & ATTR_READONLY){
		if(nd.mode & 0222)
			nd.mode &= ~0222;
	}else{
		if((nd.mode & 0222) == 0)
			nd.mode |= 0222;
	}
	if(attr & ATTR_ARCHIVE)
		nd.mode &= ~DMTMP;
	else
		nd.mode |= DMTMP;
	if(nd.mode == d->mode)
		nd.mode = ~0;
	if(dirwstat(path, &nd) < 0){
		r->respond(r, smbmkerror());
		goto out;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
	xdirflush(path, r->namecmp);
out:
	free(name);
	free(path);
	free(d);
}

void
smbcheckdirectory(Req *r, uchar *h, uchar *p, uchar *e)
{
	char *name, *path;
	int err;
	Dir *d;

	d = nil;
	name = path = nil;
	if(!unpack(h, p, e, "#0b{*2}#1w{_f}", r->o->nameunpack, &name)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(r->tid, name, nil, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if((d = xdirstat(&path, r->namecmp)) == nil){
		r->respond(r, smbmkerror());
		goto out;
	}
	if((d->qid.type & QTDIR) == 0){
		r->respond(r, STATUS_OBJECT_PATH_NOT_FOUND);
		goto out;
	}
	if(!pack(r->rh, r->rp, r->re, "#0b{*2}#1w{}.", &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	free(name);
	free(path);
	free(d);
}

void
smbqueryinformation2(Req *r, uchar *h, uchar *p, uchar *e)
{
	int err, fid, adate, atime, mdate, mtime;
	Tree *t;
	File *f;
	Dir *d;

	f = nil;
	t = nil;
	d = nil;
	if(!unpack(h, p, e, "#0b{*2w}#1w{}", &fid)){
		r->respond(r, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((f = getfile(r->tid, fid, &t, &err)) == nil){
		r->respond(r, err);
		goto out;
	}
	if((d = statfile(f)) == nil){
		r->respond(r, smbmkerror());
		goto out;
	}
	todatetime(d->atime+tzoff, &adate, &atime);
	todatetime(d->mtime+tzoff, &mdate, &mtime);
	if(!pack(r->rh, r->rp, r->re, "#0b{*2wwwwwwllw}#1w{}.",
		mdate, mtime, adate, atime, mdate, mtime,
		filesize32(d->length), filesize32(allocsize(d->length, t->share->blocksize)),
		dosfileattr(d), &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
out:
	putfile(f);
	free(d);
}

void
smbqueryinformationdisk(Req *r, uchar *h, uchar *p, uchar *e)
{
	Tree *t;
	Share *s;

	if(!unpack(h, p, e, "#0b{*2}#1w{}")){
		r->respond(r, STATUS_NOT_SUPPORTED);
		return;
	}
	if((t = gettree(r->tid)) == nil){
		r->respond(r, STATUS_SMB_BAD_TID);
		return;
	}
	s = t->share;
	if(!pack(r->rh, r->rp, r->re, "#0b{*2wwww__}#1w{}.",
		(int)(allocsize(s->allocsize + s->freesize, s->blocksize) / s->blocksize),
		s->blocksize / s->sectorsize, s->sectorsize,
		(int)(allocsize(s->freesize, s->blocksize) / s->blocksize), &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, 0);
}

static int
unixtype(Dir *d)
{
	return (d->qid.type & QTDIR) != 0;
}

static int
fpackdir(Req *r, Dir *d, Tree *t, int i, int level, uchar *b, uchar *p, uchar *e, uchar **prevoff, uchar **nameoff)
{
	vlong atime, mtime, alen, dlen;
	uchar shortname[2*12];
	uchar *namep;
	Share *share;
	int n;

	share = t->share;
	dlen = d->length;
	alen = allocsize(dlen, share->blocksize);
	atime = tofiletime(d->atime);
	mtime = tofiletime(d->mtime);
	memset(shortname, 0, sizeof(shortname));

	switch(level){
	case 0x0101:	/* SMB_FIND_FILE_DIRECTORY_INFO */
		n = pack(b, p, e, "llvvvvvvl#0l{.f}%4",
			0, i, mtime, atime, mtime, mtime, dlen, alen, extfileattr(d),
			&namep, r->o->untermnamepack, d->name);
		break;

	case 0x0102:	/* SMB_FIND_FILE_FULL_DIRECTORY_INFO */
		n = pack(b, p, e, "llvvvvvvl#0ll{.f}%4",
			0, i, mtime, atime, mtime, mtime, dlen, alen, extfileattr(d), 0,
			&namep, r->o->untermnamepack, d->name);
		break;

	case 0x0103:	/* SMB_FIND_FILE_NAMES_INFO */
		n = pack(b, p, e, "ll#0l{.f}%4",
			0, i, &namep, r->o->untermnamepack, d->name);
		break;

	case 0x0104:	/* SMB_FIND_FILE_BOTH_DIRECTORY_INFO */
		n = pack(b, p, e, "llvvvvvvl#1l#2lb_[]{.f}{}____%4",
			0, i, mtime, atime, mtime, mtime, dlen, alen, extfileattr(d),
			0, shortname, shortname+sizeof(shortname),
			&namep, r->o->untermnamepack, d->name);
		break;

	case 0x0105:	/* SMB_FIND_FILE_FULL_DIRECTORY_INFO */
		n = pack(b, p, e, "llvvvvvvl#0lvv{.f}%4",
			0, i, mtime, atime, mtime, mtime, dlen, alen, extfileattr(d),
			(vlong)0, (vlong)i,
			&namep, r->o->untermnamepack, d->name);
		break;

	case 0x0202:	/* SMB_FIND_FILE_UNIX */
		n = pack(b, p, e, "llvvvvvvvlvvvvv.f",
			0, i,
			dlen, alen,
			mtime, atime, mtime,
			(vlong)unixuid(share, d->uid), (vlong)unixgid(share, d->gid), unixtype(d),
			0LL, 0LL, /* MAJ/MIN */
			(vlong)d->qid.path,
			(vlong)d->mode & 0777,
			1LL,	/* NLINKS */
			&namep, r->o->namepack, d->name);
		break;

	default:
		logit("[%.4x] unknown FIND infolevel", level);
		return -1;
	}
	if(n <= 0)
		return 0;
	if(nameoff)
		*nameoff = namep;
	if(prevoff && *prevoff)
		pack(b, *prevoff, e, "l", (int)(p - *prevoff));
	if(prevoff)
		*prevoff = p;
	return n;
}

static int
qpackdir(Req *, Dir *d, Tree *t, File *f, int level, uchar *b, uchar *p, uchar *e)
{
	vlong atime, mtime, dlen, alen;
	int link, delete, isdir;
	Share *share;

	if(debug)
		fprint(2, "QYERY level %.4x\n", level);

	share = t->share;
	dlen = d->length;
	alen = allocsize(dlen, share->blocksize);
	atime = tofiletime(d->atime);
	mtime = tofiletime(d->mtime);
	isdir = (d->qid.type & QTDIR) != 0;
	delete = f && deletedfile(f);
	link = !delete;

	switch(level){
	case 0x0101:	/* SMB_QUERY_FILE_BASIC_INFO */
		return pack(b, p, e, "vvvvl____", mtime, atime, mtime, mtime, extfileattr(d));

	case 0x0102:	/* SMB_QUERY_FILE_STANDARD_INFO */
		return pack(b, p, e, "vvlbb", alen, dlen, link, delete, isdir);

	case 0x0103:	/* SMB_QUERY_FILE_EA_INFO */
		return pack(b, p, e, "l", 0);

	case 0x0107:	/* SMB_QUERY_FILE_ALL_INFO */
		return pack(b, p, e, "vvvvl____vvlbb__#1l#0l{f}{}", 
			mtime, atime, mtime, mtime, extfileattr(d), alen, dlen, link,  delete, isdir,
			smbuntermnamepack16, d->name);

	case 0x0109:	/* SMB_QUERY_FILE_STREAM_INFO */
		if(isdir)
			return 0;
		return pack(b, p, e, "l#0lvv{f}", 0, dlen, alen, smbuntermstrpack16, "::$DATA");

	case 0x0200:	/* SMB_QUERY_FILE_UNIX_BASIC */
		return pack(b, p, e, "vvvvvvvlvvvvv",
			dlen, alen,
			mtime, atime, mtime,
			(vlong)unixuid(share, d->uid), (vlong)unixgid(share, d->gid), unixtype(d),
			0LL, 0LL, /* MAJ/MIN */
			(vlong)d->qid.path,
			(vlong)d->mode & 0777,
			(vlong)link);	/* NLINKS */
	default:
		logit("[%.4x] unknown QUERY infolevel", level);
		return -1;
	}
}

void
trans2querypathinformation(Trans *t)
{
	char *name, *path;
	Tree *tree;
	int n, level;
	Dir *d;

	d = nil;
	path = name = nil;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "w____f",
		&level, t->o->nameunpack, &name)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(t->r->tid, name, &tree, &n)) == nil){
		t->respond(t, n);
		goto out;
	}
	if((d = xdirstat(&path, t->namecmp)) == nil){
		t->respond(t, smbmkerror());
		goto out;
	}
	pack(t->out.param.b, t->out.param.p, t->out.param.e, "__.", &t->out.param.p);
	if((n = qpackdir(t->r, d, tree, nil, level, t->out.data.b, t->out.data.p, t->out.data.e)) < 0)
		t->respond(t, STATUS_OS2_INVALID_LEVEL);
	else {
		t->out.data.p += n;
		t->respond(t, 0);
	}
out:
	free(name);
	free(path);
	free(d);
}

void
trans2queryfileinformation(Trans *t)
{
	int n, fid, level;
	Tree *tree;
	File *f;
	Dir *d;

	f = nil;
	d = nil;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "ww", &fid, &level)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((f = getfile(t->r->tid, fid, &tree, &n)) == nil){
		t->respond(t, n);
		goto out;
	}
	if((d = statfile(f)) == nil){
		t->respond(t, smbmkerror());
		goto out;
	}
	pack(t->out.param.b, t->out.param.p, t->out.param.e, "__.", &t->out.param.p);
	if((n = qpackdir(t->r, d, tree, f, level, t->out.data.b, t->out.data.p, t->out.data.e)) < 0)
		t->respond(t, STATUS_OS2_INVALID_LEVEL);
	else {
		t->out.data.p += n;
		t->respond(t, 0);
	}
out:
	putfile(f);
	free(d);
}

static int
setfilepathinformation(Req *r, Dir *d, Tree *t, File *f, char *path, int level, uchar *b, uchar *p, uchar *e)
{
	int attr, adt, atm, mdt, mtm, delete;
	vlong len, ctime, atime, mtime, mode, uid, gid;
	Dir nd;

	nulldir(&nd);
	if(debug)
		fprint(2, "SET level %.4x\n", level);
	switch(level){
	case 0x0001:	/* SMB_INFO_STANDARD */
		if(!unpack(b, p, e, "____wwww__________", &adt, &atm, &mdt, &mtm))
			goto unsup;
		nd.atime = fromdatetime(adt, atm)-tzoff;
		nd.mtime = fromdatetime(mdt, mtm)-tzoff;
		break;

	case 0x0101:	/* SMB_SET_FILE_BASIC_INFO */
		if(f == nil || !unpack(b, p, e, "________vv________l____", &atime, &mtime, &attr))
			goto unsup;
		if(atime && atime != -1LL)
			nd.atime = fromfiletime(atime);
		if(mtime && mtime != -1LL)
			nd.mtime = fromfiletime(mtime);
		if(attr){
			if(attr & ATTR_READONLY){
				if(d->mode & 0222)
					nd.mode = d->mode & ~0222;
			} else {
				if((d->mode & 0222) == 0)
					nd.mode = d->mode | 0222;
			}
		}
		break;

	case 0x0102:	/* SMB_SET_FILE_DISPOSITION_INFO */
		if(f == nil || !unpack(b, p, e, "b", &delete))
			goto unsup;
		if((f->dacc & FILE_DELETE) == 0)
			return STATUS_ACCESS_DENIED;
		deletefile(f, delete);
		break;

	case 0x0103:	/* SMB_SET_FILE_ALLOCATION_INFO */
	case 0x0104:	/* SMB_SET_FILE_END_OF_FILE_INFO */
		if(f == nil || !unpack(b, p, e, "v", &len))
			goto unsup;
		if(d->qid.type & QTDIR)
			return STATUS_OS2_INVALID_ACCESS;
		if(len != -1LL)
			nd.length = len;
		break;

	case 0x0200:	/* SMB_SET_FILE_UNIX_BASIC */
		if(!unpack(b, p, e, "v________vvvvv____________________________v________",
			&len, &ctime, &atime, &mtime, &uid, &gid, &mode))
			goto unsup;
		if(len != -1LL)
			nd.length = len;
		if(atime && atime != -1LL)
			nd.atime = fromfiletime(atime);
		if(mtime && mtime != -1LL)
			nd.mtime = fromfiletime(mtime);
		else if(ctime && ctime != -1LL)
			nd.mtime = fromfiletime(ctime);
		if(uid != -1LL){
			if((nd.uid = unixname(t->share, (int)uid, 0)) == nil)
				return STATUS_SMB_BAD_UID;
		}
		if(gid != -1LL){
			if((nd.gid = unixname(t->share, (int)gid, 1)) == nil)
				return STATUS_SMB_BAD_UID;
		}
		if(mode != -1LL)
			nd.mode = (d->mode & ~0777) | (mode & 0777);
		break;

	default:
		logit("[%.4x] unknown SET infolevel", level);
		return STATUS_OS2_INVALID_LEVEL;
	unsup:
		return STATUS_NOT_SUPPORTED;
	}
	if(debug)
		fprint(2, "wstat\nmode %lo\natime %ld\nmtime %ld\nlength %llux\nuid %s\ngid %s\n",
			nd.mode, nd.atime, nd.mtime, nd.length, nd.uid, nd.gid);
	if(((f && f->fd >= 0) ? dirfwstat(f->fd, &nd) : dirwstat(path, &nd)) < 0)
		return smbmkerror();
	xdirflush(path, r->namecmp);
	return 0;
}

void
trans2setpathinformation(Trans *t)
{
	int err, level;
	Tree *tree;
	char *name, *path;
	Dir *d;

	d = nil;
	name = path = nil;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "w____f", &level, 
		t->o->nameunpack, &name)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((path = getpath(t->r->tid, name, &tree, &err)) == nil)
		goto errout;
	if((d = xdirstat(&path, t->namecmp)) == nil){
		t->respond(t, smbmkerror());
		goto out;
	}
	if(err = setfilepathinformation(t->r, d, tree, nil, path, level, t->in.data.b, t->in.data.p, t->in.data.e)){
errout:
		t->respond(t, err);
		goto out;
	}
	pack(t->out.param.b, t->out.param.p, t->out.param.e, "__.", &t->out.param.p);
	t->respond(t, 0);
out:
	free(name);
	free(path);
	free(d);
}

void
trans2setfileinformation(Trans *t)
{
	int err, fid, level;
	Tree *tree;
	File *f;
	Dir *d;

	f = nil;
	d = nil;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "ww__", &fid, &level)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((f = getfile(t->r->tid, fid, &tree, &err)) == nil)
		goto errout;
	if((d = statfile(f)) == nil){
		t->respond(t, smbmkerror());
		goto out;
	}
	if(err = setfilepathinformation(t->r, d, tree, f, f->path, level, t->in.data.b, t->in.data.p, t->in.data.e)){
errout:
		t->respond(t, err);
		goto out;
	}
	pack(t->out.param.b, t->out.param.p, t->out.param.e, "__.", &t->out.param.p);
	t->respond(t, 0);
out:
	putfile(f);
	free(d);
}

enum {
	FILE_CASE_SENSITIVE_SEARCH = 1,
	FILE_CASE_PRESERVED_NAMES = 2,
	FILE_UNICODE_ON_DISK = 4,
};

void
trans2queryfsinformation(Trans *t)
{
	int n, level;
	Share *share;
	Tree *tree;
	char *s;

	s = nil;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "w", &level)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if((tree = gettree(t->r->tid)) == nil){
		t->respond(t, STATUS_SMB_BAD_TID);
		goto out;
	}
	share = tree->share;
	if(debug)
		fprint(2, "FS level %.4x\n", level);
	switch(level){
	case 0x0001:	/* SMB_INFO_ALLOCATION */
		n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "llllw", 
			0x00000000, share->blocksize/share->sectorsize,
			filesize32(allocsize(share->allocsize+share->freesize, share->blocksize)/share->blocksize),
			filesize32(allocsize(share->freesize, share->blocksize)/share->blocksize),
			share->sectorsize);
		break;

	case 0x0002:	/* SMB_INFO_VOLUME */
		s = smprint("%.12s", share->name);
		n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "l#0b{f}",
			(int)namehash(share->root), smbuntermstrpack8, s);
		break;

	case 0x0102:	/* SMB_QUERY_FS_VOLUME_INFO */
		s = smprint("%.12s", share->name);
		n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "vl#0l__{f}",
			tofiletime(starttime), (int)namehash(share->root), smbuntermstrpack16, s);
		break;

	case 0x0103:	/* SMB_QUERY_FS_SIZE_INFO */
		n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "vvll", 
			allocsize(share->allocsize+share->freesize, share->blocksize)/share->blocksize,
			allocsize(share->freesize, share->blocksize)/share->blocksize, 
			share->blocksize/share->sectorsize,
			share->sectorsize);
		break;

	case 0x0105:	/* SMB_QUERY_FS_ATTRIBUTE_INFO */
		n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "ll#0l{f}", 
			FILE_CASE_SENSITIVE_SEARCH |
			FILE_CASE_PRESERVED_NAMES |
			FILE_UNICODE_ON_DISK,
			share->namelen, smbuntermstrpack16, share->fsname);
		break;

	case 0x0200:	/* SMB_QUERY_CIFS_UNIX_INFO */
		n = pack(t->out.data.b, t->out.data.p, t->out.data.e, "wwv", 1, 0, 0x800000);
		break;

	default:
		logit("[%.4x] unknown FS infolevel", level);
		t->respond(t, STATUS_OS2_INVALID_LEVEL);
		goto out;
	}
	if(n <= 0)
		t->respond(t, STATUS_INVALID_SMB);
	else {
		t->out.data.p += n;
		t->respond(t, 0);
	}
out:
	free(s);
}

enum {
	SMB_FIND_CLOSE_AFTER_REQUEST = 0x1,
	SMB_FIND_CLOSE_AT_EOS = 0x2,
	SMB_FIND_RETURN_RESUME_KEYS = 0x4,
	SMB_FIND_CONTINUE_FROM_LAST = 0x8,
};

void
trans2findfirst2(Trans *t)
{
	int i, nsid, eos, n, attr, count, flags, level;
	uchar *prevoff, *nameoff;
	char *name, *path;
	Tree *tree;
	Find *f;
	Dir *d;

	f = nil;
	name = path = nil;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "wwww____f", 
		&attr, &count, &flags, &level, t->o->nameunpack, &name)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if(debug)
		fprint(2, "FIND level %.4x\n", level);
	if((path = getpath(t->r->tid, name, &tree, &n)) == nil){
		t->respond(t, n);
		goto out;
	}
	if((f = openfind(path, t->namecmp, attr, 1, &n)) == nil){
		t->respond(t, n);
		goto out;
	}
	n = eos = 0;
	prevoff = nameoff = nil;
	for(i = 0; i < count; i++){
		if((eos = readfind(f, f->index, &d)) < 0)
			break;
		if((n = fpackdir(t->r, d, tree, 0, level,
			t->out.data.b, t->out.data.p, t->out.data.e, 
			&prevoff, &nameoff)) <= 0)
			break;
		t->out.data.p += n;
		f->index = eos + 1;
	}
	if((n < 0) || (flags & SMB_FIND_CLOSE_AFTER_REQUEST) || 
	   ((flags & SMB_FIND_CLOSE_AT_EOS) && (eos < 0))){
		if(n < 0){
			t->respond(t, STATUS_OS2_INVALID_LEVEL);
			goto out;
		}
		eos = -1;
		nsid = 0;
	} else {
		nsid = newsid(tree, f);
	}
	if(!i && (eos < 0)){
		t->respond(t, STATUS_NO_MORE_FILES);
		goto out;
	}
	if(!pack(t->out.param.b, t->out.param.p, t->out.param.e, "wwwww.",
		nsid, i, (eos < 0), 0, (int)(nameoff - t->out.data.b), &t->out.param.p)){
		t->respond(t, STATUS_INVALID_SMB);
		delsid(tree, nsid);
	} else
		t->respond(t, 0);
out:
	free(name);
	free(path);
	putfind(f);
}

void
trans2findnext2(Trans *t)
{
	int i, n, eos, sid, count, level, index, flags;
	uchar *prevoff, *nameoff;
	char *name;
	Tree *tree;
	Find *f;
	Dir *d;

	f = nil;
	name = nil;
	if(!unpack(t->in.param.b, t->in.param.p, t->in.param.e, "wwwlwf", 
		&sid, &count, &level, &index, &flags, t->o->nameunpack, &name)){
		t->respond(t, STATUS_NOT_SUPPORTED);
		goto out;
	}
	if(debug)
		fprint(2, "FIND level %.4x\n", level);
	if((f = getfind(t->r->tid, sid, &tree, &n)) == nil){
		t->respond(t, n);
		goto out;
	}
	n = eos = 0;
	if((flags & SMB_FIND_CONTINUE_FROM_LAST) == 0){
		f->index = 0;
		while((eos = readfind(f, f->index, &d)) >= 0){
			f->index = eos + 1;
			if(strcmp(name, d->name) == 0)
				break;
		}
	}
	prevoff = nameoff = nil;
	for(i = 0; i < count; i++){
		if((eos = readfind(f, f->index, &d)) < 0)
			break;
		if((n = fpackdir(t->r, d, tree, 0, level,
			t->out.data.b, t->out.data.p, t->out.data.e, 
			&prevoff, &nameoff)) <= 0)
			break;
		t->out.data.p += n;
		f->index = eos + 1;
	}
	if((flags & SMB_FIND_CLOSE_AFTER_REQUEST) || 
	   ((flags & SMB_FIND_CLOSE_AT_EOS) && (eos < 0))){
		delsid(tree, sid);
		eos = -1;
	}
	if(!i && (eos < 0)){
		t->respond(t, STATUS_NO_MORE_FILES);
		goto out;
	}
	if(!pack(t->out.param.b, t->out.param.p, t->out.param.e, "wwww.",
		i, (eos < 0), 0, (int)(nameoff - t->out.data.b), &t->out.param.p))
		t->respond(t, STATUS_INVALID_SMB);
	else
		t->respond(t, 0);
out:
	free(name);
	putfind(f);
}

static void
transrespond(Trans *t, int err)
{
	Req *r;

	r = t->r;
	t->r = nil;
	t->respond = nil;
	if(!err && !pack(r->rh, r->rp, r->re, 
		"#0b{*2ww__#3w@3ww#4w@4ww#1b_[*2]}#2w{%4[]%4[]}.",
		t->out.param.p - t->out.param.b, 
		t->out.data.p - t->out.data.b,
		0, 0,
		t->out.setup.b, t->out.setup.p,
		t->out.param.b, t->out.param.p,
		t->out.data.b, t->out.data.p, &r->rp))
		r->respond(r, STATUS_INVALID_SMB);
	else
		r->respond(r, err);
	free(t->out.param.b);
	free(t->out.data.b);
	free(t->out.setup.b);
}

struct {
	char *name;
	void (*fun)(Trans *t);
} transoptab[] = {
	[0x0000] { "TRANS_RAP", transrap },
}, trans2optab[] = {
	[0x0001] { "TRANS2_FIND_FIRST2", trans2findfirst2 },
	[0x0002] { "TRANS2_FIND_NEXT2", trans2findnext2 },
	[0x0003] { "TRANS2_QUERY_FS_INFORMATION", trans2queryfsinformation },
	[0x0005] { "TRANS2_QUERY_PATH_INFORMATION", trans2querypathinformation },
	[0x0007] { "TRANS2_QUERY_FILE_INFORMATION", trans2queryfileinformation },
	[0x0006] { "TRANS2_SET_PATH_INFORMATION", trans2setpathinformation },
	[0x0008] { "TRANS2_SET_FILE_INFORMATION", trans2setfileinformation },
};

void
smbtransaction(Req *r, uchar *h, uchar *p, uchar *e)
{
	int tpc, tdc, rpc, rdc, rsc;
	uchar *sa, *se, *da, *de, *pa, *pe;
	void (*fun)(Trans *t);
	Trans t;

	t.r = r;
	t.o = r->o;
	t.namecmp = r->namecmp;
	t.cmd = 0;
	t.respond = transrespond;
	if(!unpack(h, p, e, "#0b{*2wwwwb_w______#3w@3w#4w@4w#1b_[*2]}#2w{[?][?]}",
		&tpc, &tdc, &rpc, &rdc, &rsc, &t.flags, &sa, &se, &pa, &pe, &da, &de)){
unsup:
		r->respond(r, STATUS_NOT_SUPPORTED);
		return;
	}
	unpack(sa, sa, se, "w", &t.cmd);

	switch(r->cmd){
	case 0x25:	/* SMB_COM_TRANSACTION */
		if((t.cmd >= nelem(transoptab)) || ((fun = transoptab[t.cmd].fun) == nil)){
			logit("[%.4x] transaction subcommand not implemented", t.cmd);
			goto unsup;
		}
		t.name = transoptab[t.cmd].name;
		break;
	case 0x32:	/* SMB_COM_TRANSACTION2 */
		if((t.cmd >= nelem(trans2optab)) || ((fun = trans2optab[t.cmd].fun) == nil)){
			logit("[%.4x] transaction2 subcommand not implemented", t.cmd);
			goto unsup;
		}
		t.name = trans2optab[t.cmd].name;
		break;
	default:
		goto unsup;
	}

	if((tpc > (pe - pa)) || (tdc > (de - da))){
		logit("[%.4x] %s request truncated", t.cmd, t.name);
		goto unsup;
	}
	if(57+((rsc+1)&~1)+((rpc+3)&~3)+((rdc+3)&~3) > remotebuffersize){
		rdc = remotebuffersize-(57+((rsc+1)&~1)+((rpc+3)&~3)) & ~3;
		if(rdc <= 0){
			logit("[%.4x] %s response doesnt fit in client buffer", t.cmd, t.name);
			goto unsup;
		}
	}

	t.in.param.b = t.in.param.p = pa; t.in.param.e = pe;
	t.in.data.b = t.in.data.p = da; t.in.data.e = de;
	t.in.setup.b = t.in.setup.p = sa; t.in.setup.e = se;

	t.out.param.b = t.out.param.p = t.out.param.e = (rpc > 0) ? malloc(rpc) : nil;
	t.out.param.e += rpc;
	t.out.data.b = t.out.data.p = t.out.data.e = (rdc > 0) ? malloc(rdc) : nil;
	t.out.data.e += rdc;
	t.out.setup.b = t.out.setup.p = t.out.setup.e = (rsc > 0) ? malloc(rsc) : nil;
	t.out.setup.e += rsc;

	if(debug)
		fprint(2, "[%.4x] %s\n", t.cmd, t.name);
	(*fun)(&t);
}

void
smbnoandxcommand(Req *r, uchar *, uchar *, uchar *)
{
	r->respond(r, (r->cmd == 0xFF) ? STATUS_SMB_BAD_COMMAND : 0);
}

struct {
	char *name;
	void (*fun)(Req *, uchar *, uchar *, uchar *);
} optab[] = {
	[0x00] { "SMB_COM_CREATE_DIRECTORY", smbcreatedirectory },
	[0x01] { "SMB_COM_DELETE_DIRECTORY", smbdeletedirectory },
	[0x04] { "SMB_COM_CLOSE", smbcloseflush },
	[0x05] { "SMB_COM_FLUSH", smbcloseflush },
	[0x06] { "SMB_COM_DELETE", smbdelete },
	[0x07] { "SMB_COM_RENAME", smbrename },
	[0x08] { "SMB_COM_QUERY_INFORMATION", smbqueryinformation },
	[0x09] { "SMB_COM_SET_INFORMATION", smbsetinformation },
	[0x10] { "SMB_CON_CHECK_DIRECTORY", smbcheckdirectory },
	[0x0b] { "SMB_COM_WRITE", smbwrite },
	[0x23] { "SMB_COM_QUERY_INFORMATION2", smbqueryinformation2 },
	[0x24] { "SMB_COM_LOCKING_ANDX", smblockingandx },
	[0x25] { "SMB_COM_TRANSACTION", smbtransaction },
	[0x27] { "SMB_COM_IOCTL", smbstatusnotimplemented },
	[0x28] { "SMB_COM_IOCTL_SECONDARY", smbstatusnotimplemented },
	[0x29] { "SMB_COM_COPY", smbstatusnotimplemented },
	[0x2a] { "SMB_COM_MOVE", smbstatusnotimplemented },
	[0x2b] { "SMB_COM_ECHO", smbecho },
	[0x2d] { "SMB_COM_OPEN_ANDX", smbopenandx },
	[0x2e] { "SMB_COM_READ_ANDX", smbreadandx },
	[0x2f] { "SMB_COM_WRITE_ANDX", smbwriteandx },
	[0x32] { "SMB_COM_TRANSACTION2", smbtransaction },
	[0x34] { "SMB_COM_FIND_CLOSE2", smbcloseflush },
	[0x40] { "SMB_IOS", smbnoandxcommand },
	[0x71] { "SMB_COM_DISCONNECT_TREE", smbdisconnecttree },
	[0x72] { "SMB_COM_NEGOTIATE", smbnegotiate },
	[0x73] { "SMB_COM_SESSION_SETUP_ANX", smbsessionsetupandx },
	[0x74] { "SMB_COM_LOGOFF_ANDX", smblogoffandx },
	[0x75] { "SMB_COM_TREE_CONNECT_ANDX", smbtreeconnectandx },
	[0x80] { "SMB_COM_QUERY_INFORMATION_DISK", smbqueryinformationdisk },
	[0xa2] { "SMB_COM_NT_CREATE_ANDX", smbntcreatendx },
	[0xFF] { "SMB_COM_NO_ANDX_COMMAND", smbnoandxcommand },
};

void
smbcmd(Req *r, int cmd, uchar *h, uchar *p, uchar *e)
{
	if((cmd >= nelem(optab)) || (optab[cmd].fun == nil)){
		logit("[%.2x] command not implemented", cmd);
		r->respond(r, STATUS_NOT_SUPPORTED);
		return;
	}
	r->name = optab[cmd].name;
	if(debug)
		fprint(2, "[%.2x] %s\n", cmd, r->name);
	if((!negotiated && cmd != 0x72) || (negotiated && cmd == 0x72)){
		r->respond(r, STATUS_INVALID_SMB);
		return;
	}
	if(!remoteuser){
		switch(cmd){
		case 0x72: /* SMB_COM_NEGOTIATE */
		case 0x73: /* SMB_COM_SESSION_SETUP_ANX */
		case 0x74: /* SMB_COM_LOGOFF_ANDX */
		case 0xFF: /* SMB_COM_NO_ANDX_COMMAND */
			break;
		default:
			logit("auth not completed in %s request", r->name);
		case 0x75: /* SMB_COM_TREE_CONNECT_ANDX */
			r->respond(r, STATUS_LOGON_FAILURE);
			return;
		}
	} else if(r->uid != sessionuid){
		switch(cmd){
		case 0x73: /* SMB_COM_SESSION_SETUP_ANX */
		case 0x2b: /* SMB_COM_ECHO */
			break;
		default:
			logit("bad uid %.4x in %s request", r->uid, r->name);
			r->respond(r, STATUS_SMB_BAD_UID);
			return;
		}
	}
	(*optab[cmd].fun)(r, h, p, e);
}
