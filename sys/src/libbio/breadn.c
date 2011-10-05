#include <u.h>
#include <libc.h>
#include <bio.h>

long
Breadn(Biobufhdr *bp, void *data, long len)
{
	char *e, *p;
	int n;

	p = data;
	e = p + len;
	if(e < p){
		Berror(bp, "invalid read length");
		return -1;
	}
	while(p < e){
		if((n = Bread(bp, p, e - p)) <= 0){
			if(n < 0 && p == data)
				return -1;
			break;
		}
		p += n;
	}
	return p - (char*)data;
}
