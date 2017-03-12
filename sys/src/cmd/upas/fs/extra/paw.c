#include<u.h>
#include<libc.h>
#include<bio.h>

void
main(void)
{
	char *f[10], *s;
	vlong sum;
	Biobuf b;

	sum = 0;
	Binit(&b, 0, OREAD);

	while(s = Brdstr(&b, '\n', 1)){
		if(getfields(s, f, nelem(f), 1, " ") > 2)
			sum += strtoul(f[2], 0, 0);
		free(s);
	}
	Bterm(&b);
	print("%lld\n", sum);
	exits("");
}
