#include "common.h"
#include "dat.h"

enum {
	Bounces,
	Owner,
	List,
	Nboxes,
};

char *suffix[Nboxes] = {
[Bounces]	"-bounces",
[Owner]		"-owner",
[List]		"",
};

int
createpipeto(char *alfile, char *user, char *listname, char *dom, int which)
{
	char buf[Pathlen], rflag[64];
	int fd;
	Dir *d;

	mboxpathbuf(buf, sizeof buf, user, "pipeto");

	fprint(2, "creating new pipeto: %s\n", buf);
	fd = create(buf, OWRITE, 0775);
	if(fd < 0)
		return -1;
	d = dirfstat(fd);
	if(d == nil){
		fprint(fd, "Couldn't stat %s: %r\n", buf);
		return -1;
	}
	d->mode |= 0775;
	if(dirfwstat(fd, d) < 0)
		fprint(fd, "Couldn't wstat %s: %r\n", buf);
	free(d);

	if(dom != nil)
		snprint(rflag, sizeof rflag, "-r%s@%s ", listname, dom);
	else
		rflag[0] = 0;

	fprint(fd, "#!/bin/rc\n");
	switch(which){
	case Owner:
		fprint(fd, "/bin/upas/mlowner %s %s\n", alfile, listname);
		break;
	case List:
		fprint(fd, "/bin/upas/ml %s%s %s\n", rflag, alfile, user);
		break;
	case Bounces:
		fprint(fd, "exit ''\n");
		break;
	}
	close(fd);

	return 0;
}

void
usage(void)
{
	fprint(2, "usage:\t%s -c listname\n", argv0);
	fprint(2, "\t%s -[ar] listname addr\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *listname, *dom, *addr, alfile[Pathlen], buf[64], flag[127];
	int i;


	rfork(RFENVG|RFREND);

	memset(flag, 0, sizeof flag);
	ARGBEGIN{
	case 'c':
	case 'r':
	case 'a':
		flag[ARGC()] = 1;
		break;
	default:
		usage();
	}ARGEND;

	if(flag['a'] + flag['r'] + flag['c'] > 1){
		fprint(2, "%s: -a, -r, and -c are mutually exclusive\n", argv0);
		exits("usage");
	}

	if(argc < 1)
		usage();

	listname = argv[0];
	if((dom = strchr(listname, '@')) != nil)
		*dom++ = 0;
	mboxpathbuf(alfile, sizeof alfile, listname, "address-list");

	if(flag['c']){
		for(i = 0; i < Nboxes; i++){
			snprint(buf, sizeof buf, "%s%s", listname, suffix[i]);
			if(creatembox(buf, nil) < 0)
				sysfatal("creating %s's mbox: %r", buf);
			if(createpipeto(alfile, buf, listname, dom, i) < 0)
				sysfatal("creating %s's pipeto: %r", buf);
		}
		writeaddr(alfile, "# mlmgr c flag", 0, listname);
	} else if(flag['r']){
		if(argc != 2)
			usage();
		addr = argv[1];
		writeaddr(alfile, "# mlmgr r flag", 0, listname);
		writeaddr(alfile, addr, 1, listname);
	} else if(flag['a']){
		if(argc != 2)
			usage();
		addr = argv[1];
		writeaddr(alfile, "# mlmgr a flag", 0, listname);
		writeaddr(alfile, addr, 0, listname);
	}
	exits(0);
}
