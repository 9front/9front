#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <ip.h>

char*
v4parseip(uchar *to, char *from)
{
	int i;
	char *p;

	p = from;
	for(i = 0; i < 4 && *p; i++){
		to[i] = strtoul(p, &p, 0);
		if(*p == '.')
			p++;
	}
	switch(CLASS(to)){
	case 0:	/* class A - 1 uchar net */
	case 1:
		if(i == 3){
			to[3] = to[2];
			to[2] = to[1];
			to[1] = 0;
		} else if (i == 2){
			to[3] = to[1];
			to[1] = 0;
		}
		break;
	case 2:	/* class B - 2 uchar net */
		if(i == 3){
			to[3] = to[2];
			to[2] = 0;
		}
		break;
	}
	return p;
}

static int
ipcharok(int c)
{
	return c == '.' || c == ':' || (isascii(c) && isxdigit(c));
}

static int
delimchar(int c)
{
	if(c == '\0')
		return 1;
	if(c == '.' || c == ':' || (isascii(c) && isalnum(c)))
		return 0;
	return 1;
}

/*
 * `from' may contain an address followed by other characters,
 * at least in /boot, so we permit whitespace (and more) after the address.
 * we do ensure that "delete" cannot be parsed as "de::".
 *
 * some callers don't check the return value for errors, so
 * set `to' to something distinctive in the case of a parse error.
 */
vlong
parseip(uchar *to, char *from)
{
	int i, elipsis = 0, v4 = 1;
	ulong x;
	char *p, *op;

	memset(to, 0, IPaddrlen);
	p = from;
	for(i = 0; i < IPaddrlen && ipcharok(*p); i+=2){
		op = p;
		x = strtoul(p, &p, 16);
		if(*p == '.' || (*p == 0 && i == 0)){	/* ends with v4? */
			if(i > IPaddrlen-4){
				memset(to, 0, IPaddrlen);
				return -1;		/* parse error */
			}
			p = v4parseip(to+i, op);
			i += 4;
			break;
		}
		/* v6: at most 4 hex digits, followed by colon or delim */
		if(x != (ushort)x || (*p != ':' && !delimchar(*p))) {
			memset(to, 0, IPaddrlen);
			return -1;			/* parse error */
		}
		to[i] = x>>8;
		to[i+1] = x;
		if(*p == ':'){
			v4 = 0;
			if(*++p == ':'){	/* :: is elided zero short(s) */
				if (elipsis) {
					memset(to, 0, IPaddrlen);
					return -1;	/* second :: */
				}
				elipsis = i+2;
				p++;
			}
		} else if (p == op)		/* strtoul made no progress? */
			break;
	}
	if (p == from || !delimchar(*p)) {
		memset(to, 0, IPaddrlen);
		return -1;				/* parse error */
	}
	if(i < IPaddrlen){
		memmove(&to[elipsis+IPaddrlen-i], &to[elipsis], i-elipsis);
		memset(&to[elipsis], 0, IPaddrlen-i);
	}
	if(v4){
		to[10] = to[11] = 0xff;
		return (ulong)nhgetl(to + IPv4off);
	} else
		return 6;
}

/*
 *  hack to allow ip v4 masks to be entered in the old
 *  style
 */
vlong
parseipmask(uchar *to, char *from, int v4)
{
	vlong x;
	int i, w;
	uchar *p;

	if(*from == '/'){
		/* as a number of prefix bits */
		i = atoi(from+1);
		if(i < 0)
			i = 0;
		if(i <= 32 && v4)
			i += 96;
		if(i > 128)
			i = 128;
		w = i;
		memset(to, 0, IPaddrlen);
		for(p = to; i >= 8; i -= 8)
			*p++ = 0xff;
		if(i > 0)
			*p = ~((1<<(8-i))-1);
		/*
		 * identify as ipv6 if the mask is inexpressible as a v4 mask
		 * (because it has too few mask bits).  Arguably, we could
		 * always return 6 here.
		 */
		if (w < 96)
			return v4 ? -1 : 6;
		x = (ulong)nhgetl(to+IPv4off);
	} else {
		/* as a straight v4 bit mask */
		x = parseip(to, from);
		if(memcmp(to, v4prefix, IPv4off) == 0)
			memset(to, 0xff, IPv4off);
		else if(v4 && memcmp(to, IPallbits, IPv4off) != 0)
			x = -1;
	}
	return x;
}

vlong
parseipandmask(uchar *ip, uchar *mask, char *ipstr, char *maskstr)
{
	vlong x;

	x = parseip(ip, ipstr);
	if(maskstr == nil)
		memset(mask, 0xff, IPaddrlen);
	else if(parseipmask(mask, maskstr, memcmp(ip, v4prefix, IPv4off) == 0) == -1)
		x = -1;
	return x;
}
