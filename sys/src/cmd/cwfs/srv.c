#include "all.h"
#include "io.h"
#include <fcall.h>		/* 9p2000 */
#include <thread.h>

enum {
	Maxfdata	= 8192,
	Nqueue	= 200,		/* queue size (tunable) */
	Nsrvo	= 8,			/* number of write workers */
};

typedef struct Srv Srv;
struct Srv
{
	Ref;
	char		*name;
	Chan	*chan;
	int		fd;
	char		buf[64];
};

static struct {
	Lock;
	Chan *hd;
} freechans;

static Queue *srvoq;

void
chanhangup(Chan *chan, char *msg, int dolock)
{
	Srv *srv;

	USED(dolock);
	USED(msg);

	fileinit(chan);
	if(chan->type != Devsrv)
		return;
	srv = chan->pdata;
	if(srv == nil || srv->chan != chan)
		return;
	close(srv->fd);
	srv->fd = -1;
}

static void
srvput(Srv *srv)
{
	Chan *chan;

	if(decref(srv))
		return;

	if(chatty)
		print("%s closed\n", srv->name);

	chanhangup(srv->chan, "", 0);
	memset(srv->buf, 0, sizeof(srv->buf));
	chan = srv->chan;
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
		while((srv->fd >= 0) && (write(srv->fd, mb->data, mb->count) != mb->count)){
			rerrstr(buf, sizeof(buf));
			if(strstr(buf, "interrupt"))
				continue;

			if(buf[0] && chatty)
				print("srvo %s: %s\n", srv->name, buf);
			chanhangup(srv->chan, buf, 0);
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
		panic("srvi %s: mballoc failed", srv->name);
	b = mb->data;
	p = b;
	e = b + mb->count;

Read:
	while((srv->fd >= 0) && ((n = read(srv->fd, p, e - p)) >= 0)){
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
					panic("srvi %s: mballoc failed", srv->name);
				memmove(ms->data, b, m);
			} else {
				ms = mb;
				if((mb = mballoc(mb->count, srv->chan, Mbeth1)) == nil)
					panic("srvi %s: mballoc failed", srv->name);
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

	if(buf[0] && chatty)
		print("srvi %s: %s\n", srv->name, buf);
	chanhangup(srv->chan, buf, 0);
	srvput(srv);

	mbfree(mb);
}

Chan*
srvchan(int fd, char *name)
{
	Chan *chan;
	Srv *srv;

	lock(&freechans);
	if(chan = freechans.hd){
		srv = chan->pdata;
		freechans.hd = srv->chan;
		unlock(&freechans);
	} else {
		unlock(&freechans);
		chan = fs_chaninit(Devsrv, 1, sizeof(*srv));
		srv = chan->pdata;
	}
	chan->reply = srvoq;
	if(chan->send == nil)
		chan->send = serveq;
	chan->protocol = nil;
	chan->msize = 0;
	chan->whotime = 0;

	incref(srv);
	srv->chan = chan;
	srv->fd = fd;
	snprint(srv->buf, sizeof(srv->buf), "srvi %s", name);
	srv->name = strchr(srv->buf, ' ')+1;
	newproc(srvi, srv, srv->buf);

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
