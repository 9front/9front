/*
 * simulate the read patterns of external programs for testing
 * info file. "infotest 511 512" simulates what ned does today.
 *
 * here's how the new info scheme was verified:
 *
	ramfs
	s=/sys/src/cmd/upas
	unmount /mail/fs
	$s/fs/8.out -p
	for(f in /mail/fs/mbox/*/info){
		for(i in `{seq 1 1026})
			$s/fs/infotst $i `{echo $i + 1 | hoc} > /tmp/$i < $f
		for(i in /tmp/*)
			cmp $i /tmp/1
		rm /tmp/*
	}

	# now test for differences with old scheme under
	# ideal reading conditions
	for(f in /mail/fs/mbox/*/info){
		i = `{echo $f | sed 's:/mail/fs/mbox/([^/]+)/info:\1:g'}
		$s/fs/infotst 2048 > /tmp/$i < $f
	}
	unmount /mail/fs
	upas/fs -p
	for(f in /mail/fs/mbox/*/info){
		i = `{echo $f | sed 's:/mail/fs/mbox/([^/]+)/info:\1:g'}
		$s/fs/infotst 2048 > /tmp/$i.o < $f
	}
	for(i in /tmp/*.o)
		cmp $i `{echo $i | sed 's:\.o$::g'}
	rm /tmp/*
 */
#include <u.h>
#include <libc.h>

enum{
	Ntab	= 100,
};

int	tab[Ntab];
int	ntab;
int	largest;

void
usage(void)
{
	fprint(2, "usage: infotest n1 n2 ... nm\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *buf;
	int i, n;

	ARGBEGIN{
	default:
		usage();
	}ARGEND
	if(argc == 0)
		usage();
	for(; *argv; argv++){
		if(ntab == nelem(tab))
			break;
		i = atoi(*argv);
		if(i > largest)
			largest = i;
		tab[ntab++] = i;
	}
	buf = malloc(largest);
	if(!buf)
		sysfatal("malloc: %r");
	for(i = 0;; ){
		switch(n = read(0, buf, tab[i])){
		case -1:
			sysfatal("read: %r");
		case 0:
			exits("");
		default:
			write(1, buf, n);
			break;
		}
		if(i < ntab-1)
			i++;
	}
}
