#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

struct th {
  char *name;
  ulong perm;
  ulong size;
  char type;
  char *user, *group;
};

static char *sndup(char* s, ulong n) {
	char *d, *p;
	p = memchr(s, 0, n);
	if(p)
		n = p-s;
	d = malloc(n+1);
	memcpy(d,s,n);
	d[n] = 0;
	return d;
}


int readheader(struct th* th) {
  int i;
  char b[512];

  if(readn(0, b, 512) != 512) return -1;
  
  // Check for end of archive
  for(i=0; i<512; i++) {
	if(b[i]!=0) goto rhok;
  }
  if(readn(0, b, 512) != 512) return -1;
  for(i=0; i<512; i++) {
	if(b[i]!=0) return -1;
  }
  return 0;

 rhok:
  th->name = cleanname(sndup(b, 100));
  th->perm = strtoul(b+100, nil, 8);
  th->size = strtoul(b+124, nil, 8);
  th->type = b[156];
  th->user = sndup(b+265, 32);
  th->group= sndup(b+297, 32);
  return 1;
}

void main(int argc, char *argv[]) {
  ARGBEGIN {
  } ARGEND;
  for(;;) {
	struct th th;
	ulong off;
	uchar b[512];
	char err[ERRMAX];
	DigestState *s;
	int r, wfd;

	r = readheader(&th);
	if(r == 0)
		exits(nil);
	if(r < 0)
		sysfatal("unexpected eof");
			
	switch(th.type) {
	case '5':
		if((wfd = create(th.name, OREAD, DMDIR|th.perm)) >= 0)
			close(wfd);
		break;
	case '0': case 0:
		fprint(2, "A %s\n", th.name);
		if((wfd = create(th.name, OWRITE|OEXCL, th.perm)) < 0)
			sysfatal("%r", th.name);
		s = nil;
		for(off=0; off<th.size; off+=512) {
			if(readn(0, b, 512) == 512){
				if((r = th.size-off) > 512)
					r = 512;
				if(write(wfd, b, r) == r){
					s = sha1(b, r, nil, s);
					continue;
				}
			}
			errstr(err, sizeof(err));
			remove(th.name);
			errstr(err, sizeof(err));
			sysfatal("%s: %r", th.name);
		}

		uchar digest[20], hdigest[41];
		sha1(nil, 0, digest, s);
		enc16((char*)hdigest, 41, digest, 20);
		print("%s\t%s\n", th.name, hdigest);
		close(wfd);
		break;
	default:
		sysfatal("Unknown file type '%c'", th.type);
	}

	free(th.name);
	free(th.user);
	free(th.group);
  }
}
