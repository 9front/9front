#include "all.h"
#include "io.h"
#include <fcall.h>		/* 9p2000 */
#include <thread.h>

enum {
	Maxfdata	= 8192,
	Nqueue		= 200,		/* queue size (tunable) */
	Nsrvo		= 8,		/* number of write workers */
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

static Queue *srvoq;

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
srvo(void *)
{
	Srv *srv;
	Msgbuf *mb;
	char buf[ERRMAX];

	for(;;){
		mb = fs_recv(srvoq, 0);
		if(mb == nil)
			continue;
		if(mb->data == nil){
			if(!(mb->flags & FREE))
				mbfree(mb);
			continue;
		}
		srv = (Srv*)mb->param;
		while(write(srv->fd, mb->data, mb->count) != mb->count){
			rerrstr(buf, sizeof(buf));
			if(strstr(buf, "interrupt"))
				continue;
			chanhangup(srv->chan, buf);
			break;
		}
		mbfree(mb);
		srvput(srv);
	}
}

static void
srvi(void *aux)
{
	Srv *srv = aux;
	Msgbuf *mb, *ms;
	uchar *b, *p, *e;
	int n, m;
	char buf[ERRMAX];

	if((mb = mballoc(IOHDRSZ+Maxfdata, srv->chan, Mbeth1)) == nil)
		panic("srvi: mballoc failed");
	b = mb->data;
	p = b;
	e = b + mb->count;

Read:
	while((n = read(srv->fd, p, e - p)) >= 0){
		p += n;
		while((p - b) >= BIT32SZ){
			m = GBIT32(b);
			if((m < BIT32SZ) || (m > mb->count)){
				werrstr("bad length in 9P2000 message header");
				goto Error;
			}
			if((n = (p - b) - m) < 0){
				e = b + m;
				goto Read;
			}
			if(m <= SMALLBUF){
				if((ms = mballoc(m, srv->chan, Mbeth1)) == nil)
					panic("srvi: mballoc failed");
				memmove(ms->data, b, m);
			} else {
				ms = mb;
				if((mb = mballoc(mb->count, srv->chan, Mbeth1)) == nil)
					panic("srvi: mballoc failed");
				ms->count = m;
			}
			if(n > 0)
				memmove(mb->data, b + m, n);
			b = mb->data;
			p = b + n;

			incref(srv);
			ms->param = (uint)srv;
			fs_send(serveq, ms);
		}
		e = b + mb->count;
	}

Error:
	rerrstr(buf, sizeof(buf));
	if(strstr(buf, "interrupt"))
		goto Read;

	chanhangup(srv->chan, buf);
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
	if(chan = freechans.hd){
		srv = chan->pdata;
		freechans.hd = srv->chan;
		unlock(&freechans);
	} else {
		unlock(&freechans);
		chan = fs_chaninit(1, sizeof(*srv));
		srv = chan->pdata;
	}
	chan->reply = srvoq;
	if(chan->send == nil)
		chan->send = serveq;
	chan->protocol = nil;
	chan->msize = 0;
	chan->whotime = 0;
	snprint(chan->whochan, sizeof(chan->whochan), "%s", name);

	incref(srv);
	srv->chan = chan;
	srv->fd = fd;
	snprint(buf, sizeof(buf), "srvi %s", name);
	newproc(srvi, srv, buf);

	return chan;
}

void
srvinit(void)
{
	int i;

	if(srvoq != nil)
		return;

	srvoq = newqueue(Nqueue, "srvoq");
	for(i=0; i<Nsrvo; i++)
		newproc(srvo, nil, "srvo");
}
