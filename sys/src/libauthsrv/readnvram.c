#include <u.h>
#include <libc.h>
#include <authsrv.h>
#include <libsec.h>

static long	finddosfile(int, char*);

static int
check(void *x, int len, uchar sum, char *msg)
{
	if(nvcsum(x, len) == sum)
		return 0;
	memset(x, 0, len);
	fprint(2, "%s\n", msg);
	return 1;
}

/*
 *  get key info out of nvram.  since there isn't room in the PC's nvram use
 *  a disk partition there.
 */
static struct {
	char *cputype;
	char *file;
	int off;
	int len;
} nvtab[] = {
	"sparc", "#r/nvram", 1024+850, sizeof(Nvrsafe),
	"pc", "#S/sdC0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdC0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdC1/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdC1/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdD0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdD0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdE0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdE0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sdF0/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sdF0/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sd00/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sd00/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sd01/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sd01/9fat", -1, sizeof(Nvrsafe),
	"pc", "#S/sd10/nvram", 0, sizeof(Nvrsafe),
	"pc", "#S/sd10/9fat", -1, sizeof(Nvrsafe),
	"pc", "#f/fd0disk", -1, 512,	/* 512: #f requires whole sector reads */
	"pc", "#f/fd1disk", -1, 512,
	"mips", "#r/nvram", 1024+900, sizeof(Nvrsafe),
	"power", "#F/flash/flash0", 0x440000, sizeof(Nvrsafe),
	"power", "#F/flash/flash", 0x440000, sizeof(Nvrsafe),
	"power", "#r/nvram", 4352, sizeof(Nvrsafe),	/* OK for MTX-604e */
	"power", "/nvram", 0, sizeof(Nvrsafe),	/* OK for Ucu */
	"arm", "#F/flash/flash0", 0x100000, sizeof(Nvrsafe),
	"arm", "#F/flash/flash", 0x100000, sizeof(Nvrsafe),
	"debug", "/tmp/nvram", 0, sizeof(Nvrsafe),
};

typedef struct {
	int	fd;
	int	safelen;
	vlong	safeoff;
} Nvrwhere;

static char *nvrfile = nil, *cputype = nil;

/* returns with *locp filled in and locp->fd open, if possible */
static void
findnvram(Nvrwhere *locp)
{
	char *nvrlen, *nvroff, *v[2];
	int fd, i, safelen;
	vlong safeoff;

	if (nvrfile == nil)
		nvrfile = getenv("nvram");
	if (cputype == nil)
		cputype = getenv("cputype");
	if(cputype == nil)
		cputype = strdup("mips");
	if(strcmp(cputype, "386")==0 || strcmp(cputype, "amd64")==0 || strcmp(cputype, "alpha")==0) {
		free(cputype);
		cputype = strdup("pc");
	}

	fd = -1;
	safeoff = -1;
	safelen = -1;
	if(nvrfile != nil && *nvrfile != '\0'){
		/* accept device and device!file */
		i = gettokens(nvrfile, v, nelem(v), "!");
		if (i < 1) {
			i = 1;
			v[0] = "";
			v[1] = nil;
		}
		fd = open(v[0], ORDWR|OCEXEC);
		if (fd < 0)
			fd = open(v[0], OREAD|OCEXEC);
		safelen = sizeof(Nvrsafe);
		if(strstr(v[0], "/9fat") == nil)
			safeoff = 0;
		nvrlen = getenv("nvrlen");
		if(nvrlen != nil)
			safelen = strtol(nvrlen, 0, 0);
		nvroff = getenv("nvroff");
		if(nvroff != nil)
			if(strcmp(nvroff, "dos") == 0)
				safeoff = -1;
			else
				safeoff = strtoll(nvroff, 0, 0);
		if(safeoff < 0 && fd >= 0){
			safelen = 512;
			safeoff = finddosfile(fd, i == 2? v[1]: "plan9.nvr");
			if(safeoff < 0){	/* didn't find plan9.nvr? */
				close(fd);
				fd = -1;
			}
		}
		free(nvroff);
		free(nvrlen);
	}else
		for(i=0; i<nelem(nvtab); i++){
			if(strcmp(cputype, nvtab[i].cputype) != 0)
				continue;
			if((fd = open(nvtab[i].file, ORDWR|OCEXEC)) < 0)
				continue;
			safeoff = nvtab[i].off;
			safelen = nvtab[i].len;
			if(safeoff == -1){
				safeoff = finddosfile(fd, "plan9.nvr");
				if(safeoff < 0){  /* didn't find plan9.nvr? */
					close(fd);
					fd = -1;
					continue;
				}
			}
			break;
		}
	locp->fd = fd;
	locp->safelen = safelen;
	locp->safeoff = safeoff;
}

static int
ask(char *prompt, char *buf, int len, int raw)
{
	char *s;
	int n;

	memset(buf, 0, len);
	for(;;){
		if((s = readcons(prompt, nil, raw)) == nil)
			return -1;
		if((n = strlen(s)) >= len)
			fprint(2, "%s longer than %d characters; try again\n", prompt, len-1);
		else {
			memmove(buf, s, n);
			memset(s, 0, n);
			free(s);
			return 0;
		}
		memset(s, 0, n);
		free(s);
	}
}

/*
 *  get key info out of nvram.  since there isn't room in the PC's nvram use
 *  a disk partition there.
 */
int
readnvram(Nvrsafe *safep, int flag)
{
	int err;
	char buf[512];		/* 512 for floppy i/o */
	char *dodes;
	Nvrsafe *safe;
	Nvrwhere loc;

	err = 0;
	safe = (Nvrsafe*)buf;
	memset(&loc, 0, sizeof loc);
	findnvram(&loc);
	if (loc.safelen < 0)
		loc.safelen = sizeof *safe;
	else if (loc.safelen > sizeof buf)
		loc.safelen = sizeof buf;
	if (loc.safeoff < 0) {
		fprint(2, "readnvram: couldn't find nvram\n");
		if(!(flag&NVwritemem))
			memset(safep, 0, sizeof(*safep));
		safe = safep;
		/*
		 * allow user to type the data for authentication,
		 * even if there's no nvram to store it in.
		 */
	}

	if(flag&NVwritemem)
		safe = safep;
	else {
		memset(safep, 0, sizeof(*safep));
		if(loc.fd < 0
		|| seek(loc.fd, loc.safeoff, 0) < 0
		|| read(loc.fd, buf, loc.safelen) != loc.safelen){
			err = 1;
			if(flag&(NVwrite|NVwriteonerr))
				if(loc.fd < 0)
					fprint(2, "can't open %s: %r\n", nvrfile);
				else if (seek(loc.fd, loc.safeoff, 0) < 0)
					fprint(2, "can't seek %s to %lld: %r\n",
						nvrfile, loc.safeoff);
				else
					fprint(2, "can't read %d bytes from %s: %r\n",
						loc.safelen, nvrfile);
			/* start from scratch */
			memset(safep, 0, sizeof(*safep));
			safe = safep;
		}else{
			*safep = *safe;	/* overwrite arg with data read */
			safe = safep;

			/* verify data read */
			err |= check(safe->machkey, DESKEYLEN, safe->machsum,
						"bad nvram des key");
			err |= check(safe->authid, ANAMELEN, safe->authidsum,
						"bad authentication id");
			err |= check(safe->authdom, DOMLEN, safe->authdomsum,
						"bad authentication domain");
			if(0){
				err |= check(safe->config, CONFIGLEN, safe->configsum,
						"bad secstore key");
				err |= check(safe->aesmachkey, AESKEYLEN, safe->aesmachsum,
						"bad nvram aes key");
			} else {
				if(nvcsum(safe->config, CONFIGLEN) != safe->configsum)
					memset(safe->config, 0, CONFIGLEN);
				if(nvcsum(safe->aesmachkey, AESKEYLEN) != safe->aesmachsum)
					memset(safe->aesmachkey, 0, AESKEYLEN);
			}
			if(err == 0)
				if(safe->authid[0]==0 || safe->authdom[0]==0){
					fprint(2, "empty nvram authid or authdom\n");
					err = 1;
				}
		}
	}

	if((flag&(NVwrite|NVwritemem)) || (err && (flag&NVwriteonerr))){
		if (!(flag&NVwritemem)) {
			char pass[PASSWDLEN];
			char pass2[PASSWDLEN];
			Authkey k;

			if(ask("authid", safe->authid, sizeof safe->authid, 0))
				goto Out;
			if(ask("authdom", safe->authdom, sizeof safe->authdom, 0))
				goto Out;
			if(ask("secstore key", safe->config, sizeof safe->config, 1))
				goto Out;
Again:
			if(ask("password", pass, sizeof pass, 1))
				goto Out;
			if(ask("confirm password", pass2, sizeof pass2, 1))
				goto Out;
			if(tsmemcmp(pass, pass2, sizeof pass) != 0){
				fprint(2, "password mismatch\n");
				goto Again;
			}
			if((dodes = readcons("enable legacy p9sk1", "no", 0)) == nil)
				goto Out;
			passtokey(&k, pass);
			memset(pass, 0, sizeof pass);
			memset(pass2, 0, sizeof pass2);
			if(dodes[0] == 'y' || dodes[0] == 'Y')
				memmove(safe->machkey, k.des, DESKEYLEN);
			else
				memset(safe->machkey, 0, DESKEYLEN);
			memmove(safe->aesmachkey, k.aes, AESKEYLEN);
			memset(&k, 0, sizeof k);
			memset(dodes, 0, strlen(dodes));
			free(dodes);
		}

		safe->machsum = nvcsum(safe->machkey, DESKEYLEN);
		// safe->authsum = nvcsum(safe->authkey, DESKEYLEN);
		safe->configsum = nvcsum(safe->config, CONFIGLEN);
		safe->authidsum = nvcsum(safe->authid, sizeof safe->authid);
		safe->authdomsum = nvcsum(safe->authdom, sizeof safe->authdom);
		safe->aesmachsum = nvcsum(safe->aesmachkey, AESKEYLEN);

		*(Nvrsafe*)buf = *safe;
		if(loc.fd < 0
		|| seek(loc.fd, loc.safeoff, 0) < 0
		|| write(loc.fd, buf, loc.safelen) != loc.safelen){
			fprint(2, "can't write key to nvram: %r\n");
			err = 1;
		}else
			err = 0;
	}
Out:
	if (loc.fd >= 0)
		close(loc.fd);
	return err? -1: 0;
}

typedef struct Dosboot	Dosboot;
struct Dosboot{
	uchar	magic[3];	/* really an xx86 JMP instruction */
	uchar	version[8];
	uchar	sectsize[2];
	uchar	clustsize;
	uchar	nresrv[2];
	uchar	nfats;
	uchar	rootsize[2];
	uchar	volsize[2];
	uchar	mediadesc;
	uchar	fatsize[2];
	uchar	trksize[2];
	uchar	nheads[2];
	uchar	nhidden[4];
	uchar	bigvolsize[4];
	uchar	driveno;
	uchar	reserved0;
	uchar	bootsig;
	uchar	volid[4];
	uchar	label[11];
	uchar	type[8];
};
#define	GETSHORT(p) (((p)[1]<<8) | (p)[0])
#define	GETLONG(p) ((GETSHORT((p)+2) << 16) | GETSHORT((p)))

typedef struct Dosdir	Dosdir;
struct Dosdir
{
	char	name[8];
	char	ext[3];
	uchar	attr;
	uchar	reserved[10];
	uchar	time[2];
	uchar	date[2];
	uchar	start[2];
	uchar	length[4];
};

static char*
dosparse(char *from, char *to, int len)
{
	char c;

	memset(to, ' ', len);
	if(from == 0)
		return 0;
	while(len-- > 0){
		c = *from++;
		if(c == '.')
			return from;
		if(c == 0)
			break;
		if(c >= 'a' && c <= 'z')
			*to++ = c + 'A' - 'a';
		else
			*to++ = c;
	}
	return 0;
}

/*
 *  return offset of first file block
 *
 *  This is a very simplistic dos file system.  It only
 *  works on floppies, only looks in the root, and only
 *  returns a pointer to the first block of a file.
 *
 *  This exists for cpu servers that have no hard disk
 *  or nvram to store the key on.
 *
 *  Please don't make this any smarter: it stays resident
 *  and I'ld prefer not to waste the space on something that
 *  runs only at boottime -- presotto.
 */
static long
finddosfile(int fd, char *file)
{
	uchar secbuf[512];
	char name[8];
	char ext[3];
	Dosboot	*b;
	Dosdir *root, *dp;
	int nroot, sectsize, rootoff, rootsects, n;

	/* dos'ize file name */
	file = dosparse(file, name, 8);
	dosparse(file, ext, 3);

	/* read boot block, check for sanity */
	b = (Dosboot*)secbuf;
	if(read(fd, secbuf, sizeof(secbuf)) != sizeof(secbuf))
		return -1;
	if(b->magic[0] != 0xEB || b->magic[1] != 0x3C || b->magic[2] != 0x90)
		return -1;
	sectsize = GETSHORT(b->sectsize);
	if(sectsize != 512)
		return -1;
	rootoff = (GETSHORT(b->nresrv) + b->nfats*GETSHORT(b->fatsize)) * sectsize;
	if(seek(fd, rootoff, 0) < 0)
		return -1;
	nroot = GETSHORT(b->rootsize);
	rootsects = (nroot*sizeof(Dosdir)+sectsize-1)/sectsize;
	if(rootsects <= 0 || rootsects > 64)
		return -1;

	/*
	 *  read root. it is contiguous to make stuff like
	 *  this easier
	 */
	root = malloc(rootsects*sectsize);
	if(read(fd, root, rootsects*sectsize) != rootsects*sectsize)
		return -1;
	n = -1;
	for(dp = root; dp < &root[nroot]; dp++)
		if(memcmp(name, dp->name, 8) == 0 && memcmp(ext, dp->ext, 3) == 0){
			n = GETSHORT(dp->start);
			break;
		}
	free(root);

	if(n < 0)
		return -1;

	/*
	 *  dp->start is in cluster units, not sectors.  The first
	 *  cluster is cluster 2 which starts immediately after the
	 *  root directory
	 */
	return rootoff + rootsects*sectsize + (n-2)*sectsize*b->clustsize;
}

