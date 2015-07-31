/*
 * iostats - Gather file system information
 */
#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>

#define DEBUGFILE	"iostats.out"
#define DONESTR		"done"

enum{
	Maxfile		= 1000,	/* number of Files we'll log */
};

typedef struct File File;
typedef struct Fid Fid;
typedef struct Req Req;
typedef struct Rpc Rpc;
typedef struct Stats Stats;

/* per file statistics */
struct File
{
	Qid	qid;
	char	*path;

	ulong	nopen;

	ulong	nread;
	vlong	bread;

	ulong	nwrite;
	vlong	bwrite;
};

/* fid context */
struct Fid
{
	int	fid;
	Qid	qid;
	char	*path;
	File	*file;	/* set on open/create */
	Fid	*next;
};

/* a request */
struct Req
{
	Req	*next;
	vlong	t;

	Fcall	f;
	uchar	buf[];
};

/* per rpc statistics */
struct Rpc
{
	char	*name;
	ulong	count;
	vlong	time;
	vlong	lo;
	vlong	hi;
	vlong	bin;
	vlong	bout;
};

/* all the statistics */
struct Stats
{
	vlong	totread;
	vlong	totwrite;
	ulong	nrpc;
	vlong	nproto;
	Rpc	rpc[Tmax];
	File	file[Maxfile];
};

Stats	stats[1];

int pfd[2];
int efd[2];
int done;

Lock rqlock;
Req *rqhead;

Fid *fidtab[1024];

void
catcher(void *a, char *msg)
{
	USED(a);

	if(strcmp(msg, DONESTR) == 0) {
		done = 1;
		noted(NCONT);
	}
	if(strcmp(msg, "exit") == 0)
		exits("exit");

	noted(NDFLT);
}

void
update(Rpc *rpc, vlong t)
{
	vlong t2;

	t2 = nsec();
	t = t2 - t;
	if(t < 0)
		t = 0;

	rpc->time += t;
	if(t < rpc->lo)
		rpc->lo = t;
	if(t > rpc->hi)
		rpc->hi = t;
}

Fid**
fidhash(int fid)
{
	return &fidtab[fid % nelem(fidtab)];
}

Fid*
getfid(int fid, int new)
{
	Fid *f, **ff;

	ff = fidhash(fid);
	for(f = *ff; f != nil; f = f->next){
		if(f->fid == fid)
			return f;
	}
	if(new){
		f = mallocz(sizeof(*f), 1);
		f->fid = fid;
		f->next = *ff;
		*ff = f;
	}
	return f;
}

void
setfid(Fid *f, char *path, Qid qid)
{
	if(path != f->path){
		free(f->path);
		f->path = path;
	}
	f->qid = qid;
	f->file = nil;
}

void
rattach(Fcall *fin, Fcall *fout)
{
	setfid(getfid(fin->fid, 1), strdup("/"), fout->qid);
}

void
rwalk(Fcall *fin, Fcall *fout)
{
	Fid *of, *f;
	int i;

	if((of = getfid(fin->fid, 0)) == nil)
		return;
	f = getfid(fin->newfid, 1);
	if(f != of)
		setfid(f, strdup(of->path), of->qid);
	for(i=0; i<fout->nwqid; i++)
		setfid(f, cleanname(smprint("%s/%s", f->path, fin->wname[i])), fout->wqid[i]);
}

void
ropen(Fcall *fin, Fcall *fout)
{
	File *fs;
	Fid *f;

	if((f = getfid(fin->fid, 0)) == nil)
		return;
	if(fin->type == Tcreate)
		setfid(f, cleanname(smprint("%s/%s", f->path, fin->name)), fout->qid);
	else
		setfid(f, f->path, fout->qid);
	for(fs = stats->file; fs < &stats->file[Maxfile]; fs++){
		if(fs->nopen == 0){
			fs->path = strdup(f->path);
			fs->qid = f->qid;
			f->file = fs;
			break;
		}
		if(fs->qid.path == f->qid.path && strcmp(fs->path, f->path) == 0){
			f->file = fs;
			break;
		}
	}
	if(f->file != nil)
		f->file->nopen++;
}

void
rclunk(Fcall *fin)
{
	Fid **ff, *f;

	for(ff = fidhash(fin->fid); (f = *ff) != nil; ff = &f->next){
		if(f->fid == fin->fid){
			*ff = f->next;
			free(f->path);
			free(f);
			return;
		}
	}
}

void
rio(Fcall *fin, Fcall *fout)
{
	Fid *f;
	int count;

	count = fout->count;
	if((f = getfid(fin->fid, 0)) == nil)
		return;
	switch(fout->type){
	case Rread:
		if(f->file != nil){
			f->file->nread++;
			f->file->bread += count;
		}
		stats->totread += count;
		break;
	case Rwrite:
		if(f->file != nil){
			f->file->nwrite++;
			f->file->bwrite += count;
		}
		stats->totwrite += count;
		break;
	}
}

void
usage(void)
{
	fprint(2, "usage: iostats [-dC] [-f debugfile] cmds [args ...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Rpc *rpc;
	ulong ttime;
	char *dbfile;
	char buf[64*1024];
	float brpsec, bwpsec, bppsec;
	int cpid, fspid, expid, rspid, dbg, n, mflag;
	char *fds[3];
	Waitmsg *w;
	File *fs;
	Req *r;

	dbg = 0;
	mflag = MREPL;
	dbfile = DEBUGFILE;

	ARGBEGIN{
	case 'd':
		dbg++;
		break;
	case 'f':
		dbfile = ARGF();
		break;
	case 'C':
		mflag |= MCACHE;
		break;
	default:
		usage();
	}ARGEND

	USED(dbfile);

	if(argc == 0)
		usage();

	if(pipe(pfd) < 0)
		sysfatal("pipe: %r");

	/* dup std fds to be inherited to exportfs */
	fds[0] = smprint("/fd/%d", dup(0, -1));
	fds[1] = smprint("/fd/%d", dup(1, -1));
	fds[2] = smprint("/fd/%d", dup(2, -1));

	switch(cpid = fork()) {
	case -1:
		sysfatal("fork: %r");
	case 0:
		close(pfd[1]);
		close(atoi(strrchr(fds[0], '/')+1));
		close(atoi(strrchr(fds[1], '/')+1));
		close(atoi(strrchr(fds[2], '/')+1));

		if(getwd(buf, sizeof(buf)) == 0)
			sysfatal("no working directory: %r");

		rfork(RFENVG|RFNAMEG);

		if(mount(pfd[0], -1, "/", mflag, "") < 0)
			sysfatal("mount /: %r");

		/* replace std fds with the exported ones */
		close(0); open(fds[0], OREAD);
		close(1); open(fds[1], OWRITE);
		close(2); open(fds[2], OWRITE);

		bind("#c/pid", "/dev/pid", MREPL);
		bind("#c/ppid", "/dev/ppid", MREPL);
		bind("#e", "/env", MREPL|MCREATE);
		bind("#d", "/fd", MREPL);

		if(chdir(buf) < 0)
			sysfatal("chdir: %r");

		exec(*argv, argv);
		if(**argv != '/' && strncmp(*argv, "./", 2) != 0 && strncmp(*argv, "../", 3) != 0)
			exec(smprint("/bin/%s", *argv), argv);
		sysfatal("exec: %r");
	default:
		close(pfd[0]);
	}

	/* isolate us from interrupts */
	rfork(RFNOTEG);
	if(pipe(efd) < 0)
		sysfatal("pipe: %r");

	/* spawn exportfs */
	switch(expid = fork()) {
	default:
		close(efd[0]);
		close(atoi(strrchr(fds[0], '/')+1));
		close(atoi(strrchr(fds[1], '/')+1));
		close(atoi(strrchr(fds[2], '/')+1));
		break;
	case -1:
		sysfatal("fork: %r");
	case 0:
		dup(efd[0], 0);
		close(efd[0]);
		close(efd[1]);
		close(pfd[1]);
		if(dbg){
			execl("/bin/exportfs", "exportfs", "-df", dbfile, "-r", "/", nil);
		} else {
			execl("/bin/exportfs", "exportfs", "-r", "/", nil);
		}
		sysfatal("exec: %r");
	}

	switch(fspid = fork()) {
	default:
		close(pfd[1]);
		close(efd[1]);

		buf[0] = '\0';
		while((w = wait()) != nil && (cpid != -1 || fspid != -1 || expid != -1)){
			if(w->pid == fspid)
				fspid = -1;
			else if(w->pid == expid)
				expid = -1;
			else if(w->pid == cpid){
				cpid = -1;
				strcpy(buf, w->msg);
				if(fspid != -1)
					postnote(PNPROC, fspid, DONESTR);
			}
			if(buf[0] == '\0')
				strcpy(buf, w->msg);
			free(w);
		}
		exits(buf);
	case -1:
		sysfatal("fork: %r");
	case 0:
		notify(catcher);
		break;
	}

	stats->rpc[Tversion].name = "version";
	stats->rpc[Tauth].name = "auth";
	stats->rpc[Tflush].name = "flush";
	stats->rpc[Tattach].name = "attach";
	stats->rpc[Twalk].name = "walk";
	stats->rpc[Topen].name = "open";
	stats->rpc[Tcreate].name = "create";
	stats->rpc[Tclunk].name = "clunk";
	stats->rpc[Tread].name = "read";
	stats->rpc[Twrite].name = "write";
	stats->rpc[Tremove].name = "remove";
	stats->rpc[Tstat].name = "stat";
	stats->rpc[Twstat].name = "wstat";

	for(n = 0; n < nelem(stats->rpc); n++)
		stats->rpc[n].lo = 10000000000LL;

	switch(rspid = rfork(RFPROC|RFMEM)) {
	case 0:
		/* read response from exportfs and pass to mount */
		while(!done){
			uchar tmp[sizeof(buf)];
			Fcall f;
			Req **rr;

			n = read(efd[1], buf, sizeof(buf));
			if(n < 0)
				break;
			if(n == 0)
				continue;

			/* convert response */
			memset(&f, 0, sizeof(f));
			memmove(tmp, buf, n);
			if(convM2S(tmp, n, &f) != n)
				sysfatal("convM2S: %r");

			/* find request to this response */
			lock(&rqlock);
			for(rr = &rqhead; (r = *rr) != nil; rr = &r->next){
				if(r->f.tag == f.tag){
					*rr = r->next;
					r->next = nil;
					break;
				}
			}
			stats->nproto += n;
			unlock(&rqlock);

			switch(f.type){
			case Ropen:
			case Rcreate:
				ropen(&r->f, &f);
				break;
			case Rclunk:
			case Rremove:
				rclunk(&r->f);
				break;
			case Rattach:
				rattach(&r->f, &f);
				break;
			case Rwalk:
				rwalk(&r->f, &f);
				break;
			case Rread:
			case Rwrite:
				rio(&r->f, &f);
				break;
			}

			rpc = &stats->rpc[r->f.type];
			update(rpc, r->t);
			rpc->bout += n;
			free(r);

			if(write(pfd[1], buf, n) != n)
				break;
		}
		exits(nil);
	default:
		/* read request from mount and pass to exportfs */
		while(!done){
			n = read(pfd[1], buf, sizeof(buf));
			if(n < 0)
				break;
			if(n == 0)
				continue;

			r = mallocz(sizeof(*r) + n, 1);
			memmove(r->buf, buf, n);
			if(convM2S(r->buf, n, &r->f) != n)
				sysfatal("convM2S: %r");

			rpc = &stats->rpc[r->f.type];
			rpc->count++;
			rpc->bin += n;

			lock(&rqlock);
			stats->nrpc++;
			stats->nproto += n;
			r->next = rqhead;
			rqhead = r;
			unlock(&rqlock);

			r->t = nsec();

			if(write(efd[1], buf, n) != n)
				break;
		}
	}

	/* shutdown */
	done = 1;
	postnote(PNPROC, rspid, DONESTR);
	close(pfd[1]);
	close(efd[1]);

	/* dump statistics */
	rpc = &stats->rpc[Tread];
	brpsec = (double)stats->totread / (((float)rpc->time/1e9)+.000001);

	rpc = &stats->rpc[Twrite];
	bwpsec = (double)stats->totwrite / (((float)rpc->time/1e9)+.000001);

	ttime = 0;
	for(n = 0; n < nelem(stats->rpc); n++) {
		rpc = &stats->rpc[n];
		if(rpc->count == 0)
			continue;
		ttime += rpc->time;
	}

	bppsec = (double)stats->nproto / ((ttime/1e9)+.000001);

	fprint(2, "\nread      %llud bytes, %g Kb/sec\n", stats->totread, brpsec/1024.0);
	fprint(2, "write     %llud bytes, %g Kb/sec\n", stats->totwrite, bwpsec/1024.0);
	fprint(2, "protocol  %llud bytes, %g Kb/sec\n", stats->nproto, bppsec/1024.0);
	fprint(2, "rpc       %lud count\n\n", stats->nrpc);

	fprint(2, "%-10s %5s %5s %5s %5s %5s          T       R\n", 
	      "Message", "Count", "Low", "High", "Time", "Averg");

	for(n = 0; n < nelem(stats->rpc); n++) {
		rpc = &stats->rpc[n];
		if(rpc->count == 0)
			continue;
		fprint(2, "%-10s %5lud %5llud %5llud %5llud %5llud ms %8llud %8llud bytes\n", 
			rpc->name, 
			rpc->count,
			rpc->lo/1000000,
			rpc->hi/1000000,
			rpc->time/1000000,
			rpc->time/1000000/rpc->count,
			rpc->bin,
			rpc->bout);
	}

	fprint(2, "\nOpens    Reads  (bytes)   Writes  (bytes) File\n");
	for(fs = stats->file; fs < &stats->file[Maxfile]; fs++){
		if(fs->nopen == 0)
			break;

		if(strcmp(fs->path, fds[0]) == 0)
			fs->path = "stdin";
		else if(strcmp(fs->path, fds[1]) == 0)
			fs->path = "stdout";
		else if(strcmp(fs->path, fds[2]) == 0)
			fs->path = "stderr";

		fprint(2, "%5lud %8lud %8llud %8lud %8llud %s\n",
			fs->nopen,
			fs->nread, fs->bread,
			fs->nwrite, fs->bwrite,
			fs->path);
	}

	exits(nil);
}
