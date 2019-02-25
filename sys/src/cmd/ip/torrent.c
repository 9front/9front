#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

typedef struct Dict Dict;
typedef struct Piece Piece;
typedef struct File File;
typedef struct Stats Stats;

struct Dict
{
	Dict	*val;
	Dict	*next;
	char	*start, *end;
	int	len;
	char	typ;	// i, d, s, l
	char	str[];
};

struct Piece
{
	uchar	*hash;
	int	len;
	int	brk;
};

struct File
{
	File	*next;
	char	*name;
	int	fd;
	vlong	off;
	vlong	len;
};

struct Stats
{
	Lock;
	vlong	up;
	vlong	down;
	vlong	left;
};

enum {
	MAXIO = 16*1024,
	SRVPROCS = 16,
	CLIPROCS = 16,
};

int debug;
int nproc = 1;
int killgroup = -1;
int port = 6881;
char *deftrack = "http://exodus.desync.com/announce";
char *mntweb = "/mnt/web";
char *useragent = "torrent";
uchar infohash[SHA1dlen];
uchar peerid[20];
int blocksize;

int npieces;
Piece *pieces;

int nhavemap;
uchar *havemap;
int nhavepieces;

File *files;
Stats stats;

int
finished(void)
{
	return nhavepieces >= npieces;
}

void
freedict(Dict *d)
{
	if(d){
		if(d->val != d)
			freedict(d->val);
		freedict(d->next);
		free(d);
	}
}

char*
bparse(char *s, char *e, Dict **dp)
{
	char *x, t;
	Dict *d;
	int n;

	*dp = nil;
	if(s >= e)
		return e;

	t = *s;
	switch(t){
	case 'd':
	case 'l':
		x = s++;
		d = nil;
		while(s < e){
			if(*s == 'e'){
				s++;
				break;
			}
			if(t == 'd'){
				s = bparse(s, e, dp);
				if((d = *dp) == nil)
					break;
			} else
				d = *dp = mallocz(sizeof(*d), 1);
			d->typ = t;
			d->start = x;
			if(s < e){
				s = bparse(s, e, &d->val);
				dp = &d->next;
				d->end = s;
			}
			x = s;
		}
		if(d)
			d->end = s;
		return s;
	case 'i':
		x = ++s;
		if((s = memchr(x, 'e', e - x)) == nil)
			return e;
		n = s - x;
		s++;
		break;
	default:
		if((x = memchr(s, ':', e - s)) == nil)
			return e;
		x++;
		if((n = atoi(s)) < 0)
			return e;
		s = x + n;
		if((s > e) || (s < x)){
			n = e - x;
			s = e;
		}
		t = 's';
	}
	d = mallocz(sizeof(*d) + n+1, 1);
	d->typ = t;
	memmove(d->str, x, d->len = n);
	d->str[n] = 0;
	*dp = d;
	return s;
}

char*
dstr(Dict *d)
{
	if(d && (d->typ == 's' || d->typ == 'i'))
		return d->str;
	return nil;
}

Dict*
dlook(Dict *d, char *s)
{
	for(; d && d->typ == 'd'; d = d->next)
		if(d->len && strcmp(d->str, s) == 0)
			return d->val;
	return nil;
}

int
readall(int fd, char **p)
{
	int n, r;

	n = 0;
	*p = nil;
	while(*p = realloc(*p, n+1024)){
		if((r = read(fd, *p+n, 1024)) <= 0)
			break;
		n += r;
	}
	return n;
}

int
rwpiece(int wr, int index, uchar *data, int len, int poff)
{
	vlong off;
	int n, m;
	File *f;

	if(len <= 0 || poff < 0 || poff >= pieces[index].len)
		return 0;
	if(len+poff > pieces[index].len)
		len = pieces[index].len - poff;
	off = (vlong)index * (vlong)blocksize;
	off += poff;
	for(f = files; f; f = f->next)
		if((f->off+f->len) > off)
			break;
	off -= f->off;
	n = ((off + len) > f->len) ? f->len - off : len;
	if((n = (wr ? pwrite(f->fd, data, n, off) : pread(f->fd, data, n, off))) <= 0)
		return -1;
	if((m = rwpiece(wr, index, data + n, len - n, poff + n)) < 0)
		return -1;
	return n+m;
}

int
havepiece(int x, char *from)
{
	uchar *p, m, hash[SHA1dlen];
	int n;

	m = 0x80>>(x&7);
	if(havemap[x>>3] & m)
		return 1;
	p = malloc(blocksize);
	n = pieces[x].len;
	if(rwpiece(0, x, p, n, 0) != n){
		free(p);
		return 0;
	}
	sha1(p, n, hash, nil);
	free(p);
	if(memcmp(hash, pieces[x].hash, sizeof(hash))){
		if(debug && from != nil)
			fprint(2, "peer %s: damaged piece %d\n", from, x);
		return 0;
	}
	lock(&stats);
	if((havemap[x>>3] & m) == 0){
		havemap[x>>3] |= m;
		nhavepieces++;
		stats.left -= pieces[x].len;
	}
	unlock(&stats);
	if(debug && from != nil)
		fprint(2, "peer %s: completed piece %d\n", from, x);
	return 1;
}

int
pickpiece(uchar *map)
{
	int i, x, r, k;
	uchar m;

	r = -1;
	k = 0;
	for(i = 0; i<nhavemap; i++){
		if(map[i] == 0)
			continue;
		for(x = i<<3, m = 0x80; m; m >>= 1, x++){
			if((~map[i] | havemap[i]) & m)
				continue;
			if(nrand(++k) == 0)
				r = x;
		}
	}
	return r;
}

int
unpack(uchar *s, int n, char *fmt, ...)
{
	va_list arg;
	uchar *b, *e;

	b = s;
	e = b + n;
	va_start(arg, fmt);
	for(; *fmt; fmt++) {
		switch(*fmt){
		case '_':
			s++;
			break;
		case 'b':
			if(s+1 > e) goto Err;
			*va_arg(arg, int*) = *s++;
			break;
		case 'w':
			if(s+2 > e) goto Err;
			*va_arg(arg, int*) = s[0]<<8 | s[1];
			s += 2;
			break;
		case 'l':
			if(s+4 > e) goto Err;
			*va_arg(arg, int*) = s[0]<<24 | s[1]<<16 | s[2]<<8 | s[3];
			s += 4;
			break;
		case 'v':
			if(s+4 > e) goto Err;
			*va_arg(arg, vlong*) = 
				(vlong)s[0]<<56 | 
				(vlong)s[1]<<48 | 
				(vlong)s[2]<<40 |
				(vlong)s[3]<<32 |
				(vlong)s[4]<<24 |
				(vlong)s[5]<<16 | 
				(vlong)s[6]<<8 | 
				(vlong)s[7];
			s += 8;
			break;
		}
	}
	va_end(arg);
	return s - b;
Err:
	va_end(arg);
	return -1;
}

int
pack(uchar *s, int n, char *fmt, ...)
{
	va_list arg;
	uchar *b, *e;
	vlong v;
	int i;

	b = s;
	e = b + n;
	va_start(arg, fmt);
	for(; *fmt; fmt++) {
		switch(*fmt){
		case '_':
			i = 0;
			if(0){
		case 'b':
			i = va_arg(arg, int);
			}
			if(s+1 > e) goto Err;
			*s++ = i & 0xFF;
			break;
		case 'w':
			i = va_arg(arg, int);
			if(s+2 > e) goto Err;
			*s++ = (i>>8) & 0xFF;
			*s++ = i & 0xFF;
			break;
		case 'l':
			i = va_arg(arg, int);
			if(s+4 > e) goto Err;
			*s++ = (i>>24) & 0xFF;
			*s++ = (i>>16) & 0xFF;
			*s++ = (i>>8) & 0xFF;
			*s++ = i & 0xFF;
			break;
		case 'v':
			v = va_arg(arg, vlong);
			if(s+8 > e) goto Err;
			*s++ = (v>>56) & 0xFF;
			*s++ = (v>>48) & 0xFF;
			*s++ = (v>>40) & 0xFF;
			*s++ = (v>>32) & 0xFF;
			*s++ = (v>>24) & 0xFF;
			*s++ = (v>>16) & 0xFF;
			*s++ = (v>>8) & 0xFF;
			*s++ = v & 0xFF;
			break;
		case '*':
			i = va_arg(arg, int);
			if(s+i > e) goto Err;
			memmove(s, va_arg(arg, void*), i);
			s += i;
			break;
		}
	}
	va_end(arg);
	return s - b;
Err:
	va_end(arg);
	return -1;
}



int
peer(int fd, int incoming, char *addr)
{
	uchar buf[64+MAXIO], *map, *told, *p, m;
	int mechoking, hechoking;
	int mewant, hewant;
	int workpiece, workoffset;
	int i, o, l, x, n;

	if(debug) fprint(2, "peer %s: %s connected\n", addr, incoming ? "incoming" : "outgoing");

	for(i=0; i<2; i++){
		if((incoming && i) || (!incoming && !i)){
			if(debug) fprint(2, "peer %s: -> handshake\n", addr);
			n = pack(buf, sizeof(buf), "*________**", 
				20, "\x13BitTorrent protocol",
				sizeof(infohash), infohash,
				sizeof(peerid), peerid);
			if(write(fd, buf, n) != n)
				return 1;
		}
		if((incoming && !i) || (!incoming && i)){
			n = 20 + 8 + sizeof(infohash);
			if((n = readn(fd, buf, n)) != n)
				return 1;
			if(memcmp(buf, "\x13BitTorrent protocol", 20))
				return 0;
			if(memcmp(infohash, buf + 20 + 8, sizeof(infohash)))
				return 0;
			if(debug) fprint(2, "peer %s: <- handshake\n", addr);
		}
	}
	if(readn(fd, buf, sizeof(peerid)) != sizeof(peerid))
		return 1;
	if(memcmp(peerid, buf, sizeof(peerid)) == 0)
		return 0;
	if(debug) fprint(2, "peer %s: peerid %.*s\n", addr, sizeof(peerid), (char*)buf);

	mechoking = 1;
	hechoking = 1;
	mewant = 0;
	hewant = 0;
	workpiece = -1;
	workoffset = 0;

	map = mallocz(nhavemap, 1);
	told = malloc(nhavemap);

	if(debug) fprint(2, "peer %s: -> bitfield %d\n", addr, nhavemap);
	memmove(told, havemap, nhavemap);
	n = pack(buf, sizeof(buf), "lb*", nhavemap+1, 0x05, nhavemap, told);
	if(write(fd, buf, n) != n)
		goto Out;

	for(;;){
		for(i=0; i<nhavemap; i++){
			if(told[i] != havemap[i]){
				for(x = i<<3, m = 0x80; m; m >>= 1, x++){
					if((~havemap[i] | told[i] | map[i]) & m)
						continue;
					told[i] |= m;
					if(debug) fprint(2, "peer %s: -> have %d\n", addr, x);
					n = pack(buf, sizeof(buf), "lbl", 1+4, 0x04, x);
					if(write(fd, buf, n) != n)
						goto Out;
				}
			}
			if(!mewant && (map[i] & ~havemap[i])){
				mewant = 1;
				if(debug) fprint(2, "peer %s: -> interested\n", addr);
				n = pack(buf, sizeof(buf), "lb", 1, 0x02);
				if(write(fd, buf, n) != n)
					goto Out;
			}
		}
		if(!hechoking && mewant){
			x = workpiece;
			if(x < 0 || (havemap[x>>3]&(0x80>>(x&7))) != 0 || workoffset >= pieces[x].len)
				x = pickpiece(map);
			if(x >= 0){
				o = workpiece != x ? pieces[x].brk : workoffset;
				l = pieces[x].len - o;
				if(l > MAXIO)
					l = MAXIO;
				workpiece = x;
				workoffset = o + l; 
				if(debug) fprint(2, "peer %s: -> request %d %d %d\n", addr, x, o, l);
				n = pack(buf, sizeof(buf), "lblll", 1+4+4+4, 0x06, x, o, l);
				if(write(fd, buf, n) != n)
					goto Out;
			}
		}
		if(mechoking && hewant){
			mechoking = 0;
			if(debug) fprint(2, "peer %s: -> unchoke\n", addr);
			n = pack(buf, sizeof(buf), "lb", 1, 0x01);
			if(write(fd, buf, n) != n)
				goto Out;
		}

		if(readn(fd, buf, 4) != 4)
			break;
		unpack(buf, 4, "l", &n);
		if(n < 0 || n > sizeof(buf))
			break;
		if(n == 0)
			continue;
		if(readn(fd, buf, n) != n)
			break;

		n--;
		p = buf+1;
		switch(*buf){
		case 0x00:	// Choke
			hechoking = 1;
			workpiece = -1;
			if(debug) fprint(2, "peer %s: <- choke\n", addr);
			break;
		case 0x01:	// Unchoke
			hechoking = 0;
			if(debug) fprint(2, "peer %s: <- unchoke\n", addr);
			break;
		case 0x02:	// Interested
			hewant = 1;
			if(debug) fprint(2, "peer %s: <- interested\n", addr);
			break;
		case 0x03:	// Notinterested
			hewant = 0;
			if(debug) fprint(2, "peer %s: <- notinterested\n", addr);
			break;
		case 0x04:	// Have <piceindex>
			if(unpack(p, n, "l", &x) < 0)
				goto Out;
			if(debug) fprint(2, "peer %s: <- have %d\n", addr, x);
			if(x < 0 || x >= npieces)
				continue;
			map[x>>3] |= 0x80>>(x&7);
			break;
		case 0x05:	// Bitfield
			if(debug) fprint(2, "peer %s: <- bitfield %d\n", addr, n);
			if(n != nhavemap)
				continue;
			memmove(map, p, n);
			break;
		case 0x06:	// Request <index> <begin> <length>
			if(unpack(p, n, "lll", &x, &o, &l) < 0)
				goto Out;
			if(debug) fprint(2, "peer %s: <- request %d %d %d\n", addr, x, o, l);
			if(x < 0 || x >= npieces)
				continue;
			if(!hewant || mechoking || (~havemap[x>>3]&(0x80>>(x&7))))
				continue;
			if(debug) fprint(2, "peer %s: -> piece %d %d\n", addr, x, o);
			n = 4+1+4+4;
			if(l > MAXIO)
				l = MAXIO;
			if((l = rwpiece(0, x, buf + n, l, o)) <= 0)
				continue;
			n = pack(buf, sizeof(buf), "lbll", 1+4+4+l, 0x07, x, o);
			n += l;
			if(write(fd, buf, n) != n)
				goto Out;
			lock(&stats);
			stats.up += n;
			unlock(&stats);
			break;
		case 0x07:	// Piece <index> <begin> <block>
			if(unpack(p, n, "ll", &x, &o) != 8)
				goto Out;
			p += 8;
			n -= 8;
			lock(&stats);
			stats.down += n;
			unlock(&stats);
			if(debug) fprint(2, "peer %s: <- piece %d %d %d\n", addr, x, o, n);
			if(x < 0 || x >= npieces)
				continue;
			if((havemap[x>>3]&(0x80>>(x&7))) != 0)
				continue;
			if(o < 0 || o >= pieces[x].len)
				continue;
			if(o+n > pieces[x].len)
				n = o - pieces[x].len;
			if((o > pieces[x].brk) || (o+n <= pieces[x].brk))
				continue;
			n = rwpiece(1, x, p, n, o);
			if(n <= 0)
				continue;
			pieces[x].brk = o+n;
			if(o+n >= pieces[x].len && !havepiece(x, addr)){
				pieces[x].brk = 0;
				/* backoff from this piece for a while */
				if(x == workpiece)
					workpiece = -1;
			}
			break;
		case 0x08:	// Cancel <index> <begin> <length>
			if(unpack(p, n, "lll", &x, &o, &l) < 0)
				goto Out;
			if(debug) fprint(2, "peer %s: <- cancel %d %d %d\n", addr, x, o, l);
			break;
		case 0x09:	// Port <port>
			if(unpack(p, n, "l", &x) < 0)
				goto Out;
			if(debug) fprint(2, "peer %s: <- port %d\n", addr, x);
			break;
		}
	}

Out:
	free(told);
	free(map);
	return 1;
}

void
server(void)
{
	char addr[64], adir[40], ldir[40];
	int afd, lfd, dfd, pid, nprocs;
	NetConnInfo *ni;

	afd = -1;
	nprocs = 0;
	for(port=6881; port<6890; port++){
		snprint(addr, sizeof(addr), "tcp!*!%d", port);
		if((afd = announce(addr, adir)) >= 0)
			break;
	}
	if(afd < 0){
		fprint(2, "announce: %r");
		return;
	}
	if(rfork(RFFDG|RFPROC|RFMEM))
		return;
	for(;;){
		if((lfd = listen(adir, ldir)) < 0){
			fprint(2, "listen: %r");
			break;
		}
		while(nprocs >= SRVPROCS)
			if(waitpid() > 0)
				nprocs--;
		nprocs++;
		if(pid = rfork(RFFDG|RFPROC|RFMEM)){
			if(pid < 0)
				nprocs--;
			close(lfd);
			continue;
		}
		if((dfd = accept(lfd, ldir)) < 0){
			fprint(2, "accept: %r");
			break;
		}
		ni = getnetconninfo(ldir, dfd);
		peer(dfd, 1, ni ? ni->raddr : "???");
		if(ni) freenetconninfo(ni);
		break;	
	}
	exits(0);
}

void
client(char *ip, char *port)
{
	static Dict *peerqh, *peerqt;
	static QLock peerslk;
	static int nprocs;
	char *addr;
	Dict *d;
	int fd;

	if(ip == nil || port == nil)
		return;

	d = mallocz(sizeof(*d) + 64, 1);
	snprint(addr = d->str, 64, "tcp!%s!%s", ip, port);
	qlock(&peerslk);
	if(dlook(peerqh, addr)){
		qunlock(&peerslk);
		free(d);
		return;
	}
	d->len = strlen(addr);
	d->typ = 'd';
	d->val = d;
	/* enqueue to front */
	if((d->next = peerqh) == nil)
		peerqt = d;
	peerqh = d;
	if(nprocs >= CLIPROCS){
		qunlock(&peerslk);
		return;
	}
	nprocs++;
	qunlock(&peerslk);
	if(rfork(RFFDG|RFPROC|RFMEM|RFNOWAIT))
		return;

	for(;;){
		qlock(&peerslk);
		/* dequeue and put to tail */
		if(d = peerqh){
			if((peerqh = d->next) == nil)
				peerqt = nil;
			d->next = nil;
			if(peerqt)
				peerqt->next = d;
			else
				peerqh = d;
			peerqt = d;
		} else
			nprocs--;
		qunlock(&peerslk);
		if(d == nil)
			exits(0);
		addr = d->str;
		if(debug) fprint(2, "client %s\n", addr);
		if((fd = dial(addr, nil, nil, nil)) >= 0){
			peer(fd, 0, addr);
			close(fd);
		}
		sleep(1000+nrand(5000));
	}
}

int
hopen(char *url, ...)
{
	int conn, ctlfd, fd, n;
	char buf[1024+1];
	va_list arg;

	snprint(buf, sizeof buf, "%s/clone", mntweb);
	if((ctlfd = open(buf, ORDWR)) < 0)
		return -1;
	if((n = read(ctlfd, buf, sizeof buf-1)) <= 0){
		close(ctlfd);
		return -1;
	}
	buf[n] = 0;
	conn = atoi(buf);
	va_start(arg, url);
	strcpy(buf, "url ");
	n = 4+vsnprint(buf+4, sizeof(buf)-4, url, arg);
	va_end(arg);
	if(write(ctlfd, buf, n) != n){
	ErrOut:
		close(ctlfd);
		return -1;
	}
	if(useragent != nil && useragent[0] != '\0'){
		n = snprint(buf, sizeof buf, "useragent %s", useragent);
		write(ctlfd, buf, n);
	}
	snprint(buf, sizeof buf, "%s/%d/body", mntweb, conn);
	if((fd = open(buf, OREAD)) < 0)
		goto ErrOut;
	close(ctlfd);
	return fd;
}

void
webseed(Dict *w, File *f)
{
	int fd, err, n, m, o, p, x, y;
	uchar buf[MAXIO];
	vlong off, len;
	Dict *w0;
	char *s;

	if(w == nil || f == nil || finished())
		return;
	if(rfork(RFPROC|RFMEM))
		return;
	w0 = w;
Retry:
	if(debug) fprint(2, "webseed %s %s\n", w->str, f->name);
	s = strrchr(w->str, '/');
	if(s && s[1] == 0)
		fd = hopen("%s%s", w->str, f->name);
	else
		fd = hopen("%s", w->str);
	if(fd < 0){
Error:
		if(debug) fprint(2, "webseed %s %s: %r\n", w->str, f->name);
		if(finished())
			exits(0);
		if((w = w->next) == w0)
			exits(0);
		goto Retry;
	}

	err = 0;
	off = f->off;
	len = f->len;
	while(len > 0 && !finished()){
		m = sizeof(buf);
		if(len < m)
			m = len;
		if((n = read(fd, buf, m)) <= 0)
			break;

		x = off / blocksize;
		p = off - (vlong)x*blocksize;
		off += n;
		len -= n;
		y = off / blocksize;

		o = 0;
		while(n > 0){
			m = pieces[x].len - p;
			if(m > n)
				m = n;
			if((havemap[x>>3] & (0x80>>(x&7))) == 0)
				rwpiece(1, x, buf+o, m, p);
			if(x == y)
				break;
			o += m;
			n -= m;
			p = 0;
			if(havepiece(x++, w->str))
				continue;
			if(++err > 10){
				close(fd);
				werrstr("file corrupted");
				goto Error;
			}
		}
	}
	if(off < f->off + f->len)
		havepiece(off / blocksize, w->str);
	havepiece(f->off / blocksize, w->str);
	close(fd);
	exits(0);
}

void
clients4(uchar *p, int len)
{
	char ip[16], port[6];

	while(len >= 6){
		len -= 6;
		snprint(ip, sizeof(ip), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
		snprint(port, sizeof(port), "%d", p[4]<<8 | p[5]);
		p += 6;
		client(ip, port);
	}
}

void
webtracker(char *url)
{
	char *event, *p;
	Dict *d, *l;
	int n, fd;

	if(rfork(RFPROC|RFMEM))
		return;
	if(debug) fprint(2, "webtracker %s\n", url);

	event = "&event=started";
	for(;;){
		vlong up, down, left;

		lock(&stats);
		up = stats.up;
		down = stats.down;
		left = stats.left;
		unlock(&stats);

		d = nil;
		if((fd = hopen("%s?info_hash=%.*H&peer_id=%.*H&port=%d&"
			"uploaded=%lld&downloaded=%lld&left=%lld&compact=1&no_peer_id=1%s",
			url, sizeof(infohash), infohash, sizeof(peerid), peerid, port,
			up, down, left, event)) >= 0){
			event = "";
			n = readall(fd, &p);
			close(fd);
			bparse(p, p+n, &d);
			free(p);
		} else if(debug) fprint(2, "tracker %s: %r\n", url);
		/* check errors and warnings */
		if(p = dstr(dlook(d, "failure reason"))) {
			if(debug)
				fprint(2, "tracker failure: %s\n", p);
			exits(0);
		}
		if(p = dstr(dlook(d, "warning message")))
			if(debug)
				fprint(2, "tracker warning: %s\n", p);
		if(l = dlook(d, "peers")){
			if(l->typ == 's')
				clients4((uchar*)l->str, l->len);
			else for(; l && l->typ == 'l'; l = l->next)
				client(dstr(dlook(l->val, "ip")), dstr(dlook(l->val, "port")));
		}
		n = 0;
		if(p = dstr(dlook(d, "interval")))
			n = atoi(p);
		if(n < 10 | n > 60*60)
			n = 2*60;
		freedict(d);
		sleep(n * 1000 + nrand(5000));
	}
}

int
udpaddr(char addr[64], int naddr, char *url)
{
	int port;
	char *x;

	if((url = strchr(url, ':')) == nil)
		return -1;
	url++;
	while(*url == '/')
		url++;
	if(x = strchr(url, ':')){
		port = atoi(x+1);
	} else {
		port = 80;
		if((x = strchr(url, '/')) == nil)
			x = strchr(url, 0);
	}
	snprint(addr, naddr, "udp!%.*s!%d", utfnlen(url, x-url), url, port);
	return 0;
}

void
udptracker(char *url)
{
	int fd, event, n, m, a, i;
	int transid, interval;
	vlong connid;
	uchar buf[MAXIO];
	char addr[64];

	if(udpaddr(addr, sizeof(addr), url) < 0)
		return;
	if(rfork(RFPROC|RFMEM))
		return;
	if(debug) fprint(2, "udptracker %s\n", addr);

	event = 1;
	for(;;){
		alarm(30000);
		if((fd = dial(addr, 0, 0, 0)) < 0)
			goto Sleep;

		/* connect */
		transid = rand();
		n = pack(buf, sizeof(buf), "vll", 0x41727101980LL, 0, transid);
		if(write(fd, buf, n) != n)
			goto Sleep;
		for(;;){
			if((n = read(fd, buf, sizeof(buf))) <= 0)
				goto Sleep;
			if(unpack(buf, n, "llv", &a, &i, &connid) < 0)
				continue;
			if(a == 0 && i == transid)
				break;
		}
		alarm(0);

		/* announce */
		transid = rand();
		lock(&stats);
		n = pack(buf, sizeof(buf), "vll**vvvl____llw",
			connid, 1, transid,
			sizeof(infohash), infohash,
			sizeof(peerid), peerid,
			stats.down,
			stats.left,
			stats.up,
			event,
			0, -1,
			port);
		unlock(&stats);

		interval = 0;
		alarm(30000);
		if(write(fd, buf, n) != n)
			goto Sleep;
		for(;;){
			if((n = read(fd, buf, sizeof(buf))) <= 0)
				goto Sleep;
			if((m = unpack(buf, n, "lll________", &a, &i, &interval)) < 0)
				continue;
			if(a == 1 && i == transid){
				clients4(buf+m, n - m);
				break;
			}
		}
		event = 0;
Sleep:
		alarm(0);
		if(fd >= 0)
			close(fd);
		if(interval < 10 | interval > 60*60)
			interval = 2*60;
		sleep(interval * 1000 + nrand(5000));
	}
}

void
tracker(char *url)
{
	static Dict *trackers;
	static QLock trackerslk;
	Dict *d;
	int n;

	if(url == nil)
		return;
	qlock(&trackerslk);
	if(dlook(trackers, url)){
		qunlock(&trackerslk);
		return;
	}
	n = strlen(url);
	d = mallocz(sizeof(*d) + n+1, 1);
	strcpy(d->str, url);
	d->len = n;
	d->typ = 'd';
	d->val = d;
	d->next = trackers;
	trackers = d;
	url = d->str;
	qunlock(&trackerslk);
	if(!cistrncmp(url, "udp:", 4))
		udptracker(url);
	else
		webtracker(url);
}

int
Hfmt(Fmt *f)
{
	uchar *s, *e;
	s = va_arg(f->args, uchar*);
	if(f->flags & FmtPrec)
		e = s + f->prec;
	else
		e = s + strlen((char*)s);
	for(; s < e; s++)
		if(fmtprint(f, *s && ((*s >= '0' && *s <= '9') || 
			(*s >= 'a' && *s <= 'z') ||
			(*s >= 'A' && *s <= 'Z') || 
			strchr(".-_~", *s)) ? "%c" : "%%%.2x", *s) < 0)
			return -1;
	return 0;
}

int
mktorrent(int fd, Dict *alist, Dict *wlist)
{
	uchar *b, h[SHA1dlen];
	Dir *d;
	int n;

	if((d = dirfstat(fd)) == nil)
		return -1;
	if(d->qid.type & QTDIR){
		free(d);
		werrstr("file is a directory");
		return -1;
	}
	if(d->length == 0){
		free(d);
		werrstr("empty file");
		return -1;
	}
	for(blocksize = 256*1024;;blocksize<<=1){
		npieces = (d->length + blocksize-1) / blocksize;
		if(npieces <= 8*1024 || blocksize >= 2*1024*1024)
			break;
	}

	/*
	 * keys in dictionaries have to be ordered alphabetically
	 */
	print("d8:announce%ld:%s", strlen(alist->str), alist->str);
	if(alist->next){
		print("13:announce-listl");
		print("l%ld:%se", strlen(alist->str), alist->str);
		for(alist = alist->next; alist; alist = alist->next)
			print("l%ld:%se", strlen(alist->str), alist->str);
		print("e");
	}

	print("4:infod");
	print("6:lengthi%llde", d->length);
	print("4:name%ld:%s", strlen(d->name), d->name);
	print("12:piece lengthi%de", blocksize);
	print("6:pieces%d:", npieces*sizeof(h));
	free(d);
	b = malloc(blocksize);
	while((n = readn(fd, b, blocksize)) > 0){
		sha1(b, n, h, nil);
		if(write(1, h, sizeof(h)) != sizeof(h)){
			free(b);
			return -1;
		}
		npieces--;
	}
	if(npieces){
		werrstr("read failed: %r");
		return -1;
	}
	free(b);
	print("e");

	if(wlist){
		if(wlist->next){
			print("8:url-listl");
			for(; wlist; wlist = wlist->next)
				print("%ld:%s", strlen(wlist->str), wlist->str);
			print("e");
		} else
			print("8:url-list%ld:%s", strlen(wlist->str), wlist->str);
	}
	print("e");

	return 0;
}

int
mkdirs(char *s)
{
	char *p;
	int f;

	if(access(s, AEXIST) == 0)
		return 0;
	for(p=strchr(s+1, '/'); p; p=strchr(p+1, '/')){
		*p = 0;
		if(access(s, AEXIST)){
			if((f = create(s, OREAD, DMDIR | 0777)) < 0){
				*p = '/';
				return -1;
			}
			close(f);
		}
		*p = '/';
	}
	return 0;
}

char*
fixnamedup(char *s)
{
	int n, l;
	char *d;
	Rune r;

	n = 0;
	d = strdup(s);
	l = strlen(d);
	while(*s){
		s += chartorune(&r, s);
		if(r == ' ')
			r = 0xa0;
		if((n + runelen(r)) >= l){
			l += 64;
			d = realloc(d, l);
		}
		n += runetochar(d + n, &r);
	}
	d[n] = 0;
	return cleanname(d);
}

int
catch(void *, char *msg)
{
	if(strstr(msg, "alarm"))
		return 1;
	postnote(PNGROUP, killgroup, "kill");
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: %s [ -vsdpc ] [ -m mtpt ] [ -t tracker-url ] "
		  "[ -w webseed-url ] [ -i peerid ] [ -A useragent ] [ file ]\n", argv0);
	exits("usage");
}

Dict*
scons(char *s, Dict *t)
{
	Dict *l;

	if(s == nil)
		return t;
	for(l = t; l; l = l->next)
		if(strcmp(l->str, s) == 0)
			return t;
	l = mallocz(sizeof(*l) + strlen(s)+1, 1);
	l->next = t;
	strcpy(l->str, s);
	return l;
}

void
main(int argc, char *argv[])
{
	int sflag, pflag, vflag, cflag, fd, i, n;
	Dict *alist, *wlist, *info, *torrent, *d, *l;
	char *p, *s, *e;
	File **fp, *f;
	vlong len;

	fmtinstall('H', Hfmt);
	alist = wlist = nil;
	sflag = pflag = vflag = cflag = 0;
	ARGBEGIN {
	case 'm':
		mntweb = EARGF(usage());
		break;
	case 't':
		alist = scons(EARGF(usage()), alist);
		break;
	case 'w':
		wlist = scons(EARGF(usage()), wlist);
		break;
	case 's':
		sflag = 1;
		break;
	case 'p':
		pflag = 1;
		break;
	case 'v':
		vflag = 1;
		break;
	case 'c':
		cflag = 1;
		break;
	case 'd':
		debug++;
		break;
	case 'i':
		strncpy((char*)peerid, EARGF(usage()), sizeof(peerid));
		break;
	case 'A':
		useragent = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if((s = getenv("NPROC")) != 0){
		if((nproc = atoi(s)) <= 0)
			nproc = 1;
		free(s);
	}

	fd = 0;
	if(*argv)
		if((fd = open(*argv, OREAD)) < 0)
			sysfatal("open: %r");
	if(cflag){
		if(alist == nil)
			alist = scons(deftrack, alist);
		if(mktorrent(fd, alist, wlist) < 0)
			sysfatal("%r");
		exits(0);
	}
	if((n = readall(fd, &p)) <= 0)
		sysfatal("read torrent: %r");
	bparse(p, p+n, &torrent);

	alist = scons(dstr(dlook(torrent, "announce")), alist);
	for(d = dlook(torrent, "announce-list"); d && d->typ == 'l'; d = d->next)
		for(l = d->val; l && l->typ == 'l'; l = l->next)
			alist = scons(dstr(l->val), alist);

	if(d = dlook(torrent, "url-list")){
		if(d->typ == 's')
			wlist = scons(dstr(d->val), wlist);
		else for(l = d; l && l->typ == 'l'; l = l->next)
			wlist = scons(dstr(l->val), wlist);
		/* make wlist into a ring */
		for(l = wlist; l && l->next; l = l->next)
			;
		if(l) l->next = wlist;
	}

	if(alist == nil && wlist == nil)
		sysfatal("no trackers or webseeds in torrent");

	if((d = info = dlook(torrent, "info")) == nil)
		sysfatal("no meta info in torrent");
	for(s = e = d->start; d && d->typ == 'd'; d = d->next)
		e = d->end;
	sha1((uchar*)s, e - s, (uchar*)infohash, nil);
	free(p);

	fp = &files;
	if(d = dlook(info, "files")){		
		for(; d && d->typ == 'l'; d = d->next){
			Dict *di;

			if((s = dstr(dlook(d->val, "length"))) == nil)
				continue;
			f = mallocz(sizeof(*f), 1);
			f->len = atoll(s);
			f->name = dstr(dlook(info, "name"));
			for(di = dlook(d->val, "path"); di && di->typ == 'l'; di = di->next)
				if(s = dstr(di->val))
					f->name = f->name ? smprint("%s/%s", f->name, s) : s;
			*fp = f;
			fp = &f->next;
		}
	} else if(s = dstr(dlook(info, "length"))){
		f = mallocz(sizeof(*f), 1);
		f->len = atoll(s);
		f->name = dstr(dlook(info, "name"));
		*fp = f;
	}
	len = 0;
	for(f = files; f; f = f->next){
		if(f->name == nil || f->len <= 0)
			sysfatal("bogus file entry in meta info");
		s = fixnamedup(f->name);
		if(vflag) fprint(pflag ? 2 : 1, "%s\n", s);
		if((f->fd = open(s, ORDWR)) < 0){
			if(mkdirs(s) < 0)
				sysfatal("mkdirs: %r");
			if((f->fd = create(s, ORDWR, 0666)) < 0)
				sysfatal("create: %r");
		}
		f->off = len;
		len += f->len;
	}
	if(len <= 0)
		sysfatal("no files in torrent");

	if((s = dstr(dlook(info, "piece length"))) == nil)
		sysfatal("missing piece length in meta info");
	if((blocksize = atoi(s)) <= 0)
		sysfatal("bogus piece length in meta info");
	d = dlook(info, "pieces");
	if(d == nil || d->typ != 's' || d->len <= 0 || d->len % SHA1dlen)
		sysfatal("bad or no pices in meta info");
	npieces = d->len / SHA1dlen;
	pieces = mallocz(sizeof(Piece) * npieces, 1);
	nhavemap = (npieces+7) / 8;
	havemap = mallocz(nhavemap, 1);
	for(i = 0; i<npieces; i++){
		pieces[i].hash = (uchar*)d->str + i*SHA1dlen;
		if(len < blocksize)
			pieces[i].len = len;
		else
			pieces[i].len = blocksize;
		len -= pieces[i].len;
		stats.left += pieces[i].len;
	}
	if(len)
		sysfatal("pieces do not match file length");

	for(i=0; i<nproc; i++){
		switch(rfork(RFPROC|RFMEM)){
		case -1:
			sysfatal("fork: %r");
		case 0:
			for(; i<npieces; i+=nproc)
				havepiece(i, nil);
			exits(0);
		}
	}
	while(waitpid() >= 0)
		;

	if(finished() && !sflag)
		exits(0);

	srand(truerand());
	atnotify(catch, 1);
	switch(i = rfork(RFPROC|RFMEM|RFNOTEG)){
	case -1:
		sysfatal("fork: %r");
	case 0:
		if(peerid[0] == 0)
			strncpy((char*)peerid, "-NF9001-", 9);
		for(i=sizeof(peerid)-1; i >= 0 && peerid[i] == 0; i--)
			peerid[i] = nrand(10)+'0';
		server();
		for(; alist; alist = alist->next)
			tracker(alist->str);
		for(f = files, l = wlist; f && l; f = f->next, l = l->next)
			webseed(l, f);
		while(waitpid() != -1)
			;
		break;
	default:
		killgroup = i;
		do {
			sleep(1000);
			if(pflag)
				print("%d %d\n", nhavepieces, npieces);
		} while(!finished() || sflag);
	}
	postnote(PNGROUP, killgroup, "kill");
	exits(0);
}
