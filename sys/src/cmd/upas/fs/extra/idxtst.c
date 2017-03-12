#include <u.h>
#include <libc.h>
#include <bio.h>

static char *etab[] = {
	"not found",
	"does not exist",
	"file is locked",
	"exclusive lock",
};

static int
bad(int idx)
{
	char buf[ERRMAX];
	int i;

	rerrstr(buf, sizeof buf);
	for(i = idx; i < nelem(etab); i++)
		if(strstr(buf, etab[i]))
			return 0;
	return 1;
}

static int
exopen(char *s)
{
	int i, fd;

	for(i = 0; i < 30; i++){
		if((fd = open(s, OWRITE|OTRUNC)) >= 0 || bad(0))
			return fd;
		if((fd = create(s, OWRITE|OEXCL, DMEXCL|0600)) >= 0  || bad(2))
			return fd;
		sleep(1000);
	}
	werrstr("lock timeout");
	return -1;
}

void
main(void)
{
	int fd;
	Biobuf *b;

	fd = exopen("testingex");
	if(fd == -1)
		sysfatal("exopen: %r");
	b = Bopen("testingex", OREAD);
	if(b){
		free(b);
		fprint(2, "print both opened at once\n");
	}else
		fprint(2, "bopen: %r\n");
	close(fd);
	exits("");
}
