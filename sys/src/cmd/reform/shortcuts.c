#include <u.h>
#include <libc.h>
#include <keyboard.h>

static int lightstep = 5, volstep = 3;
static int light, vol, mod;

static void
process(char *s)
{
	char b[128], *p;
	int n, o;
	Rune r;

	if(*s == 'K' && s[1] == 0)
		mod = 0;

	o = 0;
	b[o++] = *s;
	for(p = s+1; *p != 0; p += n){
		if((n = chartorune(&r, p)) == 1 && r == Runeerror){
			/* bail out */
			n = strlen(p);
			memmove(b+o, p, n);
			o += n;
			p += n;
			break;
		}

		if(*s == 'k' && r == Kmod4){
			mod = 1;
		}else if(*s == 'K'){
			if(mod && r >= (KF|1) && r <= (KF|4))
				continue;
			if(r == Kmod4)
				mod = 0;
		}else if(mod && r >= (KF|1) && r <= (KF|4)){
			if(*s == 'c'){
				if(r == (KF|1))
					fprint(light, "lcd %+d", -lightstep);
				else if(r == (KF|2))
					fprint(light, "lcd %+d", lightstep);
				else if(r == (KF|3))
					fprint(vol, "master %+d", -volstep);
				else if(r == (KF|4))
					fprint(vol, "master %+d", volstep);
			}
			continue;
			}

		memmove(b+o, p, n);
		o += n;
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
