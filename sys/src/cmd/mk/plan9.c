#include	"mk.h"

char 	shell[] =	"/bin/rc";
char 	shellname[] =	"rc";
char	envdir[] =	"/env/";

static	Word	*encodenulls(char*, int);

void
readenv(void)
{
	char *p;
	int envf, f;
	Dir *e;
	Bufblock *path;
	int i, n, len, npath;
	Word *w;

	rfork(RFENVG);	/*  use copy of the current environment variables */

	envf = open(envdir, OREAD);
	if(envf < 0)
		return;

	path = newbuf();
	bufcpy(path, envdir);
	npath = path->current - path->start;

	while((n = dirread(envf, &e)) > 0){
		for(i = 0; i < n; i++){
				/* don't import funny names, NULL values,
				 * or internal mk variables
				 */
			len = e[i].length;
			if(len <= 0 || *shname(e[i].name) != '\0')
				continue;
			if (symlook(e[i].name, S_INTERNAL, 0))
				continue;

			path->current = path->start + npath;
			bufcpy(path, e[i].name);
			insert(path, 0);
			f = open(path->start, OREAD);
			if(f < 0)
				continue;
			p = Malloc(len+1);
			if(read(f, p, len) != len){
				perror(path->start);
				close(f);
				continue;
			}
			p[len] = '\0';
			close(f);
			w = encodenulls(p, len);
			free(p);
			setvar(e[i].name, w);
		}
		free(e);
	}
	freebuf(path);
	close(envf);
}

static Word *
encodenulls(char *s, int n)
{
	Word *head, **link;
	int m;

	head = 0;
	link = &head;
	while(n > 0){
		m = strlen(s)+1;
		n -= m;
		*link = newword(s);
		s += m;
		link = &(*link)->next;
	}
	return head;
}

void
exportenv(Symtab **e)
{
	int f, n, npath;
	Bufblock *path;
	Word *w;

	path = newbuf();
	bufcpy(path, envdir);
	npath = path->current - path->start;

	for(;*e; e++){
		w = (*e)->u.ptr;
		path->current = path->start + npath;
		bufcpy(path, (*e)->name);
		insert(path, 0);
		if(w == 0){
			remove(path->start);
			continue;
		}
		f = create(path->start, OWRITE, 0666L);
		if(f < 0) {
			fprint(2, "can't create %s\n", path->start);
			perror(path->start);
			continue;
		}
		if(w->next == 0){
			n = strlen(w->s);
			if(n == 0) n = 1;
			if(write(f, w->s, n) != n)
				perror(path->start);
		} else {
			Bufblock *buf = newbuf();
			do {
				bufcpy(buf, w->s);
				insert(buf, 0);
				w = w->next;
			} while(w);
			n = buf->current - buf->start;
			if(write(f, buf->start, n) != n)
				perror(path->start);
			freebuf(buf);
		}
		close(f);
	}
	freebuf(path);
}

int
waitfor(char *msg)
{
	Waitmsg *w;
	int pid;

	if((w=wait()) == nil)
		return -1;
	strecpy(msg, msg+ERRMAX, w->msg);
	pid = w->pid;
	free(w);
	return pid;
}

void
expunge(int pid, char *msg)
{
	postnote(PNPROC, pid, msg);
}

int
execsh(char *cmd, char *args, Symtab **env, Bufblock *buf)
{
	int fd, tot, n, pid;

	pid = pipecmd(cmd, args, env, buf? &fd: 0);
	if(buf){
		tot = 0;
		for(;;){
			if(buf->current >= buf->end)
				growbuf(buf);
			n = read(fd, buf->current, buf->end-buf->current);
			if(n <= 0)
				break;
			buf->current += n;
			tot += n;
		}
		if(tot && buf->current[-1] == '\n')
			buf->current--;
		close(fd);
	}
	return pid;
}

int
pipecmd(char *cmd, char *args, Symtab **env, int *fd)
{
	int pid, pfd[2];

	if(DEBUG(D_EXEC))
		fprint(1, "pipecmd='%s'\n", cmd);/**/

	if(fd && pipe(pfd) < 0){
		perror("pipe");
		Exit();
	}
	pid = rfork(RFPROC|RFFDG|RFENVG);
	if(pid < 0){
		perror("mk fork");
		Exit();
	}
	if(pid == 0){
		if(fd){
			close(pfd[0]);
			dup(pfd[1], 1);
			close(pfd[1]);
		}
		if(env)
			exportenv(env);
		if(args)
			execl(shell, shellname, args, "-Ic", cmd, nil);
		else
			execl(shell, shellname, "-Ic", cmd, nil);
		perror(shell);
		_exits("exec");
	}
	if(fd){
		char name[32];

		close(pfd[1]);
		snprint(name, sizeof(name), "/fd/%d", pfd[0]);
		*fd = open(name, OREAD|OCEXEC);
		close(pfd[0]);
	}
	return pid;
}

void
Exit(void)
{
	while(waitpid() >= 0)
		;
	exits("error");
}

int
notifyf(void *a, char *msg)
{
	static int nnote;

	USED(a);
	if(++nnote > 100){	/* until andrew fixes his program */
		fprint(2, "mk: too many notes\n");
		notify(0);
		abort();
	}
	if(strcmp(msg, "interrupt")!=0 && strcmp(msg, "hangup")!=0)
		return 0;
	killchildren(msg);
	return -1;
}

void
catchnotes()
{
	atnotify(notifyf, 1);
}

int
chgtime(char *name)
{
	Dir sbuf;

	if(access(name, AEXIST) >= 0) {
		nulldir(&sbuf);
		sbuf.mtime = time((long *)0);
		return dirwstat(name, &sbuf);
	}
	return close(create(name, OWRITE, 0666));
}

void
rcopy(char **to, Resub *match, int n)
{
	int c;
	char *p;

	*to = match->sp;		/* stem0 matches complete target */
	for(to++, match++; --n > 0; to++, match++){
		if(match->sp && match->ep){
			p = match->ep;
			c = *p;
			*p = 0;
			*to = Strdup(match->sp);
			*p = c;
		}
		else
			*to = 0;
	}
}

void
dirtime(char *dir)
{
	int i, fd, n, npath;
	Bufblock *path;
	ulong t;
	Dir *d;

	if(symlook(dir, S_BULKED, 0))
		return;
	symlook(dir, S_BULKED, 1);

	path = newbuf();
	bufcpy(path, dir);
	if(strcmp(dir, ".") == 0)
		npath = 0;
	else {
		insert(path, '/');
		npath = path->current - path->start;
	}
	insert(path, 0);
	fd = open(path->start, OREAD);
	if(fd >= 0){
		while((n = dirread(fd, &d)) > 0){
			for(i=0; i<n; i++){
				t = d[i].mtime;
				/* defensive driving: this does happen */
				if(t == 0) t = 1;
				path->current = path->start + npath;
				bufcpy(path, d[i].name);
				insert(path, 0);
				symlook(path->start, S_TIME, 1)->u.value = t;
			}
			free(d);
		}
		close(fd);
	}
	freebuf(path);
}

ulong
mkmtime(char *name, int force)
{
	char *a, *s;
	ulong t;

	t = 0;
	/* cleanname() needs at least 2 characters */
	a = Malloc(strlen(name)+2+1);
	strcpy(a, name);
	cleanname(a);
	s = utfrrune(a, '/');
	if(s){
		*s = 0;
		dirtime(a);
		*s = '/';
	}else{
		dirtime(".");
	}
	if(!force){
		Symtab *sym = symlook(a, S_TIME, 0);
		if(sym)
			t = sym->u.value;
	} else {
		Dir *d = dirstat(a);
		if(d){
			t = d->mtime;
			free(d);
		}
	}
	free(a);
	return t;
}
