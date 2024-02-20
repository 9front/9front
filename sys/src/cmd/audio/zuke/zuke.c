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
#include "icy.h"

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

	Rgdisabled = 0,
	Rgtrack,
	Rgalbum,
	Numrg,

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

	Ncol = 10,
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
	Channel *icytitlec;
	char *icytitle;
	double seek;
	double gain;
	int pcur;
};

struct Playlist
{
	Meta *m;
	int n;
	char *raw;
	int rawsz;
};

static int debug;
static int audio = -1;
static int volume, rg;
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
static Channel *redrawc;
static Mousectl *mctl;
static Keyboardctl kctl;
static int shiftdown;
static int colwidth[Ncol];
static int mincolwidth[Ncol];
static char *cols = "AatD";
static int colspath;
static int *shuffle;
static int repeatone;
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
static int Seekthicc;
static int Coversz;

static char *
matchvname(char **s)
{
	char *names[] = {"mix", "master", "pcm out"};
	int i, l;

	for(i = 0; i < nelem(names); i++){
		l = strlen(names[i]);
		if(strncmp(*s, names[i], l) == 0){
			*s += l;
			return names[i];
		}
	}

	return nil;
}

static void
chvolume(int d)
{
	int f, x, ox, want, try;
	char *s, *e;
	Biobufhdr b;
	uchar buf[1024];
	char *n;

	if((f = open("/dev/volume", ORDWR|OCEXEC)) < 0)
		return;
	Binits(&b, f, OREAD, buf, sizeof(buf));
	want = x = -1;
	ox = 0;
	for(try = 0; try < 10; try++){
		for(n = nil; (s = Brdline(&b, '\n')) != nil;){
			if((n = matchvname(&s)) != nil && (ox = strtol(s, &e, 10)) >= 0 && s != e)
				break;
			n = nil;
		}

		if(want < 0){
			want = CLAMP(ox+d, 0, 100);
			x = ox;
		}
		if(n == nil || (d > 0 && ox >= want) || (d < 0 && ox <= want))
			break;
		x = CLAMP(x+d, 0, 100);
		if(fprint(f, "%s %d\n", n, x) < 0)
			break;
		/* go to eof and back */
		while(Brdline(&b, '\n') != nil);
		Bseek(&b, 0, 0);
	}
	volume = CLAMP(ox, 0, 100);
	Bterm(&b);
	close(f);
}

static void
audioon(void)
{
	lock(&audiolock);
	if(audio < 0){
		if((audio = open("/dev/audio", OWRITE|OCEXEC)) < 0 && audioerr == 0){
			fprint(2, "%r\n");
			audioerr = 1;
		}else{
			chvolume(0);
		}
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
	char *s;

	switch(c){
	case Palbum: s = m->album; break;
	case Partist: s = m->artist[0]; break;
	case Pcomposer: s = m->composer; break;
	case Pdate: s = m->date; break;
	case Ptitle: s = (!colspath && (m->title == nil || *m->title == 0)) ? m->basename : m->title; break;
	case Ptrack: snprint(tmp, sizeof(tmp), "%4s", m->track); s = m->track ? tmp : nil; break;
	case Ppath: s = m->path; break;
	case Pduration:
		tmp[0] = 0;
		if(m->duration > 0)
			snprint(tmp, sizeof(tmp), "%8P", m->duration/1000);
		s = tmp;
		break;
	default: sysfatal("invalid column '%c'", c);
	}

	return s ? s : "";
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
redraw_(int full)
{
	static Image *back, *ocover;
	static int oscrollcenter, opcur, opcurplaying;

	int x, i, j, scrollcenter, w;
	uvlong dur, msec;
	Rectangle sel, r;
	char tmp[32], *s;
	Point p, sp, p₀, p₁;
	Image *col;

	/* seekbar playback/duration text */
	i = snprint(tmp, sizeof(tmp), "%s%s%s%s",
		rg ? (rg == Rgalbum ? "ᴬ" : "ᵀ") : "",
		repeatone ? "¹" : "",
		shuffle != nil ? "∫" : "",
		(rg || repeatone || shuffle != nil) ? " " : ""
	);
	msec = 0;
	dur = 0;
	w = stringwidth(f, tmp);
	if(pcurplaying >= 0){
		msec = byteswritten*1000/Bps;
		dur = getmeta(pcurplaying)->duration;
		if(dur > 0){
			snprint(tmp+i, sizeof(tmp)-i, "%P/%P ", dur/1000, dur/1000);
			w += stringwidth(f, tmp+i);
			msec = MIN(msec, dur);
			i += snprint(tmp+i, sizeof(tmp)-i, "%P/%P ",
				(uvlong)(newseekmx >= 0 ? seekoff : msec)/1000,
				dur/1000
			);
		}else{
			j = snprint(tmp+i, sizeof(tmp)-i, "%P ", msec/1000);
			w += stringwidth(f, tmp+i);
			i += j;
		}
	}
	snprint(tmp+i, sizeof(tmp)-i, "%d%%", 100);
	w += stringwidth(f, tmp+i);
	snprint(tmp+i, sizeof(tmp)-i, "%d%%", volume);

	lockdisplay(display);

	if(back == nil || Dx(screen->r) != Dx(back->r) || Dy(screen->r) != Dy(back->r)){
		freeimage(back);
		back = allocimage(display, Rpt(ZP,subpt(screen->r.max, screen->r.min)), XRGB32, 0, DNofill);
		full = 1;
	}

	r = screen->r;

	/* scrollbar */
	p₀ = Pt(r.min.x, r.min.y);
	p₁ = Pt(r.min.x+Scrollwidth, r.max.y-Seekthicc);
	if(scroll < 1)
		scrollcenter = 0;
	else
		scrollcenter = (p₁.y-p₀.y-Scrollheight/2 - Seekthicc)*scroll / (pl->n - scrollsz);
	if(full || oscrollcenter != scrollcenter){
		draw(screen, Rpt(p₀, Pt(p₁.x, p₁.y)), colors[Dback].im, nil, ZP);
		line(screen, Pt(p₁.x, p₀.y), p₁, Endsquare, Endsquare, 0, colors[Dflow].im, ZP);
		r = Rpt(
			Pt(p₀.x+1, p₀.y + scrollcenter + Scrollheight/4),
			Pt(p₁.x-1, p₀.y + scrollcenter + Scrollheight/4 + Scrollheight)
		);
		/* scrollbar handle */
		draw(screen, r, colors[Dblow].im, nil, ZP);
	}

	/* seek bar rectangle */
	r = Rpt(Pt(p₀.x, p₁.y), Pt(screen->r.max.x-w-4, screen->r.max.y));

	/* playback/duration text */
	draw(screen, Rpt(Pt(r.max.x, p₁.y), screen->r.max), colors[Dblow].im, nil, ZP);
	p = addpt(Pt(screen->r.max.x - stringwidth(f, tmp) - 4, p₁.y), Pt(2, 2));
	string(screen, p, colors[Dfhigh].im, ZP, f, tmp);

	/* seek control */
	if(pcurplaying >= 0 && dur > 0){
		border(screen, r, 3, colors[Dblow].im, ZP);
		r = insetrect(r, 3);
		seekbar = r;
		p = r.min;
		x = p.x + Dx(r) * (double)msec / (double)dur;
		r.min.x = x;
		draw(screen, r, colors[Dback].im, nil, ZP);
		r.min.x = p.x;
		r.max.x = x;
		draw(screen, r, colors[Dbmed].im, nil, ZP);
	}else
		draw(screen, r, colors[Dblow].im, nil, ZP);

	Rectangle bp[2] = {
		Rpt(addpt(screen->r.min, Pt(Scrollwidth+1, 0)), subpt(screen->r.max, Pt(0, Seekthicc))), 
		ZR,
	};

	if(cover != nil){
		r.min.x = screen->r.max.x - Dx(cover->r) - 8;
		r.min.y = p₁.y - Dy(cover->r) - 6;
		r.max.x = screen->r.max.x;
		r.max.y = p₁.y + 2;
		if(full || cover != ocover){
			border(screen, r, 4, colors[Dblow].im, ZP);
			draw(screen, insetrect(r, 4), cover, nil, ZP);
		}
		bp[1] = bp[0];
		bp[0].max.y = r.min.y;
		bp[1].max.x = r.min.x;
		bp[1].min.y = r.min.y;
	}else if(ocover != nil)
		full = 1;

	/* playlist */
	if(full || oscrollcenter != scrollcenter || pcur != opcur || pcurplaying != opcurplaying){
		draw(back, back->r, colors[Dback].im, nil, ZP);

		p.x = sp.x = Scrollwidth;
		p.y = 0;
		sp.y = back->r.max.y;
		for(i = 0; cols[i+1] != 0; i++){
			p.x += colwidth[i] + 4;
			sp.x = p.x;
			line(back, p, sp, Endsquare, Endsquare, 0, colors[Dflow].im, ZP);
			p.x += 4;
		}

		sp.x = sp.y = 0;
		p.x = Scrollwidth + 2;
		p.y = back->r.min.y + 2;

		for(i = scroll; i < pl->n; i++, p.y += f->height){
			if(i < 0)
				continue;
			if(p.y > back->r.max.y)
				break;

			if(pcur == i){
				sel.min.x = Scrollwidth;
				sel.min.y = p.y;
				sel.max.x = back->r.max.x;
				sel.max.y = p.y + f->height;
				replclipr(back, 0, back->r);
				draw(back, sel, colors[Dbinv].im, nil, ZP);
				col = colors[Dfinv].im;
			}else{
				col = colors[Dfmed].im;
			}

			sel = back->r;

			p.x = Scrollwidth + 2 + 3;
			for(j = 0; cols[j] != 0; j++){
				sel.max.x = cols[j+1] ? (p.x + colwidth[j] - 1) : back->r.max.x;
				replclipr(back, 0, sel);
				if(playercurr != nil && playercurr->icytitle != nil && pcurplaying == i && cols[j] == Ptitle)
					s = playercurr->icytitle;
				else
					s = getcol(getmeta(i), cols[j]);
				string(back, p, col, sp, f, s);
				p.x += colwidth[j] + 8;
			}

			if(pcurplaying == i){
				Point rightp, leftp;
				leftp.y = rightp.y = p.y - 1;
				leftp.x = Scrollwidth;
				rightp.x = back->r.max.x;
				replclipr(back, 0, back->r);
				line(back, leftp, rightp, 0, 0, 0, colors[Dflow].im, sp);
				leftp.y = rightp.y = p.y + f->height;
				line(back, leftp, rightp, 0, 0, 0, colors[Dflow].im, sp);
			}
		}

		for(i = 0; bp[i].max.x ; i++)
			draw(screen, bp[i], back, nil, subpt(bp[i].min, screen->r.min));
	}
	oscrollcenter = scrollcenter;
	opcurplaying = pcurplaying;
	ocover = cover;
	opcur = pcur;

	flushimage(display, 1);
	unlockdisplay(display);
}

static void
redrawproc(void *)
{
	ulong full, nbfull, another;

	threadsetname("redraw");
	while(recv(redrawc, &full) == 1){
Again:
		redraw_(full);
		another = 0;
		full = 0;
		while(nbrecv(redrawc, &nbfull) > 0){
			full |= nbfull;
			another = 1;
		}
		if(another)
			goto Again;
	}

	threadexits(nil);
}

static void
redraw(int full)
{
	sendul(redrawc, full);
}

static void
coverload(void *player_)
{
	int p[2], pid, fd, i;
	char *prog, *path, *s, tmp[64];
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

	if(m->imagefmt != nil)
		prog = "audio/readtags -i";
	else{
		path = strdup(m->path);
		if(path != nil && (s = utfrrune(path, '/')) != nil){
			*s = 0;

			for(i = 0; i < nelem(covers) && prog == nil; i++){
				if((s = smprint("%s/%s.jpg", path, covers[i])) != nil && (fd = open(s, OREAD|OCEXEC)) >= 0)
					prog = "jpg -9t";
				free(s);
				s = nil;
				if(fd < 0 && (s = smprint("%s/%s.png", path, covers[i])) != nil && (fd = open(s, OREAD|OCEXEC)) >= 0)
					prog = "png -9t";
				free(s);
			}
		}
		free(path);
	}

	if(prog == nil)
		goto done;
	if(fd < 0)
		fd = open(m->path, OREAD|OCEXEC);
	snprint(tmp, sizeof(tmp), "%s | resample -x%d", prog, Coversz);
	pipe(p);
	if((pid = rfork(RFPROC|RFFDG|RFNOTEG|RFCENVG|RFNOWAIT)) == 0){
		dup(fd, 0); close(fd);
		dup(p[1], 1); close(p[1]);
		if(!debug){
			dup(fd = open("/dev/null", OWRITE), 2);
			close(fd);
		}
		execl("/bin/rc", "rc", "-c", tmp, nil);
		sysfatal("execl: %r");
	}
	close(fd);
	close(p[1]);

	if(pid > 0){
		newcover = readimage(display, p[0], 1);
		/* if readtags fails, readimage will also fail, and we send nil over ch */
		sendp(ch, newcover);
	}
	close(p[0]);
done:
	if(pid < 0)
		sendp(ch, nil);
	chanclose(ch);
	chanfree(ch);
	postnote(PNGROUP, pid, "die");
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
	int i;

	if(!pnotifies)
		return;

	if(p != nil){
		m = getmeta(p->pcur);
		for(i = 0; cols[i] != 0; i++)
			Bprint(&out, "%s\t", getcol(m, cols[i]));
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

static void
setgain(Player *player)
{
	if(player == nil)
		return;
	if(rg == Rgdisabled)
		player->gain = 0.0;
	if(rg == Rgtrack)
		player->gain = getmeta(player->pcur)->rgtrack;
	else if(rg == Rgalbum)
		player->gain = getmeta(player->pcur)->rgalbum;
	player->gain = pow(10.0, player->gain/20.0);
}

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
	setgain(player);

	threadcreate(playerthread, player, 32768);
	if(getmeta(pcur)->filefmt[0] && playerret(player) < 0)
		return nil;

done:
	if(pcur < pl->n-1 && playernext == nil && loadnext)
		playernext = newplayer(pcur+1, 0);

	return player;
}

static long
iosetname(va_list *)
{
	procsetname("player/io");
	return 0;
}

static int
clip16(int v)
{
	if(v > 0x7fff)
		return 0x7fff;
	if(v < -0x8000)
		return -0x8000;
	return v;
}

static void
gain(double g, char *buf, long n)
{
	s16int *f;

	if(g != 1.0)
		for(f = (s16int*)buf; n >= 4; n -= 4){
			*f++ = clip16(*f * g);
			*f++ = clip16(*f * g);
		}
}

static void
playerthread(void *player_)
{
	char *buf, cmd[64], seekpos[12], *fmt, *path, *icytitle;
	Player *player;
	Ioproc *io;
	Image *thiscover;
	ulong c;
	int p[2], q[2], fd, pid, noinit, trycoverload;
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

restart:
	cur = getmeta(player->pcur);
	fmt = cur->filefmt;
	path = cur->path;
	fd = -1;
	q[0] = -1;
	pid = -1;
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
		if(strncmp(cur->path, "http://", 7) == 0){ /* try icy */
			pipe(q);
			if(icyget(cur, q[0], &player->icytitlec) == 0){
				fd = q[1];
				path = nil;
			}else{
				close(q[0]); q[0] = -1;
				close(q[1]);
			}
		}
	}

	pipe(p);
	if((pid = rfork(RFPROC|RFFDG|RFNOTEG|RFCENVG|RFNOWAIT)) == 0){
		close(q[0]);
		close(p[1]);
		if(fd < 0)
			fd = open("/dev/null", OREAD);
		dup(fd, 0); close(fd); /* fd == q[1] when it's Icy */
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
			execl("/bin/play", "play", "-o", "/fd/1", path, nil);
		}
		exits("%r");
	}
	if(pid < 0)
		sysfatal("rfork: %r");
	/* fd is q[1] when it's Icy */
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
		iocall(io, iosetname);
		if((n = ioreadn(io, p[1], buf, Relbufsz)) < 0)
			fprint(2, "player: %r\n");
		if(recv(player->ctl, &c) < 0 || c != Cstart)
			goto freeplayer;
		if(n < 1)
			goto next;
		audioon();
		gain(player->gain, buf, n);
		boffset = iowrite(io, audio, buf, n);
		noinit = 1;
	}

	boffsetlast = boffset;
	byteswritten = boffset;
	pcurplaying = player->pcur;
	redraw(1);

	while(1){
		n = ioread(io, p[1], buf, Relbufsz);
		if(n <= 0){
			if(repeatone){
				c = Cseekrel;
				boffset = 0;
			}
			break;
		}
		if(player->icytitlec != nil && nbrecv(player->icytitlec, &icytitle) != 0){
			free(player->icytitle);
			player->icytitle = icytitle;
			redraw(1);
		}
		thiscover = nil;
		if(player->img != nil && nbrecv(player->img, &thiscover) != 0){
			freeimage(cover);
			cover = thiscover;
			redraw(0);
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
			if(c == Cseekrel && *fmt){
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
		gain(player->gain, buf, n);
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
		postnote(PNGROUP, pid, "die");
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
	close(p[1]);
	closeioproc(io);
	postnote(PNGROUP, pid, "die");
	if(player->icytitlec != nil){
		while((icytitle = recvp(player->icytitlec)) != nil)
			free(icytitle);
		chanfree(player->icytitlec);
	}
	chanfree(player->ctl);
	chanfree(player->ev);
	if(player == playercurr)
		playercurr = nil;
	if(player == playernext)
		playernext = nil;
	free(buf);
	free(player->icytitle);
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
	if(player != nil){
		player->seek = off;
		sendul(player->ctl, Cseekrel);
	}
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
		if(n == 0)
			werrstr("empty");
		free(s);
		return nil;
	}
	s[sz] = 0;

	return s;
}

static int
cmpint(void *a, void *b)
{
	return *(int*)a - *(int*)b;
}

static Playlist *
readplist(int fd, int mincolwidth[Ncol])
{
	char *raw, *s, *e, *a[5], *b;
	int plsz, i, *w;
	Playlist *pl;
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
	for(s = pl->raw, m = pl->m, e = s; e != nil; s = e){
		if((e = strchr(s, '\n')) == nil)
			goto addit;
		s += 2;
		*e++ = 0;
		switch(s[-2]){
		case 0:
addit:
			if(m->path != nil){
				if(m->filefmt == nil)
					m->filefmt = "";
				if(m->numartist == 0 && m->composer != nil)
					m->artist[m->numartist++] = m->composer;
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
		case Pcomposer: m->composer = s; break;
		case Pfilefmt:  m->filefmt = s; break;
		case Palbum:    m->album = s; break;
		case Pdate:     m->date = s; break;
		case Ptitle:    m->title = s; break;
		case Ptrack:    m->track = s; break;
		case Prgtrack:  m->rgtrack = atof(s); break;
		case Prgalbum:  m->rgalbum = atof(s); break;
		case Ppath:
			m->path = s;
			m->basename = (b = utfrrune(s, '/')) == nil ? s : b+1;
			break;
		}
	}

	w = malloc(sizeof(int)*pl->n);
	for(i = 0; cols[i] != 0; i++){
		for(m = pl->m; m != pl->m + pl->n; m++)
			w[m - pl->m] = stringwidth(f, getcol(m, cols[i]));
		qsort(w, pl->n, sizeof(*w), cmpint);
		mincolwidth[i] = w[93*(pl->n-1)/100];
	}
	free(w);

	return pl;
}

static void
recenter(void)
{
	updatescrollsz();
	scroll = pcur - scrollsz/2 + 1;
}

static void
seekto(char *s)
{
	vlong p;
	char *e;

	for(p = 0; *s; s = e){
		p += strtoll(s, &e, 10);
		if(s == e)
			break;
		if(*e == ':'){
			p *= 60;
			e++;
		}
	}

	seekrel(playercurr, p - byteswritten/Bps);
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
		sz = enter(inc > 0 ? "forward:" : "backward:", buf, sizeof(buf), mctl, &kctl, screen->screen);
	if(sz < 1){
		redraw(1);
		return;
	}

	cycle = 1;
	for(i = pcur+inc; i >= 0 && i < pl->n;){
		m = getmeta(i);
		for(a = 0; a < m->numartist; a++){
			if(cistrstr(m->artist[a], buf) != nil)
				break;
		}
		if(a < m->numartist)
			break;
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
	}else if(cycle && i+inc < 0){
		cycle = 0;
		i = pl->n;
		goto onemore;
	}else if(cycle && i+inc >= pl->n){
		cycle = 0;
		i = -1;
		goto onemore;
	}
	redraw(1);
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
adjustcolumns(void)
{
	int i, n, total, width;

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

static void
plumbaudio(void *kbd)
{
	int i, f, pf, mcw[Ncol], playing, shuffled;
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

			if((e = strrchr(s, '.')) != nil && strcmp(e, ".plist") == 0 && (pf = open(s, OREAD|OCEXEC)) >= 0){
				p = readplist(pf, mcw);
				close(pf);
				if(p == nil)
					continue;
				playing = pcurplaying;
				if(shuffled = (shuffle != nil))
					sendul(kbd, 's');
				/* make sure nothing is playing */
				while(pcurplaying >= 0){
					sendul(kbd, 'v');
					sleep(100);
				}
				freeplist(pl);
				pl = p;
				memmove(mincolwidth, mcw, sizeof(mincolwidth));
				adjustcolumns();
				pcur = 0;
				if(shuffled){
					pcur = nrand(pl->n);
					sendul(kbd, 's');
				}
				redraw(1);
				if(playing >= 0)
					sendul(kbd, '\n');
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
kbproc(void *cchan)
{
	char *s, buf[128], buf2[128];
	int kbd, n;
	Rune r;

	threadsetname("kbproc");
	if((kbd = open("/dev/kbd", OREAD|OCEXEC)) < 0)
		sysfatal("/dev/kbd: %r");

	buf2[0] = 0;
	buf2[1] = 0;
	buf[0] = 0;
	for(;;){
		if(buf[0] != 0){
			n = strlen(buf)+1;
			memmove(buf, buf+n, sizeof(buf)-n);
		}
		if(buf[0] == 0){
			n = read(kbd, buf, sizeof(buf)-1);
			if(n <= 0)
				break;
			buf[n-1] = 0;
			buf[n] = 0;
		}

		switch(buf[0]){
		case 'k':
			for(s = buf+1; *s;){
				s += chartorune(&r, s);
				if(utfrune(buf2+1, r) == nil){
					if(r == Kshift)
						shiftdown = 1;
				}
			}
			break;
		case 'K':
			for(s = buf2+1; *s;){
				s += chartorune(&r, s);
				if(utfrune(buf+1, r) == nil){
					if(r == Kshift)
						shiftdown = 0;
				}
			}
			break;
		case 'c':
			if(chartorune(&r, buf+1) > 0 && r != Runeerror)
				nbsend(cchan, &r);
		default:
			continue;
		}

		strcpy(buf2, buf);
	}

	close(kbd);
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
	int seekmx, full;
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

	Binit(&out, 1, OWRITE);
	pnotifies = fd2path(1, buf, sizeof(buf)) == 0 && strcmp(buf, "/dev/cons") != 0;

	if(initdraw(nil, nil, "zuke") < 0)
		sysfatal("initdraw: %r");
	unlockdisplay(display);
	display->locking = 1;
	f = display->defaultfont;
	Scrollwidth = MAX(14, stringwidth(f, "#"));
	Scrollheight = MAX(16, f->height);
	Seekthicc = Scrollheight + 2;
	Coversz = MAX(64, stringwidth(f, "¹∫ 00:00:00/00:00:00 100%"));
	if((mctl = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");

	kctl.c = chancreate(sizeof(Rune), 20);
	proccreate(kbproc, kctl.c, 4096);
	playc = chancreate(sizeof(ind), 0);

	a[Emouse].c = mctl->c;
	a[Eresize].c = mctl->resizec;
	a[Ekey].c = kctl.c;
	a[Eplay].c = playc;

	redrawc = chancreate(sizeof(ulong), 8);
	proccreate(redrawproc, nil, 8192);

	for(n = 0; n < Numcolors; n++)
		colors[n].im = allocimage(display, Rect(0,0,1,1), XRGB32, 1, colors[n].rgb<<8 | 0xff);

	srand(time(0));
	pcurplaying = -1;
	chvolume(0);
	fmtinstall('P', positionfmt);
	threadsetname("zuke");

	if((pl = readplist(0, mincolwidth)) == nil){
		fprint(2, "playlist: %r\n");
		sysfatal("playlist error");
	}

	m.buttons = 0;
	scrolling = 0;
	seekmx = 0;
	adjustcolumns();

	proccreate(plumbaudio, kctl.c, 4096);

	if(shuffled){
		pcur = nrand(pl->n);
		toggleshuffle();
	}
	full = 1;

	for(;;){
		updatescrollsz();
		scroll = CLAMP(scroll, 0, pl->n - scrollsz);
		redraw(full);

		oldpcur = pcur;
		full = 0;
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
			if(oldbuttons == m.buttons && m.buttons == 0)
				continue;

			if(m.buttons != 2)
				scrolling = 0;
			if(m.buttons == 0)
				break;
			if(m.buttons == 8){
				scroll -= (shiftdown ? 0 : scrollsz/4)+1;
				break;
			}else if(m.buttons == 16){
				scroll += (shiftdown ? 0 : scrollsz/4)+1;
				break;
			}

			n = (m.xy.y - screen->r.min.y)/f->height;

			if(m.xy.x <= screen->r.min.x+Scrollwidth && m.xy.y <= screen->r.max.y-Seekthicc){
				if(m.buttons == 1){
					scroll -= n+1;
					break;
				}else if(m.buttons == 4){
					scroll += n+1;
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
				scroll = (m.xy.y - screen->r.min.y)*(pl->n-scrollsz) / (Dy(screen->r)-Seekthicc);
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
			adjustcolumns();
			redraw(1);
			break;
		case Ekey:
			switch(key){
			default:
				if(isdigit(key) && pcurplaying >= 0 && getmeta(pcurplaying)->duration > 0){
					buf[0] = key;
					buf[1] = 0;
					if(enter("seek:", buf, sizeof(buf), mctl, &kctl, screen->screen) < 1)
						redraw(1);
					else
						seekto(buf);
				}
				break;
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
				stop(playernext);
				threadexitsall(nil);
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
				break;
			case '-':
				chvolume(-1);
				redraw(0);
				continue;
			case '+':
			case '=':
				chvolume(+1);
				redraw(0);
				continue;
			case 'v':
				stop(playercurr);
				stop(playernext);
				playercurr = nil;
				playernext = nil;
				pcurplaying = -1;
				freeimage(cover);
				cover = nil;
				full = 1;
				break;
			case 'g':
				rg = (rg+1) % Numrg;
				setgain(playercurr);
				setgain(playernext);
				redraw(0);
				break;
			case 's':
				toggleshuffle();
				recenter();
				full = 1;
				break;
			case 'r':
				repeatone ^= 1;
				redraw(0);
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
		}
	}
}
