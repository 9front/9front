#include <u.h>
#include <libc.h>
#include <keyboard.h>
#include <plumb.h>

static int lightstep = 5, volstep = 3;
static int light, vol, actl, mod;

static void
aplumb(char *s)
{
	int f;

	if((f = plumbopen("send", OWRITE)) >= 0){
		if(plumbsendtext(f, "shortcuts", "audio", "/", s) < 0)
			fprint(2, "aplumb: %r\n");
		close(f);
	}else{
		fprint(2, "aplumb: %r\n");
	}
}

static void
process(char *s)
{
	char b[128], *p;
	int n, o, skip;
	Rune r;

	o = 0;
	b[o++] = *s;
	if(*s == 'k' || *s == 'K')
		mod = utfrune(s+1, Kmod4) != nil;

	for(p = s+1; *p != 0; p += n){
		if((n = chartorune(&r, p)) == 1 && r == Runeerror){
			/* bail out */
			n = strlen(p);
			memmove(b+o, p, n);
			o += n;
			p += n;
			break;
		}

		skip = 0;
		if(*s == 'c'){
			if(mod){
				if(skip |= (r == (KF|1)))
					fprint(light, "lcd %+d", -lightstep);
				else if(skip |= (r == (KF|2)))
					fprint(light, "lcd %+d", lightstep);
			}else{
				if(skip |= (r == Kvoldn))
					fprint(vol, "master %+d", -volstep);
				else if(skip |= (r == Kvolup))
					fprint(vol, "master %+d", volstep);
				else if(skip |= (r == Kmute))
					fprint(actl, "master toggle");
				else if(skip |= (r == Ksbwd))
					aplumb("key <");
				else if(skip |= (r == Ksfwd))
					aplumb("key >");
				else if(skip |= (r == Kpause))
					aplumb("key p");
			}
		}

		if(!skip){
			memmove(b+o, p, n);
			o += n;
		}
	}

	/* all runes filtered out - ignore completely */
	if(o == 1 && p-s > 1)
		return;

	b[o++] = 0;
	if(write(1, b, o) != o)
		exits(nil);
}

static void
usage(void)
{
	fprint(2, "usage: [-l light_step] [-v vol_step] %s\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	char b[128];
	int i, j, n;

	ARGBEGIN{
	case 'l':
		lightstep = atoi(EARGF(usage()));
		break;
	case 'v':
		volstep = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND

	light = open("/dev/light", OWRITE);
	vol = open("/dev/volume", OWRITE);
	actl = open("/dev/audioctl", OWRITE);
	for(i = 0;;){
		if((n = read(0, b+i, sizeof(b)-i)) <= 0)
			break;
		n += i;
		for(j = 0; j < n; j++){
			if(b[j] == 0){
				process(b+i);
				i = j+1;
			}
		}
		memmove(b, b+i, j-i);
		i -= j;
	}

	exits(nil);
}
