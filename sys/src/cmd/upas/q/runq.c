#include "common.h"
#include <ctype.h>

typedef struct Job Job;
typedef struct Wdir Wdir;
typedef struct Wpid Wpid;

struct Wdir {
	Dir	*d;
	int	nd;
	char	*name;
};

struct Job {
	Job	*next;
	int	pid;
	int	ac;
	int	dfd;
	char	**av;
	char	*buf;	/* backing for av */
	Wdir	*wdir;	/* work dir */
	Dir	*dp;	/* not owned */
	Mlock	*l;
	Biobuf	*b;
};

void	doalldirs(void);
void	dodir(char*);
Job*	dofile(Wdir*, Dir*);
Job*	donefile(Job*, Waitmsg*);
void	freejob(Job*);
void	rundir(char*);
char*	file(char*, char);
void	warning(char*, void*);
void	error(char*, void*);
int	returnmail(char**, Wdir*, char*, char*);
void	logit(char*, Wdir*, char*, char**);
void	doload(int);

char	*cmd;
char	*root;
int	debug;
int	giveup = 2*24*60*60;
int	limit;
Wpid	*waithd;
Wpid	*waittl;

char *runqlog = "runq";

char	**badsys;		/* array of recalcitrant systems */
int	nbad;
int	njob = 1;		/* number of concurrent jobs to invoke */
int	Eflag;			/* ignore E.xxxxxx dates */
int	Rflag;			/* no giving up, ever */
int	aflag;			/* do all dirs */

void
usage(void)
{
	fprint(2, "usage: runq [-dE] [-q dir] [-l load] [-t time] [-r nfiles] [-n nprocs] q-root cmd\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *qdir;

	qdir = 0;

	ARGBEGIN{
	case 'E':
		Eflag++;
		break;
	case 'R':	/* no giving up -- just leave stuff in the queue */
		Rflag++;
		break;
	case 'd':
		debug++;
		break;
	case 'r':
		limit = atoi(EARGF(usage()));
		break;
	case 't':
		giveup = 60*60*atoi(EARGF(usage()));
		break;
	case 'q':
		qdir = EARGF(usage());
		break;
	case 'a':
		aflag++;
		break;
	case 'n':
		njob = atoi(EARGF(usage()));
		if(njob == 0)
			usage();
		break;
	default:
		usage();
		break;
	}ARGEND;

	if(argc != 2)
		usage();

	if(!aflag && qdir == nil){
		qdir = getuser();
		if(qdir == nil)
			error("unknown user", 0);
	}
	root = argv[0];
	cmd = argv[1];

	if(chdir(root) < 0)
		error("can't cd to %s", root);

	if(aflag)
		doalldirs();
	else
		dodir(qdir);
	exits(0);
}

int
emptydir(char *name)
{
	int fd;
	long n;
	char buf[2048];

	fd = open(name, OREAD);
	if(fd < 0)
		return 1;
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if(n <= 0) {
		if(debug)
			fprint(2, "removing directory %s\n", name);
		syslog(0, runqlog, "rmdir %s", name);
		remove(name);
		return 1;
	}
	return 0;
}

/*
 *  run all user directories, must be bootes (or root on unix) to do this
 */
void
doalldirs(void)
{
	Dir *db;
	int fd;
	long i, n;


	if((fd = open(".", OREAD)) == -1){
		warning("opening %s", root);
		return;
	}
	if((n = dirreadall(fd, &db)) == -1){
		warning("reading %s: ", root);
		close(fd);
		return;
	}
	for(i=0; i<n; i++){
		if((db[i].qid.type & QTDIR) == 0)
			continue;
		if(emptydir(db[i].name))
			continue;
		dodir(db[i].name);
	}
	close(fd);
	free(db);
}

/*
 *  cd to a user directory and run it
 */
void
dodir(char *name)
{
	if(chdir(name) < 0){
		warning("cd to %s", name);
		return;
	}
	if(debug)
		fprint(2, "running %s\n", name);
	rundir(name);
	chdir("..");
}

/*
 *  run the current directory
 */
void
rundir(char *name)
{
	int nlive, fidx, fd, found;
	Job *hd, *j, **p;
	Waitmsg *w;
	Mlock *l;
	Wdir wd;

	fd = open(".", OREAD);
	if(fd == -1){
		warning("reading %s", name);
		return;
	}
	if((l = syslock("./rundir")) == nil){
		warning("locking %s", name);
		close(fd);
		return;
	}
	fidx= 0;
	hd = nil;
	nlive = 0;
	wd.name = name;
	wd.nd = dirreadall(fd, &wd.d);
	while(nlive > 0 ||  fidx< wd.nd){
		for(; fidx< wd.nd && nlive < njob; fidx++){
			if(strncmp(wd.d[fidx].name, "C.", 2) != 0)
				continue;
			if((j = dofile(&wd, &wd.d[fidx])) == nil){
				if(debug) fprint(2, "skipping %s: %r\n", wd.d[fidx].name);
				continue;
			}
			nlive++;
			j->next = hd;
			hd = j;
		}
		/* nothing to do */
		if(nlive == 0)
			break;
rescan:
		if((w = wait()) == nil){
			syslog(0, "runq", "wait error: %r");
			break;
		}
		found = 0;
		for(p = &hd; *p != nil; p = &(*p)->next){
			if(w->pid == (*p)->pid){
				*p = donefile(*p, w);
				found++;
				nlive--;
				break;
			}
		}
		free(w);
		if(!found){
			syslog(0, runqlog, "wait: pid not in job list");
			goto rescan;
		}
	}
	assert(hd == nil);
	free(wd.d);
	close(fd);
	sysunlock(l);
}

/*
 *  free files matching name in the current directory
 */
void
remmatch(Wdir *w, char *name)
{
	long i;

	syslog(0, runqlog, "removing %s/%s", w->name, name);
	for(i=0; i<w->nd; i++){
		if(strcmp(&w->d[i].name[1], &name[1]) == 0)
			remove(w->d[i].name);
	}

	/* error file (may have) appeared after we read the directory */
	/* stomp on data file in case of phase error */
	remove(file(name, 'D'));
	remove(file(name, 'E'));
}

/*
 *  like trylock, but we've already got the lock on fd,
 *  and don't want an L. lock file.
 */
static Mlock *
keeplockalive(char *path, int fd)
{
	char buf[1];
	Mlock *l;

	l = malloc(sizeof(Mlock));
	if(l == 0)
		return 0;
	l->fd = fd;
	snprint(l->name, sizeof l->name, "%s", path);

	/* fork process to keep lock alive until sysunlock(l) */
	switch(l->pid = rfork(RFPROC|RFNOWAIT)){
	default:
		break;
	case 0:
		fd = l->fd;
		for(;;){
			sleep(1000*60);
			if(pread(fd, buf, 1, 0) < 0)
				break;
		}
		_exits(0);
	}
	return l;
}

/*
 *  Launch trying a message, returning a job
 *  tracks the running pid.
 */
Job*
dofile(Wdir *w, Dir *dp)
{
	int dtime, efd, i, etime;
	Job *j;
	Dir *d;
	char *cp;

	if(debug) fprint(2, "dofile %s\n", dp->name);
	/*
	 *  if no data file or empty control or data file, just clean up
	 *  the empty control file must be 15 minutes old, to minimize the
	 *  chance of a race.
	 */
	d = dirstat(file(dp->name, 'D'));
	if(d == nil){
		syslog(0, runqlog, "no data file for %s", dp->name);
		remmatch(w, dp->name);
		return nil;
	}
	if(dp->length == 0){
		if(time(0)-dp->mtime > 15*60){
			syslog(0, runqlog, "empty ctl file for %s", dp->name);
			remmatch(w, dp->name);
		}
		return nil;
	}
	dtime = d->mtime;
	free(d);

	/*
	 *  retry times depend on the age of the errors file
	 */
	if(!Eflag && (d = dirstat(file(dp->name, 'E'))) != nil){
		etime = d->mtime;
		free(d);
		if(etime - dtime < 60*60){
			/* up to the first hour, try every 15 minutes */
			if(time(0) - etime < 15*60){
				werrstr("early retry");
				return nil;
			}
		} else {
			/* after the first hour, try once an hour */
			if(time(0) - etime < 60*60){
				werrstr("early retry");
				return nil;
			}
		}
	}

	/*
	 *  open control and data
	 */
	j = malloc(sizeof(Job));
	if(j == nil)
		return nil;
	memset(j, 0, sizeof(Job));
	j->dp = dp;
	j->dfd = -1;
	j->b = sysopen(file(dp->name, 'C'), "rl", 0660);
	if(j->b == 0)
		goto done;
	j->dfd = open(file(dp->name, 'D'), OREAD);
	if(j->dfd < 0)
		goto done;

	/*
	 *  make arg list
	 *	- read args into (malloc'd) buffer
	 *	- malloc a vector and copy pointers to args into it
	 */
	j->wdir = w;
	j->buf = malloc(dp->length+1);
	if(j->buf == nil){
		warning("buffer allocation", 0);
		freejob(j);
		return nil;
	}
	if(Bread(j->b, j->buf, dp->length) != dp->length){
		warning("reading control file %s\n", dp->name);
		freejob(j);
		return nil;
	}
	j->buf[dp->length] = 0;
	j->av = malloc(2*sizeof(char*));
	if(j->av == 0){
		warning("argv allocation", 0);
		freejob(j);
		return nil;
	}
	for(j->ac = 1, cp = j->buf; *cp; j->ac++){
		while(isspace(*cp))
			*cp++ = 0;
		if(*cp == 0)
			break;

		j->av = realloc(j->av, (j->ac+2)*sizeof(char*));
		if(j->av == 0){
			warning("argv allocation", 0);
		}
		j->av[j->ac] = cp;
		while(*cp && !isspace(*cp)){
			if(*cp++ == '"'){
				while(*cp && *cp != '"')
					cp++;
				if(*cp)
					cp++;
			}
		}
	}
	j->av[0] = cmd;
	j->av[j->ac] = 0;

	if(!Eflag &&time(0) - dtime > giveup){
		if(returnmail(j->av, w, dp->name, "Giveup") != 0)
			logit("returnmail failed", w, dp->name, j->av);
		remmatch(w, dp->name);
		goto done;
	}

	for(i = 0; i < nbad; i++){
		if(j->ac > 3 && strcmp(j->av[3], badsys[i]) == 0){
			werrstr("badsys: %s", j->av[3]);
			goto done;
		}
	}
	/*
	 * Ken's fs, for example, gives us 5 minutes of inactivity before
	 * the lock goes stale, so we have to keep reading it.
 	 */
	j->l = keeplockalive(file(dp->name, 'C'), Bfildes(j->b));
	if(j->l == nil){
		warning("lock file", 0);
		goto done;
	}

	/*
	 *  transfer
	 */
	j->pid = fork();
	switch(j->pid){
	case -1:
		sysunlock(j->l);
		sysunlockfile(Bfildes(j->b));
		syslog(0, runqlog, "out of procs");
		exits(0);
	case 0:
		if(debug) {
			fprint(2, "Starting %s\n", cmd);
			for(i = 0; j->av[i]; i++)
				fprint(2, " %s", j->av[i]);
			fprint(2, "\n");
		}
		logit("execing", w, dp->name, j->av);
		close(0);
		dup(j->dfd, 0);
		close(j->dfd);
		close(2);
		efd = open(file(dp->name, 'E'), OWRITE);
		if(efd < 0){
			if(debug)
				syslog(0, "runq", "open %s as %s: %r", file(dp->name,'E'), getuser());
			efd = create(file(dp->name, 'E'), OWRITE, 0666);
			if(efd < 0){
				if(debug) syslog(0, "runq", "create %s as %s: %r", file(dp->name, 'E'), getuser());
				exits("could not open error file - Retry");
			}
		}
		seek(efd, 0, 2);
		exec(cmd, j->av);
		error("can't exec %s", cmd);
		break;
	default:
		return j;
	}
done:
	freejob(j);
	return nil;
}

/*
 * Handle the completion of a job.
 * Wait for the pid, check its status,
 * and then pop the job off the list.
 * Return the next running job.
 */
Job*
donefile(Job *j, Waitmsg *wm)
{
	Job *n;

	if(debug)
		fprint(2, "wm->pid %d wm->msg == %s\n", wm->pid, wm->msg);
	if(wm->msg[0]){
		if(debug)
			fprint(2, "[%d] wm->msg == %s\n", getpid(), wm->msg);
		if(!Rflag && strstr(wm->msg, "Retry")==0){
			/* return the message and remove it */
			if(returnmail(j->av, j->wdir, j->dp->name, wm->msg) != 0)
				logit("returnmail failed", j->wdir, j->dp->name, j->av);
			remmatch(j->wdir, j->dp->name);
		} else {
			/* add sys to bad list and try again later */
			nbad++;
			badsys = realloc(badsys, nbad*sizeof(char*));
			badsys[nbad-1] = strdup(j->av[3]);
		}
	} else {
		/* it worked remove the message */
		remmatch(j->wdir, j->dp->name);
	}
	n = j->next;
	freejob(j);
	return n;
}

/*
 * Release resources associated with
 * a job.
 */
void
freejob(Job *j)
{
	if(j->b != nil){
		sysunlockfile(Bfildes(j->b));
		Bterm(j->b);
	}
	if(j->dfd != -1)
		close(j->dfd);
	if(j->l != nil)
		sysunlock(j->l);
	free(j->buf);
	free(j->av);
	free(j);
}


/*
 *  return a name starting with the given character
 */
char*
file(char *name, char type)
{
	static char nname[Elemlen+1];

	strncpy(nname, name, Elemlen);
	nname[Elemlen] = 0;
	nname[0] = type;
	return nname;
}

/*
 *  send back the mail with an error message
 *
 *  return 0 if successful
 */
int
returnmail(char **av, Wdir *w, char *name, char *msg)
{
	char buf[256], attachment[Pathlen], *sender;
	int fd, pfd[2];
	long n;
	String *s;

	if(av[1] == 0 || av[2] == 0){
		logit("runq - dumping bad file", w, name, av);
		return 0;
	}

	s = unescapespecial(s_copy(av[2]));
	sender = s_to_c(s);

	if(!returnable(sender) || strcmp(sender, "postmaster") == 0) {
		logit("runq - dumping p to p mail", w, name, av);
		return 0;
	}

	if(pipe(pfd) < 0){
		logit("runq - pipe failed", w, name, av);
		return -1;
	}

	switch(rfork(RFFDG|RFPROC|RFENVG|RFNOWAIT)){
	case -1:
		logit("runq - fork failed", w, name, av);
		return -1;
	case 0:
		logit("returning", w, name, av);
		close(pfd[1]);
		close(0);
		dup(pfd[0], 0);
		close(pfd[0]);
		putenv("upasname", "/dev/null");
		snprint(buf, sizeof(buf), "%s/marshal", UPASBIN);
		snprint(attachment, sizeof(attachment), "%s", file(name, 'D'));
		execl(buf, "send", "-A", attachment, "-s", "permanent failure", sender, nil);
		error("can't exec", 0);
		break;
	default:
		break;
	}

	close(pfd[0]);
	fprint(pfd[1], "\n");	/* get out of headers */
	if(av[1]){
		fprint(pfd[1], "Your request ``%.20s ", av[1]);
		for(n = 3; av[n]; n++)
			fprint(pfd[1], "%s ", av[n]);
	}
	fprint(pfd[1], "'' failed (code %s).\nThe symptom was:\n\n", msg);
	fd = open(file(name, 'E'), OREAD);
	if(fd >= 0){
		for(;;){
			n = read(fd, buf, sizeof(buf));
			if(n <= 0)
				break;
			if(write(pfd[1], buf, n) != n){
				close(fd);
				return -1;
			}
		}
		close(fd);
	}
	close(pfd[1]);
	return 0;
}

/*
 *  print a warning and continue
 */
void
warning(char *f, void *a)
{
	char err[ERRMAX];
	char buf[256];

	rerrstr(err, sizeof(err));
	snprint(buf, sizeof(buf), f, a);
	fprint(2, "runq: %s: %s\n", buf, err);
}

/*
 *  print an error and die
 */
void
error(char *f, void *a)
{
	char err[ERRMAX];
	char buf[256];

	rerrstr(err, sizeof(err));
	snprint(buf, sizeof(buf), f, a);
	fprint(2, "runq: %s: %s\n", buf, err);
	exits(buf);
}

void
logit(char *msg, Wdir *w, char *file, char **av)
{
	int n, m;
	char buf[256];

	n = snprint(buf, sizeof(buf), "%s/%s: %s", w->name, file, msg);
	for(; *av; av++){
		m = strlen(*av);
		if(n + m + 4 > sizeof(buf))
			break;
		sprint(buf + n, " '%s'", *av);
		n += m + 3;
	}
	syslog(0, runqlog, "%s", buf);
}
