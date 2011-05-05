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


int readheader(int fd, struct th* th) {
  int i;
  char b[512];
  if(readn(fd, b, 512) != 512) return -1;
  
  // Check for end of archive
  for(i=0; i<512; i++) {
	if(b[i]!=0) goto rhok;
  }
  if(readn(fd, b, 512) != 512) return -1;
  for(i=0; i<512; i++) {
	if(b[i]!=0) return -1;
  }
  return 0;

 rhok:
  th->name = sndup(b, 100);
  th->perm = strtoul(b+100, nil, 8);
  th->size = strtoul(b+124, nil, 8);
  th->type = b[156];
  th->user = sndup(b+265, 32);
  th->group= sndup(b+297, 32);
  return 1;
}

int main(void) {
  while(1) {
	struct th th;
	ulong off;
	uchar b[512];
	DigestState *s;
	int wfd;
	int r = readheader(0, &th);
	if(r <= 0) return r;

	switch(th.type) {
	case '5':
		create(th.name, OREAD, DMDIR|th.perm);
		break;
	case '0': case 0:
		print("A %s\n", th.name);
		r = access(th.name, 0);
		if(r == 0) {
			print("File already exists: %s\n", th.name);
			return -1;
		}
		if((wfd = create(th.name, OWRITE, th.perm)) < 0) {
			print("Create failed: %s\n", th.name);
			return -1;
		}
		s = nil;
		for(off=0; off<th.size; off+=512) {
			int n = th.size-off;
			n = n<512 ? n : 512;
			if(readn(0, b, 512) != 512) return -1;
			if(write(wfd, b, n) != n) return -1;
			s = sha1(b, n, nil, s);
		}

		uchar digest[20], hdigest[41];
		sha1(nil, 0, digest, s);
		enc16((char*)hdigest, 41, digest, 20);
		fprint(2, "%s\t%s\n", th.name, hdigest);
		close(wfd);
		break;
	default:
		print("Unknown file type '%c'\n", th.type);
		return -1;
	}

	free(th.name);
	free(th.user);
	free(th.group);
  }
}
