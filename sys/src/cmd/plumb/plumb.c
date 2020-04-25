#include <u.h>
#include <libc.h>
#include <plumb.h>

char *plumbfile = nil;
Plumbmsg m;

void
usage(void)
{
	fprint(2, "usage:  plumb [-p plumbfile] [-a 'attr=value ...'] [-s src] [-d dst] [-t type] [-w wdir] -i | data1\n");
	exits("usage");
}

void
gather(void)
{
	char buf[8192];
	int n;

	m.ndata = 0;
	m.data = nil;
	while((n = read(0, buf, sizeof buf)) > 0){
		m.data = realloc(m.data, m.ndata+n);
		if(m.data == nil){
			fprint(2, "plumb: alloc failed: %r\n");
			exits("alloc");
		}
		memmove(m.data+m.ndata, buf, n);
		m.ndata += n;
	}
	if(n < 0){
		fprint(2, "plumb: i/o error on input: %r\n");
		exits("read");
	}
}

int
matchmsg(Plumbmsg *m, Plumbmsg *pat)
{
	Plumbattr *a;
	char *v;

	if(pat->src && strcmp(m->src, pat->src) != 0)
		return 0;
	if(pat->dst && strcmp(m->dst, pat->dst) != 0)
		return 0;
	if(pat->wdir && strcmp(m->wdir, pat->wdir) != 0)
		return 0;
	if(pat->type && strcmp(m->type, pat->type) != 0)
		return 0;
	for(a = m->attr; a != nil; a = a->next){
		v = plumblookup(pat->attr, a->name);
		if(v != nil && strcmp(a->value, v) != 0)
			return 0;
	}
	return 1;
}

void
main(int argc, char *argv[])
{
	char buf[1024], *p, *readport;
	int fd, i, input;
	Plumbmsg *rmsg;
	Plumbattr *a;

	input = 0;
	readport = nil;
	m.src = nil;
	m.dst = nil;
	m.wdir = nil;
	m.type = "text";
	m.attr = nil;
	ARGBEGIN{
	case 'a':
		p = EARGF(usage());
		m.attr = plumbaddattr(m.attr, plumbunpackattr(p));
		break;
	case 'd':
		m.dst = EARGF(usage());
		break;
	case 'i':
		input++;
		break;
	case 't':
	case 'k':	/* for backwards compatibility */
		m.type = EARGF(usage());
		break;
	case 'p':
		plumbfile = EARGF(usage());
		break;
	case 's':
		m.src = EARGF(usage());
		break;
	case 'w':
		m.wdir = EARGF(usage());
		break;
	case 'r':
		readport = EARGF(usage());
		break;
	}ARGEND

	if((input && argc>0) || (!input && argc<1) && readport == nil)
		usage();
	if(readport != nil)
		fd = plumbopen(readport, OREAD);
	else if(plumbfile != nil)
		fd = open(plumbfile, OWRITE);
	else
		fd = plumbopen("send", OWRITE);
	if(fd < 0){
		fprint(2, "plumb: can't open plumb file: %r\n");
		exits("open");
	}
	if(readport != nil){
again:
		rmsg = plumbrecv(fd);
		if(rmsg == nil){
			fprint(2, "plumb: receive failed: %r\n");
			exits("recv");
		}
		print("got message, matching\n");
		if(!matchmsg(rmsg, &m))
			goto again;
		print("src %s\n", rmsg->src);
		print("dst %s\n", rmsg->dst);
		print("wdir %s\n", rmsg->wdir);
		print("type %s\n", rmsg->type);
		print("data %.*s\n", rmsg->ndata, rmsg->data);
		for(a = rmsg->attr; a; a = a->next)
			print("attr %s=%s\n", a->name, a->value);
		plumbfree(rmsg);
		exits(nil);
	}
	if(m.src == nil)
		m.src = "plumb";
	if(m.wdir == nil)
		m.wdir = getwd(buf, sizeof buf);
	if(input){
		gather();
		if(plumblookup(m.attr, "action") == nil)
			m.attr = plumbaddattr(m.attr, plumbunpackattr("action=showdata"));
		if(plumbsend(fd, &m) < 0){
			fprint(2, "plumb: can't send message: %r\n");
			exits("error");
		}
		exits(nil);
	}
	for(i=0; i<argc; i++){
		if(input == 0){
			m.data = argv[i];
			m.ndata = -1;
		}
		if(plumbsend(fd, &m) < 0){
			fprint(2, "plumb: can't send message: %r\n");
			exits("error");
		}
	}
	exits(nil);
}
