#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <plumb.h>
#include <ctype.h>
#include "plist.h"

#define MAX(a,b) ((a)>=(b)?(a):(b))
#define MIN(a,b) ((a)<=(b)?(a):(b))
#define CLAMP(x,min,max) MAX(min, MIN(max, x))

typedef struct Color Color;
typedef struct Player Player;
typedef struct Playlist Playlist;

enum
{
	Cstart = 1,
	Cstop,
	Ctoggle,
	Cseekrel,

	Everror = 1,
	Evready,

	Seek = 10, /* 10 seconds */
	Seekfast = 60, /* a minute */

	Bps = 44100*2*2, /* 44100KHz, stereo, s16 for a sample */
	Relbufsz = Bps/2, /* 0.5s */

	Dback = 0,
	Dfhigh,
	Dfmed,
	Dflow,
	Dfinv,
	Dbmed,
	Dblow,
	Dbinv,
	Numcolors,
};

struct Color {
	u32int rgb;
	Image *im;
};

struct Player
{
	Channel *ctl;
	Channel *ev;
	Channel *img;
	double seek;
	int pcur;
};

struct Playlist
{
	Meta *m;
	int n;
	char *raw;
	int rawsz;
};

int mainstacksize = 32768;

static int debug;
static int audio = -1;
static int volume;
static int pnotifies;
static Playlist *pl;
static Player *playernext;
static Player *playercurr;
static vlong byteswritten;
static int pcur, pcurplaying;
static int scroll, scrollsz;
static Font *f;
static Image *cover;
static Channel *playc;
static Mousectl *mctl;
static Keyboardctl *kctl;
static int colwidth[10];
static int mincolwidth[10];
static char *cols = "AatD";
static int colspath;
static int *shuffle;
static Rectangle seekbar;
static int seekmx, newseekmx = -1;
static double seekoff; /* ms */
static Lock audiolock;
static int audioerr = 0;
static Biobuf out;
static char *covers[] =
{
	"art", "folder", "cover", "Cover", "scans/CD", "Scans/Front", "Covers/Front"
};

static Color colors[Numcolors] =
{
	[Dback]  = {0xf0f0f0},
	[Dfhigh] = {0xffffff},
	[Dfmed]  = {0x343434},
	[Dflow]  = {0xa5a5a5},
	[Dfinv]  = {0x323232},
	[Dbmed]  = {0x72dec2},
	[Dblow]  = {0x404040},
	[Dbinv]  = {0xffb545},
};

static int Scrollwidth;
static int Scrollheight;
static int Coversz;

static void
audioon(void)
{
	lock(&audiolock);
	if(audio < 0 && (audio = open("/dev/audio", OWRITE|OCEXEC)) < 0 && audioerr == 0){
		fprint(2, "%r\n");
		audioerr = 1;
	}
	unlock(&audiolock);
}

static void
audiooff(void)
{
	lock(&audiolock);
	close(audio);
	audio = -1;
	audioerr = 0;
	unlock(&audiolock);
}

#pragma varargck type "P" uvlong
static int
positionfmt(Fmt *f)
{
	char *s, tmp[16];
	u64int sec;

	s = tmp;
	sec = va_arg(f->args, uvlong);
	if(sec >= 3600){
		s = seprint(s, tmp+sizeof(tmp), "%02lld:", sec/3600);
		sec %= 3600;
	}
	s = seprint(s, tmp+sizeof(tmp), "%02lld:", sec/60);
	sec %= 60;
	seprint(s, tmp+sizeof(tmp), "%02lld", sec);

	return fmtstrcpy(f, tmp);
}

static char *
getcol(Meta *m, int c)
{
	static char tmp[32];

	switch(c){
	case Palbum: return m->album;
	case Partist: return m->artist[0];
	case Pdate: return m->date;
	case Ptitle: return (!colspath && *m->title == 0) ? m->basename : m->title;
	case Ptrack: snprint(tmp, sizeof(tmp), "%4s", m->track); return m->track ? tmp : nil;
	case Ppath: return m->path;
	case Pduration:
		tmp[0] = 0;
		if(m->duration > 0)
			snprint(tmp, sizeof(tmp), "%8P", m->duration/1000);
		return tmp;
	default: sysfatal("invalid column '%c'", c);
	}

	return nil;
}

static void
adjustcolumns(void)
{
	int i, n, x, total, width;

	if(mincolwidth[0] == 0){
		for(i = 0; cols[i] != 0; i++)
			mincolwidth[i] = 1;
		for(n = 0; n < pl->n; n++){
			for(i = 0; cols[i] != 0; i++){
				if((x = stringwidth(f, getcol(pl->m+n, cols[i]))) > mincolwidth[i])
					mincolwidth[i] = x;
			}
		}
	}

	total = 0;
	n = 0;
	width = Dx(screen->r);
	for(i = 0; cols[i] != 0; i++){
		if(cols[i] == Pduration || cols[i] == Pdate || cols[i] == Ptrack)
			width -= mincolwidth[i] + 8;
		else{
			total += mincolwidth[i];
			n++;
		}
	}
	colspath = 0;
	for(i = 0; cols[i] != 0; i++){
		if(cols[i] == Ppath || cols[i] == Pbasename)
			colspath = 1;
		if(cols[i] == Pduration || cols[i] == Pdate || cols[i] == Ptrack)
			colwidth[i] = mincolwidth[i];
		else
			colwidth[i] = (width - Scrollwidth - n*8) * mincolwidth[i] / total;
	}
}

static Meta *
getmeta(int i)
{
	return &pl->m[shuffle != nil ? shuffle[i] : i];
}

static void
updatescrollsz(void)
{
	scrollsz = Dy(screen->r)/f->height - 2;
}

static void
redraw(int full)
{
	Image *col;
	Point p, sp;
	Rectangle sel, r;
	int i, j, left, right, scrollcenter, w;
	uvlong dur, msec;
	char tmp[32];

	lockdisplay(display);
	updatescrollsz();
	scroll = CLAMP(scroll, 0, pl->n - scrollsz);
	left = screen->r.min.x;
	if(scrollsz < pl->n) /* adjust for scrollbar */
		left += Scrollwidth + 1;

	if(full){
		draw(screen, screen->r, colors[Dback].im, nil, ZP);

		adjustcolumns();
		if(scrollsz < pl->n){ /* scrollbar */
			p.x = sp.x = screen->r.min.x + Scrollwidth;
			p.y = screen->r.min.y;
			sp.y = screen->r.max.y;
			line(screen, p, sp, Endsquare, Endsquare, 0, colors[Dflow].im, ZP);

			r = screen->r;
			r.max.x = r.min.x + Scrollwidth - 1;
			r.min.x += 1;
			if(scroll < 1)
				scrollcenter = 0;
			else
				scrollcenter = (Dy(screen->r)-Scrollheight*5/4)*scroll / (pl->n - scrollsz);
			r.min.y += scrollcenter + Scrollheight/4;
			r.max.y = r.min.y + Scrollheight;
			draw(screen, r, colors[Dblow].im, nil, ZP);
		}

		p.x = sp.x = left;
		p.y = 0;
		sp.y = screen->r.max.y;
		for(i = 0; cols[i+1] != 0; i++){
			p.x += colwidth[i] + 4;
			sp.x = p.x;
			line(screen, p, sp, Endsquare, Endsquare, 0, colors[Dflow].im, ZP);
			p.x += 4;
		}

		sp.x = sp.y = 0;
		p.x = left + 2;
		p.y = screen->r.min.y + 2;

		for(i = scroll; i < pl->n; i++, p.y += f->height){
			if(i < 0)
				continue;
			if(p.y > screen->r.max.y)
				break;

			if(pcur == i){
				sel.min.x = left;
				sel.min.y = p.y;
				sel.max.x = screen->r.max.x;
				sel.max.y = p.y + f->height;
				draw(screen, sel, colors[Dbinv].im, nil, ZP);
				col = colors[Dfinv].im;
			}else{
				col = colors[Dfmed].im;
			}

			sel = screen->r;

			p.x = left + 2 + 3;
			for(j = 0; cols[j] != 0; j++){
				sel.max.x = p.x + colwidth[j];
				replclipr(screen, 0, sel);
				string(screen, p, col, sp, f, getcol(getmeta(i), cols[j]));
				p.x += colwidth[j] + 8;
			}
			replclipr(screen, 0, screen->r);

			if(pcurplaying == i){
				Point rightp, leftp;
				leftp.y = rightp.y = p.y - 1;
				leftp.x = left;
				rightp.x = screen->r.max.x;
				line(screen, leftp, rightp, 0, 0, 0, colors[Dflow].im, sp);
				leftp.y = rightp.y = p.y + f->height;
				line(screen, leftp, rightp, 0, 0, 0, colors[Dflow].im, sp);
			}
		}
	}

	msec = 0;
	dur = getmeta(pcurplaying)->duration;
	if(pcurplaying >= 0){
		msec = byteswritten*1000/Bps;
		if(dur > 0){
			snprint(tmp, sizeof(tmp), "%s%P/%P 100%%",
				shuffle != nil ? "∫ " : "",
				dur/1000, dur/1000);
			w = stringwidth(f, tmp);
			msec = MIN(msec, dur);
			snprint(tmp, sizeof(tmp), "%s%P/%P %d%%",
				shuffle != nil ? "∫ " : "",
				(uvlong)(newseekmx >= 0 ? seekoff : msec)/1000,
				dur/1000, volume);
		}else{
			snprint(tmp, sizeof(tmp), "%s%P %d%%",
				shuffle != nil ? "∫ " : "",
				msec/1000, 100);
			w = stringwidth(f, tmp);
			snprint(tmp, sizeof(tmp), "%s%P %d%%",
				shuffle != nil ? "∫ " : "",
				msec/1000, volume);
		}
	}else{
		snprint(tmp, sizeof(tmp), "%s%d%%", shuffle != nil ? "∫ " : "", 100);
		w = stringwidth(f, tmp);
		snprint(tmp, sizeof(tmp), "%s%d%%", shuffle != nil ? "∫ " : "", volume);
	}
	r = screen->r;
	right = r.max.x - w - 4;
	r.min.x = left;
	r.min.y = r.max.y - f->height - 4;
	if(pcurplaying < 0 || dur == 0)
		r.min.x = right;
	draw(screen, r, colors[Dblow].im, nil, ZP);
	p = addpt(Pt(r.max.x-stringwidth(f, tmp)-4, r.min.y), Pt(2, 2));
	r.max.x = right;
	string(screen, p, colors[Dfhigh].im, sp, f, tmp);
	sel = r;

	if(cover != nil && full){
		r.max.x = r.min.x;
		r.min.x = screen->r.max.x - cover->r.max.x - 8;
		draw(screen, r, colors[Dblow].im, nil, ZP);
		r = screen->r;
		r.min.x = r.max.x - cover->r.max.x - 8;
		r.min.y = r.max.y - cover->r.max.y - 8 - f->height - 4;
		r.max.y = r.min.y + cover->r.max.y + 8;
		draw(screen, r, colors[Dblow].im, nil, ZP);
		draw(screen, insetrect(r, 4), cover, nil, ZP);
	}

	/* seek bar */
	seekbar = ZR;
	if(pcurplaying >= 0 && dur > 0){
		r = insetrect(sel, 3);
		draw(screen, r, colors[Dback].im, nil, ZP);
		seekbar = r;
		r.max.x = r.min.x + Dx(r) * (double)msec / (double)dur;
		draw(screen, r, colors[Dbmed].im, nil, ZP);
	}

	flushimage(display, 1);
	unlockdisplay(display);
}

static void
coverload(void *player_)
{
	int p[2], pid, fd, i;
	char *prog, *path, *s, tmp[32];
	Meta *m;
	Channel *ch;
	Player *player;
	Image *newcover;

	threadsetname("cover");
	player = player_;
	m = getmeta(player->pcur);
	pid = -1;
	ch = player->img;
	fd = -1;
	prog = nil;

	if(m->imagefmt != nil && m->imagereader == 0){
		if(strcmp(m->imagefmt, "image/png") == 0)
			prog = "png";
		else if(strcmp(m->imagefmt, "image/jpeg") == 0)
			prog = "jpg";
	}

	if(prog == nil){
		path = strdup(m->path);
		if(path != nil && (s = utfrrune(path, '/')) != nil){
			*s = 0;

			for(i = 0; i < nelem(covers) && prog == nil; i++){
				if((s = smprint("%s/%s.jpg", path, covers[i])) != nil && (fd = open(s, OREAD)) >= 0)
					prog = "jpg";
				free(s);
				s = nil;
				if(fd < 0 && (s = smprint("%s/%s.png", path, covers[i])) != nil && (fd = open(s, OREAD)) >= 0)
					prog = "png";
				free(s);
			}
		}
		free(path);
	}

	if(prog == nil)
		goto done;

	if(fd < 0){
		fd = open(m->path, OREAD);
		seek(fd, m->imageoffset, 0);
	}
	pipe(p);
	if((pid = rfork(RFPROC|RFFDG|RFNOTEG|RFCENVG|RFNOWAIT)) == 0){
		dup(fd, 0); close(fd);
		dup(p[1], 1); close(p[1]);
		if(!debug){
			dup(fd = open("/dev/null", OWRITE), 2);
			close(fd);
		}
		snprint(tmp, sizeof(tmp), "%s -9t | resample -x%d", prog, Coversz);
		execl("/bin/rc", "rc", "-c", tmp, nil);
		sysfatal("execl: %r");
	}
	close(fd);
	close(p[1]);

	if(pid > 0){
		newcover = readimage(display, p[0], 1);
		sendp(ch, newcover);
	}
	close(p[0]);
done:
	if(pid < 0)
		sendp(ch, nil);
	chanclose(ch);
	chanfree(ch);
	if(pid >= 0)
		postnote(PNGROUP, pid, "interrupt");
	threadexits(nil);
}

static int
playerret(Player *player)
{
	return recvul(player->ev) == Everror ? -1 : 0;
}

static void
pnotify(Player *p)
{
	Meta *m;
	char *s;
	int i;

	if(!pnotifies)
		return;

	if(p != nil){
		m = getmeta(p->pcur);
		for(i = 0; cols[i] != 0; i++)
			Bprint(&out, "%s\t", (s = getcol(m, cols[i])) ? s : "");
	}
	Bprint(&out, "\n");
	Bflush(&out);
}

static void
stop(Player *player)
{
	if(player == nil)
		return;

	if(player == playernext)
		playernext = nil;
	if(!getmeta(player->pcur)->filefmt[0])
		playerret(player);
	if(player == playercurr)
		pnotify(nil);
	sendul(player->ctl, Cstop);
}

static void
start(Player *player)
{
	if(player == nil)
		return;
	if(!getmeta(player->pcur)->filefmt[0])
		playerret(player);
	pnotify(player);
	sendul(player->ctl, Cstart);
}

static void playerthread(void *player_);

static Player *
newplayer(int pcur, int loadnext)
{
	Player *player;

	if(playernext != nil && loadnext){
		if(pcur == playernext->pcur){
			player = playernext;
			playernext = nil;
			goto done;
		}
		stop(playernext);
		playernext = nil;
	}

	player = mallocz(sizeof(*player), 1);
	player->ctl = chancreate(sizeof(ulong), 0);
	player->ev = chancreate(sizeof(ulong), 0);
	player->pcur = pcur;

	threadcreate(playerthread, player, 4096);
	if(getmeta(pcur)->filefmt[0] && playerret(player) < 0)
		return nil;

done:
	if(pcur < pl->n-1 && playernext == nil && loadnext)
		playernext = newplayer(pcur+1, 0);

	return player;
}

static void
playerthread(void *player_)
{
	char *buf, cmd[64], seekpos[12], *fmt;
	Player *player;
	Ioproc *io;
	Image *thiscover;
	ulong c;
	int p[2], fd, pid, noinit, trycoverload;
	long n, r;
	vlong boffset, boffsetlast;
	Meta *cur;

	threadsetname("player");
	player = player_;
	noinit = 0;
	boffset = 0;
	buf = nil;
	trycoverload = 1;
	io = nil;
	pid = -1;

restart:
	cur = getmeta(player->pcur);
	fmt = cur->filefmt;
	fd = -1;
	if(*fmt){
		if((fd = open(cur->path, OREAD)) < 0){
			fprint(2, "%r\n");
			sendul(player->ev, Everror);
			chanclose(player->ev);
			goto freeplayer;
		}
	}else{
		sendul(player->ev, Evready);
		chanclose(player->ev);
	}

	pipe(p);
	if((pid = rfork(RFPROC|RFFDG|RFNOTEG|RFCENVG|RFNOWAIT)) == 0){
		close(p[1]);
		if(fd < 0)
			fd = open("/dev/null", OREAD);
		dup(fd, 0); close(fd);
		dup(p[0], 1); close(p[0]);
		if(!debug){
			dup(fd = open("/dev/null", OWRITE), 2);
			close(fd);
		}
		if(*fmt){
			snprint(cmd, sizeof(cmd), "/bin/audio/%sdec", fmt);
			snprint(seekpos, sizeof(seekpos), "%g", (double)boffset/Bps);
			execl(cmd, cmd, boffset ? "-s" : nil, seekpos, nil);
		}else{
			execl("/bin/play", "play", "-o", "/fd/1", cur->path, nil);
		}
		close(0);
		close(1);
		exits("%r");
	}
	if(pid < 0)
		sysfatal("rfork: %r");
	if(fd >= 0)
		close(fd);
	close(p[0]);

	c = 0;
	if(!noinit){
		if(*fmt){
			sendul(player->ev, Evready);
			chanclose(player->ev);
		}
		buf = malloc(Relbufsz);
		if((io = ioproc()) == nil)
			sysfatal("player: %r");
		if((n = ioreadn(io, p[1], buf, Relbufsz)) < 0)
			fprint(2, "player: %r\n");
		if(recv(player->ctl, &c) < 0 || c != Cstart)
			goto freeplayer;
		if(n < 1)
			goto next;
		audioon();
		boffset = iowrite(io, audio, buf, n);
		noinit = 1;
	}

	boffsetlast = boffset;
	byteswritten = boffset;
	pcurplaying = player->pcur;
	if(c != Cseekrel)
		redraw(1);

	while(1){
		n = ioread(io, p[1], buf, Relbufsz);
		if(n <= 0)
			break;

		thiscover = nil;
		if(player->img != nil && nbrecv(player->img, &thiscover) != 0){
			freeimage(cover);
			cover = thiscover;
			redraw(1);
			player->img = nil;
		}
		r = nbrecv(player->ctl, &c);
		if(r < 0){
			audiooff();
			goto stop;
		}else if(r != 0){
			if(c == Ctoggle){
				audiooff();
				if(recv(player->ctl, &c) < 0)
					goto stop;
			}
			if(c == Cseekrel){
				boffset = MAX(0, boffset + player->seek*Bps);
				n = 0;
				break;
			}else if(c == Cstop){
				audiooff();
				goto stop;
			}
		}

		boffset += n;
		byteswritten = boffset;
		audioon();
		iowrite(io, audio, buf, n);
		if(trycoverload){
			trycoverload = 0;
			player->img = chancreate(sizeof(Image*), 0);
			proccreate(coverload, player, 4096);
		}
		if(labs(boffset/Relbufsz - boffsetlast/Relbufsz) > 0){
			boffsetlast = boffset;
			redraw(0);
		}
	}

	if(n < 1){ /* seeking backwards or end of the song */
		close(p[1]);
		p[1] = -1;
		if(c != Cseekrel || (getmeta(pcurplaying)->duration && boffset >= getmeta(pcurplaying)->duration/1000*Bps)){
next:
			playercurr = nil;
			playercurr = newplayer((player->pcur+1) % pl->n, 1);
			start(playercurr);
			goto stop;
		}
		goto restart;
	}

stop:
	if(player->img != nil)
		freeimage(recvp(player->img));
freeplayer:
	chanfree(player->ctl);
	chanfree(player->ev);
	if(pid >= 0)
		postnote(PNGROUP, pid, "interrupt");
	closeioproc(io);
	if(p[1] >= 0)
		close(p[1]);
	if(player == playercurr)
		playercurr = nil;
	if(player == playernext)
		playernext = nil;
	free(buf);
	free(player);
	threadexits(nil);
}

static int
toggle(Player *player)
{
	return (player != nil && sendul(player->ctl, Ctoggle) == 1) ? 0 : -1;
}

static void
seekrel(Player *player, double off)
{
	if(player != nil && *getmeta(pcurplaying)->filefmt){
		player->seek = off;
		sendul(player->ctl, Cseekrel);
	}
}

static void
writeplist(void)
{
	int i;

	for(i = 0; i < pl->n; i++)
		printmeta(&out, pl->m+i);
}

static void
freeplist(Playlist *pl)
{
	if(pl != nil){
		free(pl->m);
		free(pl->raw);
	}
	free(pl);
}

static char *
readall(int f)
{
	int bufsz, sz, n;
	char *s;

	bufsz = 1023;
	s = nil;
	for(sz = 0;; sz += n){
		if(bufsz-sz < 1024){
			bufsz *= 2;
			s = realloc(s, bufsz);
		}
		if((n = readn(f, s+sz, bufsz-sz-1)) < 1)
			break;
	}
	if(n < 0 || sz < 1){
		free(s);
		return nil;
	}
	s[sz] = 0;

	return s;
}

static Playlist *
readplist(int fd)
{
	char *raw, *s, *e, *a[5], *b;
	Playlist *pl;
	int plsz;
	Meta *m;

	if((raw = readall(fd)) == nil)
		return nil;

	plsz = 0;
	for(s = raw; (s = strchr(s, '\n')) != nil; s++){
		if(*(++s) == '\n')
			plsz++;
	}

	if((pl = calloc(1, sizeof(*pl))) == nil || (pl->m = calloc(plsz+1, sizeof(Meta))) == nil){
		freeplist(pl);
		werrstr("no memory");
		return nil;
	}

	pl->raw = raw;
	for(s = pl->raw, m = pl->m;; s = e){
		if((e = strchr(s, '\n')) == nil)
			break;
		s += 2;
		*e++ = 0;
		switch(s[-2]){
		case 0:
			if(m->path != nil){
				pl->n++;
				m++;
			}
			break;
		case Pimage:
			if(tokenize(s, a, nelem(a)) >= 4){
				m->imageoffset = atoi(a[0]);
				m->imagesize = atoi(a[1]);
				m->imagereader = atoi(a[2]);
				m->imagefmt = a[3];
			}
			break;
		case Pduration:
			m->duration = strtoull(s, nil, 0);
			break;
		case Partist:
			if(m->numartist < Maxartist)
				m->artist[m->numartist++] = s;
			break;
		case Pfilefmt: m->filefmt = s; break;
		case Palbum:   m->album = s; break;
		case Pdate:    m->date = s; break;
		case Ptitle:   m->title = s; break;
		case Ptrack:   m->track = s; break;
		case Ppath:
			m->path = s;
			m->basename = (b = utfrrune(s, '/')) == nil ? s : b+1;
			break;
		}
	}
	if(m != nil && m->path != nil)
		pl->n++;

	return pl;
}

static void
recenter(void)
{
	updatescrollsz();
	scroll = pcur - scrollsz/2 + 1;
}

static void
search(char d)
{
	Meta *m;
	static char buf[64];
	static int sz;
	int inc, i, a, cycle;

	inc = (d == '/' || d == 'n') ? 1 : -1;
	if(d == '/' || d == '?')
		sz = enter(inc > 0 ? "forward:" : "backward:", buf, sizeof(buf), mctl, kctl, nil);
	if(sz < 1)
		return;

	cycle = 1;
	for(i = pcur+inc; i >= 0 && i < pl->n;){
		m = getmeta(i);
		for(a = 0; a < m->numartist; a++){
			if(cistrstr(m->artist[a], buf) != nil)
				break;
		}
		if(m->album != nil && cistrstr(m->album, buf) != nil)
			break;
		if(m->title != nil && cistrstr(m->title, buf) != nil)
			break;
		if(cistrstr(m->path, buf) != nil)
			break;
onemore:
		i += inc;
	}
	if(i >= 0 && i < pl->n){
		pcur = i;
		recenter();
		redraw(1);
	}else if(cycle && i+inc < 0){
		cycle = 0;
		i = pl->n;
		goto onemore;
	}else if(cycle && i+inc >= pl->n){
		cycle = 0;
		i = -1;
		goto onemore;
	}
}

static void
chvolume(int d)
{
	int f, l, r, ol, or;
	Biobuf b;
	char *s, *a[4];

	if((f = open("/dev/volume", ORDWR)) < 0)
		return;
	Binit(&b, f, OREAD);

	l = r = 0;
	for(; (s = Brdline(&b, '\n')) != nil;){
		if(strncmp(s, "master", 6) == 0 && tokenize(s, a, 3) == 3){
			l = ol = atoi(a[1]);
			r = or = atoi(a[2]);
			for(;;){
				l += d;
				r += d;
				fprint(f, "master %d %d\n", l, r);
				Bseek(&b, 0, 0);
				for(; (s = Brdline(&b, '\n')) != nil;){
					if(strncmp(s, "master", 6) == 0 && tokenize(s, a, 3) == 3){
						if(atoi(a[1]) == l && atoi(a[2]) == r)
							goto end;
						if(atoi(a[1]) != ol && atoi(a[2]) != or)
							goto end;
						if(l < 0 || r < 0 || l > 100 || r > 100)
							goto end;
						break;
					}
				}
			}
		}
	}

end:
	volume = (l+r)/2;
	if(volume > 100)
		volume = 100;
	else if(volume < 0)
		volume = 0;

	Bterm(&b);
	close(f);
}

static void
toggleshuffle(void)
{
	int i, m, xi, a, c, pcurnew, pcurplayingnew;

	if(shuffle == nil){
		if(pl->n < 2)
			return;

		m = pl->n;
		if(pl->n < 4){
			a = 1;
			c = 3;
			m = 7;
		}else{
			m += 1;
			m |= m >> 1;
			m |= m >> 2;
			m |= m >> 4;
			m |= m >> 8;
			m |= m >> 16;
			a = 1 + nrand(m/4)*4;     /* 1 ≤ a < m   && a mod 4 = 1 */
			c = 3 + nrand((m-2)/2)*2; /* 3 ≤ c < m-1 && c mod 2 = 1 */
		}

		shuffle = malloc(pl->n*sizeof(*shuffle));
		xi = pcurplaying < 0 ? pcur : pcurplaying;
		pcurplayingnew = -1;
		pcurnew = 0;
		for(i = 0; i < pl->n;){
			if(xi < pl->n){
				if(pcur == xi)
					pcurnew = i;
				if(pcurplaying == xi)
					pcurplayingnew = i;
				shuffle[i++] = xi;
			}
			xi = (a*xi + c) & m;
		}
		pcur = pcurnew;
		pcurplaying = pcurplayingnew;
	}else{
		pcur = shuffle[pcur];
		if(pcurplaying >= 0)
			pcurplaying = shuffle[pcurplaying];
		free(shuffle);
		shuffle = nil;
	}

	stop(playernext);
	if(pcur < pl->n-1)
		playernext = newplayer(pcur+1, 0);
}

static void
plumbaudio(void *kbd)
{
	int i, f, pf;
	Playlist *p;
	Plumbmsg *m;
	char *s, *e;
	Rune c;

	threadsetname("audio/plumb");
	if((f = plumbopen("audio", OREAD)) >= 0){
		while((m = plumbrecv(f)) != nil){
			s = m->data;
			if(strncmp(s, "key", 3) == 0 && isspace(s[3])){
				for(s = s+4; isspace(*s); s++);
				for(; (i = chartorune(&c, s)) > 0 && c != Runeerror; s += i)
					sendul(kbd, c);
				continue;
			} 
			if(*s != '/' && m->wdir != nil)
				s = smprint("%s/%.*s", m->wdir, m->ndata, m->data);

			if((e = strrchr(s, '.')) != nil && strcmp(e, ".plist") == 0 && (pf = open(s, OREAD)) >= 0){
				p = readplist(pf);
				close(pf);
				if(p == nil)
					continue;

				freeplist(pl);
				pl = p;
				memset(mincolwidth, 0, sizeof(mincolwidth)); /* readjust columns */
				sendul(playc, 0);
			}else{
				for(i = 0; i < pl->n; i++){
					if(strcmp(pl->m[i].path, s) == 0){
						sendul(playc, i);
						break;
					}
				}
			}

			if(s != m->data)
				free(s);
			plumbfree(m);
		}
	}

	threadexits(nil);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-s] [-c aAdDtTp]\n", argv0);
	sysfatal("usage");
}

void
threadmain(int argc, char **argv)
{
	Rune key;
	Mouse m;
	ulong ind;
	enum {
		Emouse,
		Eresize,
		Ekey,
		Eplay,
	};
	Alt a[] = {
		{ nil, &m, CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, &key, CHANRCV },
		{ nil, &ind, CHANRCV },
		{ nil, nil, CHANEND },
	};
	int n, scrolling, oldpcur, oldbuttons, pnew, shuffled;
	char buf[64];

	shuffled = 0;
	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 's':
		shuffled = 1;
		break;
	case 'c':
		cols = EARGF(usage());
		if(strlen(cols) >= nelem(colwidth))
			sysfatal("max %d columns allowed", nelem(colwidth));
		break;
	default:
		usage();
		break;
	}ARGEND;

	if((pl = readplist(0)) == nil){
		fprint(2, "playlist: %r\n");
		sysfatal("playlist error");
	}
	close(0);

	Binit(&out, 1, OWRITE);
	pnotifies = fd2path(1, buf, sizeof(buf)) == 0 && strcmp(buf, "/dev/cons") != 0;

	if(initdraw(nil, nil, "zuke") < 0)
		sysfatal("initdraw: %r");
	f = display->defaultfont;
	Scrollwidth = MAX(14, stringwidth(f, "#"));
	Scrollheight = MAX(16, f->height);
	Coversz = MAX(64, stringwidth(f, "∫ 00:00:00/00:00:00 100%"));
	unlockdisplay(display);
	display->locking = 1;
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	a[0].c = mctl->c;
	a[1].c = mctl->resizec;
	a[2].c = kctl->c;
	a[3].c = chancreate(sizeof(ind), 0);
	playc = a[3].c;

	for(n = 0; n < Numcolors; n++)
		colors[n].im = allocimage(display, Rect(0,0,1,1), RGB24, 1, colors[n].rgb<<8 | 0xff);

	srand(time(0));
	pcurplaying = -1;
	chvolume(0);
	fmtinstall('P', positionfmt);
	threadsetname("zuke");

	if(shuffled){
		pcur = nrand(pl->n);
		toggleshuffle();
		recenter();
	}

	redraw(1);
	m.buttons = 0;
	scrolling = 0;

	proccreate(plumbaudio, kctl->c, 4096);

	for(;;){
		oldpcur = pcur;
		if(seekmx != newseekmx){
			seekmx = newseekmx;
			redraw(0);
		}

		oldbuttons = m.buttons;
		switch(alt(a)){
		case Emouse:
			if(ptinrect(m.xy, seekbar)){
				seekoff = getmeta(pcurplaying)->duration * (double)(m.xy.x-1-seekbar.min.x) / (double)Dx(seekbar);
				if(seekoff < 0)
					seekoff = 0;
				newseekmx = m.xy.x;
			}else{
				newseekmx = -1;
			}

			if(m.buttons != 2)
				scrolling = 0;
			if(m.buttons == 0)
				break;
			if(m.buttons == 8){
				scroll = MAX(scroll-scrollsz/4-1, 0);
				redraw(1);
				break;
			}else if(m.buttons == 16){
				scroll = MIN(scroll+scrollsz/4+1, pl->n-scrollsz);
				redraw(1);
				break;
			}

			n = (m.xy.y - screen->r.min.y)/f->height;

			if(m.xy.x <= screen->r.min.x+Scrollwidth){
				if(m.buttons == 1){
					scroll = MAX(0, scroll-n-1);
					redraw(1);
					break;
				}else if(m.buttons == 4){
					scroll = MIN(scroll+n+1, pl->n-scrollsz);
					redraw(1);
					break;
				}else if(m.buttons == 2){
					scrolling = 1;
				}
			}

			if(!scrolling && ptinrect(m.xy, insetrect(seekbar, -4))){
				if(ptinrect(m.xy, seekbar))
					seekrel(playercurr, seekoff/1000.0 - byteswritten/Bps);
				break;
			}

			if(scrolling){
				if(scrollsz >= pl->n)
					break;
				scroll = (m.xy.y - screen->r.min.y - Scrollheight/4)*(pl->n-scrollsz) / (Dy(screen->r)-Scrollheight/2);
				scroll = CLAMP(scroll, 0, pl->n-scrollsz);
				redraw(1);
			}else if(m.buttons == 1 || m.buttons == 2){
				n += scroll;
				if(n < pl->n){
					pcur = n;
					if(m.buttons == 2 && oldbuttons == 0){
						stop(playercurr);
						playercurr = newplayer(pcur, 1);
						start(playercurr);
					}
				}
			}
			break;
		case Eresize: /* resize */
			if(getwindow(display, Refnone) < 0)
				sysfatal("getwindow: %r");
			redraw(1);
			break;
		case Ekey:
			switch(key){
			case Kleft:
				seekrel(playercurr, -(double)Seek);
				break;
			case Kright:
				seekrel(playercurr, Seek);
				break;
			case ',':
				seekrel(playercurr, -(double)Seekfast);
				break;
			case '.':
				seekrel(playercurr, Seekfast);
				break;
			case Kup:
				pcur--;
				break;
			case Kpgup:
				pcur -= scrollsz;
				break;
			case Kdown:
				pcur++;
				break;
			case Kpgdown:
				pcur += scrollsz;
				break;
			case Kend:
				pcur = pl->n-1;
				scroll = pl->n-scrollsz;
				break;
			case Khome:
				pcur = 0;
				break;
			case '\n':
playcur:
				stop(playercurr);
				playercurr = newplayer(pcur, 1);
				start(playercurr);
				break;
			case 'q':
			case Kdel:
				stop(playercurr);
				goto end;
			case 'i':
			case 'o':
				if(pcur == pcurplaying)
					oldpcur = -1;
				pcur = pcurplaying;
				recenter();
				break;
			case 'b':
			case '>':
				if(playercurr == nil)
					break;
				pnew = pcurplaying;
				if(++pnew >= pl->n)
					pnew = 0;
				stop(playercurr);
				playercurr = newplayer(pnew, 1);
				start(playercurr);
				redraw(1);
				break;
			case 'z':
			case '<':
				if(playercurr == nil)
					break;
				pnew = pcurplaying;
				if(--pnew < 0)
					pnew = pl->n-1;
				stop(playercurr);
				playercurr = newplayer(pnew, 1);
				start(playercurr);
				redraw(1);
				break;
			case '-':
				chvolume(-1);
				redraw(0);
				break;
			case '+':
			case '=':
				chvolume(+1);
				redraw(0);
				break;
			case 'v':
				stop(playercurr);
				playercurr = nil;
				pcurplaying = -1;
				freeimage(cover);
				cover = nil;
				redraw(1);
				break;
			case 's':
				toggleshuffle();
				recenter();
				redraw(1);
				break;
			case 'c':
			case 'p':
			case ' ':
				if(toggle(playercurr) != 0)
					goto playcur;
				break;
			case '/':
			case '?':
			case 'n':
			case 'N':
				search(key);
				break;
			}
			break;
		case Eplay:
			pcur = ind;
			recenter();
			if(playercurr != nil)
				goto playcur;
			break;
		}

		if(pcur != oldpcur){
			pcur = CLAMP(pcur, 0, pl->n-1);
			if(pcur < scroll)
				scroll = pcur;
			else if(pcur > scroll + scrollsz)
				scroll = pcur - scrollsz;
			scroll = CLAMP(scroll, 0, pl->n-scrollsz);

			if(pcur != oldpcur)
				redraw(1);
		}
	}

end:
	threadexitsall(nil);
}
