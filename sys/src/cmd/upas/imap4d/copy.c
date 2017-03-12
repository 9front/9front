#include "imap4d.h"
#include <libsec.h>

int
copycheck(Box*, Msg *m, int, void *)
{
	int fd;

	if(m->expunged)
		return 0;
	fd = msgfile(m, "rawunix");
	if(fd < 0){
		msgdead(m);
		return 0;
	}
	close(fd);
	return 1;
}

static int
opendeliver(int *pip, char *folder, char *from, long t)
{
	char *av[7], buf[32];
	int i, pid, fd[2];

	if(pipe(fd) != 0)
		sysfatal("pipe: %r");
	pid = fork();
	switch(pid){
	case -1:
		return -1;
	case 0:
		av[0] = "mbappend";
		av[1] = folder;
		i = 2;
		if(from){
			av[i++] = "-f";
			av[i++] = from;
		}
		if(t != 0){
			snprint(buf, sizeof buf, "%ld", t);
			av[i++] = "-t";
			av[i++] = buf;
		}
		av[i] = 0;
		close(0);
		dup(fd[1], 0);
		if(fd[1] != 0)
			close(fd[1]);
		close(fd[0]);
		exec("/bin/upas/mbappend", av);
		ilog("exec: %r");
		_exits("b0rked");
		return -1;
	default:
		*pip = fd[0];
		close(fd[1]);
		return pid;
	}
}

static int
closedeliver(int pid, int fd)
{
	int nz, wpid;
	Waitmsg *w;

	close(fd);
	while(w = wait()){
		nz = !w->msg || !w->msg[0];
		wpid = w->pid;
		free(w);
		if(wpid == pid)
			return nz? 0: -1;
	}
	return -1;
}

/*
 * we're going to all this trouble of fiddling the .imp file for
 * the target mailbox because we wish to save the flags.  we
 * should be using upas/fs's flags instead.
 *
 * note.  appendmb for mbox fmt wants to lock the directory.  
 * since the locking is intentionally broken, we could get by
 * with aquiring the lock before we fire up appendmb and
 * trust that he doesn't worry if he does acquire the lock.
 * instead, we'll just do locking around the .imp file.
 */
static int
savemsg(char *dst, int flags, char *head, int nhead, Biobuf *b, long n, Uidplus *u)
{
	char *digest, buf[Bufsize + 1], digbuf[Ndigest + 1], folder[Pathlen];
	uchar shadig[SHA1dlen];
	int i, fd, pid, nr, ok;
	DigestState *dstate;
	Mblock *ml;

	snprint(folder, sizeof folder, "%s/%s", mboxdir, dst);
	pid = opendeliver(&fd, folder, 0, 0);
	if(pid == -1)
		return 0;
	ok = 1;
	dstate = sha1(nil, 0, nil, nil);
	if(nhead){
		sha1((uchar*)head, nhead, nil, dstate);
		if(write(fd, head, nhead) != nhead){
			ok = 0;
			goto loose;
		}
	}
	while(n > 0){
		nr = n;
		if(nr > Bufsize)
			nr = Bufsize;
		nr = Bread(b, buf, nr);
		if(nr <= 0){
			ok = 0;
			break;
		}
		n -= nr;
		sha1((uchar*)buf, nr, nil, dstate);
		if(write(fd, buf, nr) != nr){
			ok = 0;
			break;
		}
	}
loose:
	closedeliver(pid, fd);
	sha1(nil, 0, shadig, dstate);
	if(ok){
		digest = digbuf;
		for(i = 0; i < SHA1dlen; i++)
			sprint(digest + 2*i, "%2.2ux", shadig[i]);
		ml = mblock();
		if(ml == nil)
			return 0;
		ok = appendimp(dst, digest, flags, u) == 0;
		mbunlock(ml);
	}
	return ok;
}

static int
copysave(Box*, Msg *m, int, void *vs, Uidplus *u)
{
	int ok, fd;
	vlong length;
	Biobuf b;
	Dir *d;

	if(m->expunged)
		return 0;
	if((fd = msgfile(m, "rawunix")) == -1){
		msgdead(m);
		return 0;
	}
	if((d = dirfstat(fd)) == nil){
		close(fd);
		return 0;
	}
	length = d->length;
	free(d);

	Binit(&b, fd, OREAD);
	ok = savemsg(vs, m->flags, 0, 0, &b, length, u);
	Bterm(&b);
	close(fd);
	return ok;
}

int
copysaveu(Box *box, Msg *m, int i, void *vs)
{
	int ok;
	Uidplus *u;

	u = binalloc(&parsebin, sizeof *u, 1);
	ok = copysave(box, m, i, vs, u);
	*uidtl = u;
	uidtl = &u->next;
	return ok;
}


/*
 * first spool the input into a temorary file,
 * and massage the input in the process.
 * then save to real box.
 */
/*
 * copy from bin to bout,
 * map "\r\n" to "\n" and
 * return the number of bytes in the mapped file.
 *
 * exactly n bytes must be read from the input,
 * unless an input error occurs.
 */
static long
spool(Biobuf *bout, Biobuf *bin, long n)
{
	int c;

	while(n > 0){
		c = Bgetc(bin);
		n--;
		if(c == '\r' && n-- > 0){
			c = Bgetc(bin);
			if(c != '\n')
				Bputc(bout, '\r');
		}
		if(c < 0)
			return -1;
		if(Bputc(bout, c) < 0)
			return -1;
	}
	if(Bflush(bout) < 0)
		return -1;
	return Boffset(bout);
}

int
appendsave(char *mbox, int flags, char *head, Biobuf *b, long n, Uidplus *u)
{
	int fd, ok;
	Biobuf btmp;

	fd = imaptmp();
	if(fd < 0)
		return 0;
	Bprint(&bout, "+ Ready for literal data\r\n");
	if(Bflush(&bout) < 0)
		writeerr();
	Binit(&btmp, fd, OWRITE);
	n = spool(&btmp, b, n);
	Bterm(&btmp);
	if(n < 0){
		close(fd);
		return 0;
	}

	seek(fd, 0, 0);
	Binit(&btmp, fd, OREAD);
	ok = savemsg(mbox, flags, head, strlen(head), &btmp, n, u);
	Bterm(&btmp);
	close(fd);
	return ok;
}
