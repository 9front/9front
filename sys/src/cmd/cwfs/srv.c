#include "all.h"
#include "io.h"
#include <thread.h>

enum {
	Nqueue		= 100,		/* reply queue size per connection (tunable) */
	Nchans		= 30,		/* maximum number of connections */
};

typedef struct Srv Srv;
struct Srv
{
	Ref;
	Chan	*chan;
	int	fd;
};

static struct {
	Lock;
	Chan	*hd;
} freechans;

void
chanhangup(Chan *chan, char *msg)
{
	char buf[128], *p;
	Srv *srv;
	int cfd;

	fileinit(chan);
	srv = chan->pdata;
	if(chan == cons.chan || srv == nil || srv->chan != chan)
		return;
	if(msg[0] && chatty)
		fprint(2, "hangup %s: %s\n", chan->whochan, msg);
	if(fd2path(srv->fd, buf, sizeof(buf)) == 0){
		if(p = strrchr(buf, '/')){
			strecpy(p, buf+sizeof(buf), "/ctl");
			if((cfd = open(buf, OWRITE)) >= 0){
				write(cfd, "hangup", 6);
				close(cfd);
			}
		}
	}
}

static void
srvput(Srv *srv)
{
	Chan *chan;

	if(decref(srv))
		return;

	close(srv->fd);
	srv->fd = -1;
	chan = srv->chan;
	fileinit(chan);
	if(chatty)
		fprint(2, "%s closed\n", chan->whochan);
	lock(&freechans);
	srv->chan = freechans.hd;
	freechans.hd = chan;
	unlock(&freechans);
}

static void
srvo(void *aux)
{
	Chan *chan;
	Srv *srv;
	Msgbuf *mb;
	char buf[ERRMAX];

	chan = aux;
	for(;;){
		mb = fs_recv(chan->reply, 0);
		if(mb == nil)
			continue;
		if(mb->data == nil){
			if(!(mb->flags & FREE))
				mbfree(mb);
			continue;
		}
		srv = chan->pdata;
		while(write(srv->fd, mb->data, mb->count) != mb->count){
			rerrstr(buf, sizeof(buf));
			if(strstr(buf, "interrupt"))
				continue;
			chanhangup(chan, buf);
			break;
		}
		mbfree(mb);
		srvput(srv);
	}
}

static void
srvi(void *aux)
{
	Chan *chan;
	Srv *srv;
	Msgbuf *mb, *ms;
	uchar *b, *p, *e;
	int n, m;
	char err[ERRMAX];

	err[0] = 0;
	chan = aux;
	srv = chan->pdata;

	if((mb = mballoc(IOHDRSZ+MAXDAT, chan, Mbeth1)) == nil)
		panic("srvi: mballoc failed");
	b = mb->data;
	p = b;
	e = b + mb->count;

Read:
	while((n = read(srv->fd, p, e - p)) > 0){
		p += n;
		while((p - b) >= BIT32SZ){
			m = GBIT32(b);
			if((m < BIT32SZ) || (m > mb->count)){
				strcpy(err, "bad length in 9P2000 message header");
				goto Hangup;
			}
			if((n = (p - b) - m) < 0){
				e = b + m;
				goto Read;
			}
			if(m <= SMALLBUF){
				if((ms = mballoc(m, chan, Mbeth1)) == nil)
					panic("srvi: mballoc failed");
				memmove(ms->data, b, m);
			} else {
				ms = mb;
				if((mb = mballoc(mb->count, chan, Mbeth1)) == nil)
					panic("srvi: mballoc failed");
				ms->count = m;
			}
			if(n > 0)
				memmove(mb->data, b + m, n);
			b = mb->data;
			p = b + n;

			incref(srv);
			fs_send(chan->send, ms);
		}
		e = b + mb->count;
	}

	if(n < 0)
		errstr(err, sizeof(err));

Hangup:
	chanhangup(chan, err);
	srvput(srv);

	mbfree(mb);
}

Chan*
srvchan(int fd, char *name)
{
	char buf[64];
	Chan *chan;
	Srv *srv;

	lock(&freechans);
	chan = freechans.hd;
	if(chan == nil){
		unlock(&freechans);
		return nil;
	}
	srv = chan->pdata;
	freechans.hd = srv->chan;
	unlock(&freechans);

	chan->protocol = nil;
	chan->msize = 0;
	chan->whotime = 0;
	snprint(chan->whochan, sizeof(chan->whochan), "%s", name);
	chan->authok = 0;

	incref(srv);
	srv->chan = chan;
	srv->fd = fd;

	if(chan->reply == nil){
		chan->reply = newqueue(Nqueue, "srvoq");
		newproc(srvo, chan, "srvo");
	}

	if(chan->send == nil)
		chan->send = serveq;
	snprint(buf, sizeof(buf), "srvi %s", name);
	newproc(srvi, chan, buf);

	return chan;
}

void
srvinit(void)
{
	Chan *chan;
	Srv *srv;
	int i;

	if(freechans.hd != nil)
		return;
	for(i=0; i<Nchans; i++){
		chan = fs_chaninit(1, sizeof(Srv));
		srv = chan->pdata;
		srv->fd = -1;
		srv->chan = freechans.hd;
		freechans.hd = chan;
	}
}
