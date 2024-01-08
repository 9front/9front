#include "vnc.h"
#include "vncv.h"
#include <libsec.h>

char*		charset = "utf-8";
char*		encodings = "copyrect hextile corre rre raw mousewarp desktopsize xdesktopsize";
int		autoscale;
int		bpp12;
int		shared;
int		verbose;
Vnc*		vnc;
int		mousefd;
int		tls;


static int	vncstart(Vnc*, int);

enum
{
	NProcs	= 4
};

static int pids[NProcs];
static char killkin[] = "die vnc kin";

/*
 * called by any proc when exiting to tear everything down.
 */
static void
shutdown(void)
{
	int i, pid;

	hangup(vnc->ctlfd);
	close(vnc->ctlfd);
	vnc->ctlfd = -1;
	close(vnc->datafd);
	vnc->datafd = -1;

	pid = getpid();
	for(i = 0; i < NProcs; i++)
		if(pids[i] != 0 && pids[i] != pid)
			postnote(PNPROC, pids[i], killkin);
}

char*
netmkvncaddr(char *server)
{
	char *p, portstr[NETPATHLEN];
	int port;

	/* leave execnet dial strings alone */
	if(strncmp(server, "exec!", 5) == 0)
		return server;

	port = 5900;
	if(tls)
		port = 35729;
	if((p = strchr(server, ']')) == nil)
		p = server;
	if((p = strchr(p, ':')) != nil) {
		*p++ = '\0';
		port += atoi(p);
	}
	snprint(portstr, sizeof portstr, "%d", port);
	return netmkaddr(server, "tcp", portstr);
}

void
vnchungup(Vnc*)
{
	sysfatal("connection closed");
}

void
usage(void)
{
	fprint(2, "usage: vncv [-acstv] [-e encodings] [-l charset] [-k keypattern] host[:n]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int p, dfd, cfd, shared;
	char *keypattern, *label;

	keypattern = nil;
	shared = 0;
	ARGBEGIN{
	case 'a':
		autoscale = 1;
		break;
	case 'c':
		bpp12 = 1;
		break;
	case 'e':
		encodings = EARGF(usage());
		break;
	case 's':
		shared = 1;
		break;
	case 't':
		tls = 1;
		break;
	case 'v':
		verbose++;
		break;
	case 'k':
		keypattern = EARGF(usage());
		break;
	case 'l':
		charset = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if(argc != 1)
		usage();

	dfd = dial(netmkvncaddr(argv[0]), nil, nil, &cfd);
	if(dfd < 0)
		sysfatal("cannot dial %s: %r", argv[0]);
	if(tls){
		TLSconn conn;

		memset(&conn, 0, sizeof(conn));
		if((dfd = tlsClient(dfd, &conn)) < 0)
			sysfatal("tlsClient: %r");
		/* XXX check thumbprint */
		free(conn.cert);
		free(conn.sessionID);
	}
	vnc = vncinit(dfd, cfd, nil);
	vnc->srvaddr = strdup(argv[0]);

	if(vnchandshake(vnc) < 0)
		sysfatal("handshake failure: %r");
	if(vncauth(vnc, keypattern) < 0)
		sysfatal("authentication failure: %r");
	if(vncstart(vnc, shared) < 0)
		sysfatal("init failure: %r");

	label = smprint("vnc %s", argv[0]);
	if(initdraw(0, 0, label) < 0)
		sysfatal("initdraw: %r");
	free(label);
	display->locking = 1;
	unlockdisplay(display);

	choosecolor(vnc);
	sendencodings(vnc);
	initmouse();

	rfork(RFREND);
	atexit(shutdown);
	pids[0] = getpid();

	switch(p = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	default:
		break;
	case 0:
		atexit(shutdown);
		readfromserver(vnc);
		exits(nil);
	}
	pids[1] = p;

	switch(p = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	default:
		break;
	case 0:
		atexit(shutdown);
		checksnarf(vnc);
		exits(nil);
	}
	pids[2] = p;

	switch(p = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	default:
		break;
	case 0:
		atexit(shutdown);
		readkbd(vnc);
		exits(nil);
	}
	pids[3] = p;

	readmouse(vnc);
	exits(nil);
}

static int
vncstart(Vnc *v, int shared)
{
	vncwrchar(v, shared);
	vncflush(v);
	v->dim = Rpt(ZP, vncrdpoint(v));
	v->Pixfmt = vncrdpixfmt(v);
	v->name = vncrdstring(v);
	return 0;
}
