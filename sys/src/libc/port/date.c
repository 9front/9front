#include <u.h>
#include <libc.h>

typedef struct Tzabbrev Tzabbrev;
typedef struct Tzoffpair Tzoffpair;

#define Ctimefmt	"WW MMM _D hh:mm:ss ZZZ YYYY"
#define P(pad, w)	((pad) < (w) ? 0 : pad - w)

enum {
	Tzsize		= 150,
	Nsec		= 1000*1000*1000,
	Usec		= 1000*1000,
	Msec		= 1000,
	Daysec		= (vlong)24*3600,
	Days400y	= 365*400 + 4*25 - 3,
	Days4y		= 365*4 + 1,
};

enum {
	Cend,
	Cspace,
	Cnum,
	Cletter,
	Cpunct,
};
	
struct Tzone {
	char	tzname[32];
	char	stname[16];
	char	dlname[16];
	long	stdiff;
	long	dldiff;
	long	dlpairs[150];
};

static QLock zlock;
static int nzones;
static Tzone **zones;
static int mdays[] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static char *wday[] = {
	"Sunday","Monday","Tuesday",
	"Wednesday","Thursday","Friday",
	"Saturday", nil,
};
static char *month[] = {
	"January", "February", "March",
	"April", "May", "June", "July",
	"August", "September", "October",
	"November", "December", nil
};

struct Tzabbrev {
	char *abbr;
	char *name;
};

struct Tzoffpair {
	char *abbr;
	int off;
};

#define isalpha(c)\
	(((c)|0x60) >= 'a' && ((c)|0x60) <= 'z')

/* Obsolete time zone names. Hardcoded to match RFC5322 */
static Tzabbrev tzabbrev[] = {
	{"UT", "GMT"}, {"GMT", "GMT"}, {"UTC", "GMT"},
	{"EST",	"US_Eastern"}, {"EDT", "US_Eastern"},
	{"CST", "US_Central"}, {"CDT", "US_Central"},
	{"MST", "US_Mountain"}, {"MDT", "US_Mountain"},
	{"PST", "US_Pacific"}, {"PDT", "US_Pacific"},
	{nil},
};

/* Military timezone names */
static Tzoffpair milabbrev[] = {
	{"A", -1*3600},   {"B", -2*3600},   {"C", -3*3600},
	{"D", -4*3600},   {"E", -5*3600},   {"F", -6*3600},
	{"G", -7*3600},   {"H", -8*3600},   {"I", -9*3600},
	{"K", -10*3600},  {"L", -11*3600},  {"M", -12*3600},
	{"N", +1*3600},   {"O", +2*3600},   {"P", +3*3600},
	{"Q", +4*3600},   {"R", +5*3600},   {"S", +6*3600},
	{"T", +7*3600},   {"U", +8*3600},   {"V", +9*3600},
	{"W", +10*3600},  {"X", +11*3600}, {"Y", +12*3600},
	{"Z",	0}, {nil, 0}
};

static vlong
mod(vlong a, vlong b)
{
	vlong r;

	r = a % b;
	if(r < 0)
		r += b;
	return r;
}

static int
isleap(int y)
{
	return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

static int
rdname(char **f, char *p, int n)
{
	char *s, *e;

	for(s = *f; *s; s++)
		if(*s != ' ' && *s != '\t'  && *s != '\n')
			break;
	e = s + n;
	for(; *s && s != e; s++) {
		if(*s == ' ' || *s == '\t' || *s == '\n')
			break;
		*p++ = *s;
	}
	*p = 0;
	if(n - (e - s) < 3 || *s != ' ' && *s != '\t' && *s != '\n'){
		werrstr("truncated name");
		return -1;
	}
	*f = s;
	return 0;
}

static int
rdlong(char **f, long *p)
{
	int c, s;
	long l;

	s = 0;
	while((c = *(*f)++) != 0){
		if(c == '-')
			s++;
		else if(c != ' ' && c != '\n')
			break;
	}
	if(c == 0) {
		*p = 0;
		return 0;
	}
	l = 0;
	for(;;) {
		if(c == ' ' || c == '\n')
			break;
		if(c < '0' || c > '9'){
			werrstr("non-number %c in name", c);
			return -1;
		}
		l = l*10 + c-'0';
		c = *(*f)++;
	}
	if(s)
		l = -l;
	*p = l;
	return 0;
}

static int
loadzone(Tzone *tz, char *name)
{
	char buf[Tzsize*11+30], path[128], *p;
	int i, f, r;

	memset(tz, 0, sizeof(Tzone));
	if(strcmp(name, "local") == 0)
		snprint(path, sizeof(path), "/env/timezone");
	else
		snprint(path, sizeof(path), "/adm/timezone/%s", name);
	memset(buf, 0, sizeof(buf));
	if((f = open(path, 0)) == -1)
		return -1;
	r = read(f, buf, sizeof(buf));
	close(f);
	if(r == sizeof(buf) || r == -1)
		return -1;
	buf[r] = 0;
	p = buf;
	if(rdname(&p, tz->stname, sizeof(tz->stname)) == -1)
		return -1;
	if(rdlong(&p, &tz->stdiff) == -1)
		return -1;
	if(rdname(&p, tz->dlname, sizeof(tz->dlname)) == -1)
		return -1;
	if(rdlong(&p, &tz->dldiff) == -1)
		return -1;
	for(i=0; i < Tzsize; i++) {
		if(rdlong(&p, &tz->dlpairs[i]) == -1){
			werrstr("invalid transition time");
			return -1;
		}
		if(tz->dlpairs[i] == 0)
			return 0;
	}
	werrstr("invalid timezone %s", name);
	return -1;
}

Tzone*
tzload(char *tzname)
{
	Tzone *tz, **newzones;
	int i;

	if(tzname == nil)
		tzname = "GMT";
	qlock(&zlock);
	for(i = 0; i < nzones; i++){
		tz = zones[i];
		if(strcmp(tz->stname, tzname) == 0)
			goto found;
		if(strcmp(tz->dlname, tzname) == 0)
			goto found;
		if(strcmp(tz->tzname, tzname) == 0)
			goto found;
	}

	tz = malloc(sizeof(Tzone));
	if(tz == nil)
		goto error;
	newzones = realloc(zones, (nzones + 1) * sizeof(Tzone*));
	if(newzones == nil)
		goto error;
	if(loadzone(tz, tzname) != 0)
		goto error;
	if(snprint(tz->tzname, sizeof(tz->tzname), tzname) >= sizeof(tz->tzname)){
		werrstr("timezone name too long");
		return nil;
	}
	zones = newzones;
	zones[nzones] = tz;
	nzones++;
found:
	qunlock(&zlock);
	return tz;
error:
	free(tz);
	qunlock(&zlock);
	return nil;
}

static void
tzoffset(Tzone *tz, vlong abs, Tm *tm)
{
	long dl, *p;
	dl = 0;
	if(tz == nil){
		snprint(tm->zone, sizeof(tm->zone), "GMT");
		tm->tzoff = 0;
		return;
	}
	for(p = tz->dlpairs; *p; p += 2)
		if(abs > p[0] && abs <= p[1]){
			dl = 1;
			break;
		}
	if(dl){
		snprint(tm->zone, sizeof(tm->zone), tz->dlname);
		tm->tzoff = tz->dldiff;
	}else{
		snprint(tm->zone, sizeof(tm->zone), tz->stname);
		tm->tzoff = tz->stdiff;
	}
}

static Tm*
tmfill(Tm *tm, vlong abs, vlong nsec)
{
	vlong zrel, j, y, m, d, t, e;
	int i;

	zrel = abs + tm->tzoff;
	t = zrel % Daysec;
	e = zrel / Daysec;
	if(t < 0){
		t += Daysec;
		e -= 1;
	}

	t += nsec/Nsec;
	tm->sec = mod(t, 60);
	t /= 60;
	tm->min = mod(t, 60);
	t /= 60;
	tm->hour = mod(t, 24);
	tm->wday = mod((e + 4), 7);

	/*
	 * Split up year, month, day.
	 * 
	 * Implemented according to "Algorithm 199,
	 * conversions between calendar  date and
	 * Julian day number", Robert G. Tantzen,
	 * Air Force Missile Development
	 * Center, Holloman AFB, New Mex.
	 * 
	 * Lots of magic.
	 */
	j = (zrel + 2440588 * Daysec) / (Daysec) - 1721119;
	y = (4 * j - 1) / Days400y;
	j = 4 * j - 1 - Days400y * y;
	d = j / 4;
	j = (4 * d + 3) / Days4y;
	d = 4 * d + 3 - Days4y * j;
	d = (d + 4) / 4 ;
	m = (5 * d - 3) / 153;
	d = 5 * d - 3 - 153 * m;
	d = (d + 5) / 5;
	y = 100 * y + j;

	if(m < 10)
		m += 3;
	else{
		m -= 9;
		y++;
	}

	/* there's no year 0 */
	if(y <= 0)
		y--;
	/* and if j negative, the day and month are also negative */
	if(m < 0)
		m += 12;
	if(d < 0)
		d += mdays[m - 1];

	tm->yday = d;
	for(i = 0; i < m - 1; i++)
		tm->yday += mdays[i];
	if(m > 1 && isleap(y))
		tm->yday++;
	tm->year = y - 1900;
	tm->mon = m - 1;
	tm->mday = d;
	tm->nsec = mod(nsec, Nsec);
	return tm;
}	


Tm*
tmtime(Tm *tm, vlong abs, Tzone *tz)
{
	return tmtimens(tm, abs, 0, tz);
}

Tm*
tmtimens(Tm *tm, vlong abs, int ns, Tzone *tz)
{
	tm->tz = tz;
	tzoffset(tz, abs, tm);
	return tmfill(tm, abs, ns);
}

Tm*
tmnow(Tm *tm, Tzone *tz)
{
	vlong ns;

	ns = nsec();
	return tmtimens(tm, nsec()/Nsec, mod(ns, Nsec), tz);
}

vlong
tmnorm(Tm *tm)
{
	vlong c, yadj, j, abs, y, m, d;

	if(tm->mon > 1){
		m = tm->mon - 2;
		y = tm->year + 1900;
	}else{
		m = tm->mon + 10;
		y = tm->year + 1899;
	}
	d = tm->mday;
	c = y / 100;
	yadj = y - 100 * c;
	j = (c * Days400y / 4 + 
		Days4y * yadj / 4 +
		(153 * m + 2)/5 + d -
		719469);
	abs = j * Daysec;
	abs += tm->hour * 3600;
	abs += tm->min * 60;
	abs += tm->sec;
	if(tm->tz){
		tzoffset(tm->tz, abs - tm->tzoff, tm);
		tzoffset(tm->tz, abs - tm->tzoff, tm);
	}
	abs -= tm->tzoff;
	tmfill(tm, abs, tm->nsec);
	return abs;
}

static int
τconv(Fmt *f)
{
	int depth, n, v, w, h, m, c0, sgn, pad, off;
	char *p, *am;
	Tmfmt tf;
	Tm *tm;

	n = 0;
	tf = va_arg(f->args, Tmfmt);
	tm = tf.tm;
	p = tf.fmt;
	if(p == nil)
		p = Ctimefmt;
	while(*p){
		w = 1;
		pad = 0;
		while(*p == '_'){
			pad++;
			p++;
		}
		c0 = *p++;
		while(c0 && *p == c0){
			w++;
			p++;
		}
		pad += w;
		switch(c0){
		case 0:
			break;
		case 'Y':
			switch(w){
			case 1:	n += fmtprint(f, "%*d", pad, tm->year + 1900);		break;
			case 2: n += fmtprint(f, "%*d", pad, tm->year % 100);		break;
			case 4:	n += fmtprint(f, "%*d", pad, tm->year + 1900);		break;
			default: goto badfmt;
			}
			break;
		case 'M':
			switch(w){
			case 1: n += fmtprint(f, "%*d", pad, tm->mon + 1);		break;
			case 2:	n += fmtprint(f, "%*s%02d", pad-2, "", tm->mon + 1);	break;
			case 3:	n += fmtprint(f, "%*.3s", pad, month[tm->mon]);		break;
			case 4:	n += fmtprint(f, "%*s", pad, month[tm->mon]);		break;
			default: goto badfmt;
			}
			break;
		case 'D':
			switch(w){
			case 1: n += fmtprint(f, "%*d", pad, tm->mday);			break;
			case 2:	n += fmtprint(f, "%*s%02d", pad-2, "", tm->mday);	break;
			default: goto badfmt;
			}
			break;
		case 'W':
			switch(w){
			case 1:	n += fmtprint(f, "%*d", pad, tm->wday + 1);		break;
			case 2:	n += fmtprint(f, "%*.3s", pad, wday[tm->wday]);		break;
			case 3:	n += fmtprint(f, "%*s", pad, wday[tm->wday]);		break;
			default: goto badfmt;
			}
			break;
		case 'H':
			switch(w){
			case 1: n += fmtprint(f, "%*d", pad, tm->hour % 12);		break;
			case 2:	n += fmtprint(f, "%*s%02d", pad-2, "", tm->hour % 12);	break;
			default: goto badfmt;
			}
			break;
		case 'h':
			switch(w){
			case 1: n += fmtprint(f, "%*d", pad, tm->hour);			break;
			case 2:	n += fmtprint(f, "%*s%02d", pad-2, "", tm->hour);	break;
			default: goto badfmt;
			}
			break;
		case 'm':
			switch(w){
			case 1: n += fmtprint(f, "%*d", pad, tm->min);			break;
			case 2:	n += fmtprint(f, "%*s%02d", pad-2, "", tm->min);	break;
			default: goto badfmt;
			}
			break;
		case 's':
			switch(w){
			case 1: n += fmtprint(f, "%*d", pad, tm->sec);			break;
			case 2:	n += fmtprint(f, "%*s%02d", pad-2, "", tm->sec);	break;
			default: goto badfmt;
			}
			break;
		case 't':
			v = tm->nsec / (1000*1000);
			switch(w){
			case 1:	n += fmtprint(f, "%*d", pad, v % 1000);			break;
			case 2:
			case 3:	n += fmtprint(f, "%*s%03d", P(pad, 3), "", v % 1000);	break;
			default: goto badfmt;
			}
			break;
		case 'u':
			v = tm->nsec / 1000;
			switch(w){
			case 1:	n += fmtprint(f, "%*d", pad, v % 1000);			break;
			case 2:	n += fmtprint(f, "%*s%03d", P(pad, 3), "", v % 1000);	break;
			case 3:	n += fmtprint(f, "%*d", P(pad, 6), v);			break;
			case 4:	n += fmtprint(f, "%*s%06d", P(pad, 6), "", v);		break;
			default: goto badfmt;
			}
			break;
		case 'n':
			v = tm->nsec;
			switch(w){
			case 1:	n += fmtprint(f, "%*d", pad, v%1000);			break;
			case 2:	n += fmtprint(f, "%*s%03d", P(pad, 3), "", v % 1000);	break;
			case 3:	n += fmtprint(f, "%*d", pad , v%(1000*1000));		break;
			case 4: n += fmtprint(f, "%*s%06d", P(pad, 6), "", v%(1000000)); break;
			case 5:	n += fmtprint(f, "%*d", pad, v);			break;
			case 6:	n += fmtprint(f, "%*s%09d", P(pad, 9), "", v);		break;
			default: goto badfmt;
			}
			break;
		case 'z':
			if(w != 1)
				goto badfmt;
		case 'Z':
			sgn = (tm->tzoff < 0) ? '-' : '+';
			off = (tm->tzoff < 0) ? -tm->tzoff : tm->tzoff;
			h = off/3600;
			m = (off/60)%60;
			if(w < 3 && pad < 5)
				pad = 5;
			switch(w){
			case 1:	n += fmtprint(f, "%*s%c%02d%02d", pad-5, "", sgn, h, m); break;
			case 2:	n += fmtprint(f, "%*s%c%02d:%02d", pad-5, "", sgn, h, m); break;
			case 3:	n += fmtprint(f, "%*s", pad, tm->zone);			 break;
			}
			break;
		case 'A':
		case 'a':
			if(w != 1)
				goto badfmt;
			if(c0 == 'a')
				am = (tm->hour < 12) ? "am" : "pm";
			else
				am = (tm->hour < 12) ? "AM" : "PM";
			n += fmtprint(f, "%*s", pad, am);
			break;
		case '[':
			depth = 1;
			while(*p){
				if(*p == '[')
					depth++;
				if(*p == ']')
					depth--;
				if(*p == '\\')
					p++;
				if(depth == 0)
					break;
				fmtrune(f, *p++);
			}
			if(*p++ != ']')
				goto badfmt;
			break;
		default:
			while(w-- > 0)
				n += fmtrune(f, c0);
			break;
		}
	}
	return n;
badfmt:
	werrstr("garbled format %s", tf.fmt);
	return -1;			
}

static int
getnum(char **ps, int maxw, int *ok)
{
	char *s, *e;
	int n;

	n = 0;
	e = *ps + maxw;
	for(s = *ps; s != e && *s >= '0' && *s <= '9'; s++){
		n *= 10;
		n += *s - '0';
	}
	*ok = s != *ps;
	*ps = s;
	return n;
}

static int
lookup(char **s, char **tab, int len, int *ok)
{
	int nc, i;

	*ok = 0;
	for(i = 0; *tab; tab++){
		nc = (len != -1) ? len : strlen(*tab);
		if(cistrncmp(*s, *tab, nc) == 0){
			*s += nc;
			*ok = 1;
			return i;
		}
		i++;
	}
	*ok = 0;
	return -1;
}

Tm*
tmparse(Tm *tm, char *fmt, char *str, Tzone *tz, char **ep)
{
	int depth, n, w, c0, zs, z0, z1, md, ampm, zoned, sloppy, tzo, ok;
	vlong abs;
	char *s, *p, *q;
	Tzone *zparsed, *local;
	Tzabbrev *a;
	Tzoffpair *m;

	p = fmt;
	s = str;
	tzo = 0;
	ampm = -1;
	zoned = 0;
	zparsed = nil;
	sloppy = 0;
	/* Default all fields */
	tmtime(tm, 0, nil);
	if(*p == '~'){
		sloppy = 1;
		p++;
	}

	/* Skip whitespace */
	for(;; s++) {
		switch(*s) {
		case ' ':
		case '\t':
		case '\n':
		case '\f':
		case '\r':
		case '\v':
			continue;
		}
		break;
	}
	while(*p){
		w = 1;
		c0 = *p++;
		if(c0 == '?'){
			w = -1;
			c0 = *p++;
		}
		while(*p == c0){
			if(w != -1) w++;
			p++;
		}
		ok = 1;
		switch(c0){
		case 'Y':
			switch(w){
			case -1:
				tm->year = getnum(&s, 4, &ok);
				if(tm->year > 100) tm->year -= 1900;
				break;
			case 1:	tm->year = getnum(&s, 4, &ok) - 1900;	break;
			case 2: tm->year = getnum(&s, 2, &ok);		break;
			case 3:
			case 4:	tm->year = getnum(&s, 4, &ok) - 1900;	break;
			default: goto badfmt;
			}
			break;
		case 'M':
			switch(w){
			case -1:
				tm->mon = getnum(&s, 2, &ok) - 1;
				if(!ok) tm->mon = lookup(&s, month, -1, &ok);
				if(!ok) tm->mon = lookup(&s, month, 3, &ok);
				break;
			case 1:
			case 2: tm->mon = getnum(&s, 2, &ok) - 1;	break;
			case 3:	tm->mon = lookup(&s, month, 3, &ok);	break;
			case 4:	tm->mon = lookup(&s, month, -1, &ok);	break;
			default: goto badfmt;
			}
			break;
		case 'D':
			switch(w){
			case -1:
			case 1:
			case 2: tm->mday = getnum(&s, 2, &ok);		break;
			default: goto badfmt;
			}
			break;
		case 'W':
			switch(w){
			case -1:
				tm->wday = lookup(&s, wday, -1, &ok);
				if(!ok) tm->wday = lookup(&s, wday, 3, &ok);
				if(!ok) tm->wday = getnum(&s, 1, &ok) - 1;
				break;
			case 1: tm->wday = getnum(&s, 1, &ok) - 1;	break;
			case 2:	tm->wday = lookup(&s, wday, 3, &ok);	break;
			case 3:	tm->wday = lookup(&s, wday, -1, &ok);	break;
			default: goto badfmt;
			}
			break;
		case 'h':
			switch(w){
			case -1:
			case 1:
			case 2: tm->hour = getnum(&s, 2, &ok);		break;
			default: goto badfmt;
			}
			break;
		case 'm':
			switch(w){
			case -1:
			case 1:
			case 2: tm->min = getnum(&s, 2, &ok);		break;
			default: goto badfmt;
			}
			break;
		case 's':
			switch(w){
			case -1:
			case 1:
			case 2: tm->sec = getnum(&s, 2, &ok);		break;
			default: goto badfmt;
			}
			break;
		case 't':
			switch(w){
			case -1:
			case 1:
			case 2:
			case 3:	tm->nsec += getnum(&s, 3, &ok)*1000000;	break;
			}
			break;
		case 'u':
			switch(w){
			case -1:
			case 1:
			case 2:	tm->nsec += getnum(&s, 3, &ok)*1000;	break;
			case 3:
			case 4: tm->nsec += getnum(&s, 6, &ok)*1000;	break;
			}
			break;
		case 'n':
			switch(w){
			case 1:
			case 2:	tm->nsec += getnum(&s, 3, &ok);		break;
			case 3:
			case 4: tm->nsec += getnum(&s, 6, &ok);		break;
			case -1:
			case 5:
			case 6: tm->nsec += getnum(&s, 9, &ok);		break;
			}
			break;
		case 'z':
			if(w != 1)
				goto badfmt;
		case 'Z':
			zs = 0;
			zoned = 1;
			switch(*s++){
			case '+': zs = 1; break;
			case '-': zs = -1; break;
			default: s--; break;
			}
			q = s;
			switch(w){
			case -1:
			case 3:
				/*
				 * Ugly Hack:
				 * Ctime is defined as printing a 3-character timezone
				 * name. The timezone name is ambiguous. For example,
				 * EST refers to both Australian and American eastern
				 * time. On top of that, we don't want to make the
				 * tzabbrev table exhaustive. So, we put in this hack:
				 *
				 * Before we consult the well known table of timezones,
				 * we check if the local time matches the timezone name.
				 *
				 * If you want unambiguous timezone parsing, use numeric
				 * timezone offsets (Z, ZZ formats).
				 */
				if((local = tzload("local")) != nil){
					if(cistrncmp(s, local->stname, strlen(local->stname)) == 0){
						s += strlen(local->stname);
						zparsed = local;
						goto Zoneparsed;
					}
					if(cistrncmp(s, local->dlname, strlen(local->dlname)) == 0){
						s += strlen(local->dlname);
						zparsed = local;
						goto Zoneparsed;
					}
				}
				for(a = tzabbrev; a->abbr; a++){
					n = strlen(a->abbr);
					if(cistrncmp(s, a->abbr, n) == 0 && !isalpha(s[n]))
						break;
				}
				if(a->abbr != nil){
					s += strlen(a->abbr);
					zparsed = tzload(a->name);
					if(zparsed == nil){
						werrstr("unloadable zone %s (%s)", a->abbr, a->name);
						if(w != -1)
							return nil;
					}
					goto Zoneparsed;
				}
				for(m = milabbrev; m->abbr != nil; m++){
					n = strlen(m->abbr);
					if(cistrncmp(s, m->abbr, n) == 0 && !isalpha(s[n]))
						break;
				}
				if(m->abbr != nil){
					snprint(tm->zone, sizeof(tm->zone), "%s", m->abbr);
					tzo = m->off;
					goto Zoneparsed;
				}
				if(w != -1)
					break;
				/* fall through */
			case 1:
				/* offset: [+-]hhmm */
				z0 = getnum(&s, 4, &ok);
				if(s - q == 4){
					z1 = z0 % 100;
					if(z0/100 > 13 || z1 >= 60)
						goto baddate;
					tzo = zs*(3600*(z0/100) + 60*z1);
					snprint(tm->zone, sizeof(tm->zone), "%c%02d%02d", zs<0?'-':'+', z0/100, z1);
					goto Zoneparsed;
				}
				if(w != -1)
					goto baddate;
				/* fall through */
			case 2:
				s = q;
				/* offset: [+-]hh:mm */
				z0 = getnum(&s, 2, &ok);
				if(*s++ != ':')
					break;
				z1 = getnum(&s, 2, &ok);
				if(z1 > 60)
					break;
				tzo = zs*(3600*z0 + 60*z1);
				snprint(tm->zone, sizeof(tm->zone), "%c%d02:%02d", zs<0?'-':'+', z0, z1);
				goto Zoneparsed;
			}
			if(w != -1)
				goto baddate;
			/*
			 * Final fuzzy fallback: If we have what looks like an
			 * unknown timezone abbreviation, keep the zone name,
			 * but give it a timezone offset of 0. This allows us
			 * to avoid rejecting zones outside of RFC5322.
			 */
			for(s = q; *s; s++)
				if(!isalpha(*s))
					break;
			if(s - q >= 3 && !isalpha(*s)){
				strncpy(tm->zone, q, s - q);
				tzo = 0;
				ok = 1;
				goto Zoneparsed;
			}
			goto baddate;
Zoneparsed:
			break;
		case 'A':
		case 'a':
			if(cistrncmp(s, "am", 2) == 0)
				ampm = 0;
			else if(cistrncmp(s, "pm", 2) == 0)
				ampm = 1;
			else
				goto baddate;
			s += 2;
			break;
		case '[':
			depth = 1;
			while(*p){
				if(*p == '[')
					depth++;
				if(*p == ']')
					depth--;
				if(*p == '\\')
					p++;
				if(depth == 0)
					break;
				if(*s == 0)
					goto baddate;
				if(*s++ != *p++)
					goto baddate;
			}
			if(*p != ']')
				goto badfmt;
			p++;
			break;
		case '_':
		case ',':
		case ' ':
			if(*s != ' ' && *s != '\t' && *s != ',' && *s != '\n' && *s != '\0')
				goto baddate;
			p += strspn(p, " ,_\t\n");
			s += strspn(s, " ,\t\n");
			break;
		default:
			if(*s == 0)
				goto baddate;
			if(*s++ != c0)
				goto baddate;
			break;
		}
		if(!ok)
			goto baddate;
	}

	if(*p != '\0')
		goto baddate;
	if(ep != nil)
		*ep = s;
	if(!sloppy && ampm != -1 && (tm->hour < 1 || tm->hour > 12))
		goto baddate;
	if(ampm == 0 && tm->hour == 12)
		tm->hour = 0;
	else if(ampm == 1 && tm->hour < 12)
		tm->hour += 12;
	/*
	 * If we're allowing sloppy date ranges,
	 * we'll normalize out of range values.
	 */
	if(!sloppy){
		if(tm->yday < 0 || tm->yday > 365 + isleap(tm->year + 1900))
			goto baddate;
		if(tm->wday < 0 || tm->wday > 6)
			goto baddate;
		if(tm->mon < 0 || tm->mon > 11)
			goto baddate;
		md = mdays[tm->mon];
		if(tm->mon == 1 && isleap(tm->year + 1900))
			md++;
		if(tm->mday < 0 || tm->mday > md)
			goto baddate;
		if(tm->hour < 0 || tm->hour > 24)
			goto baddate;
		if(tm->min < 0 || tm->min > 59)
			goto baddate;
		if(tm->sec < 0 || tm->sec > 60)
			goto baddate;
		if(tm->nsec < 0 || tm->nsec > Nsec)
			goto baddate;
		if(tm->wday < 0 || tm->wday > 6)
			goto baddate;
	}

	/*
	 * Normalizing gives us the local time,
	 * but because we havnen't applied the
	 * timezone, we think we're GMT. So, we
	 * need to shift backwards. Then, we move
	 * the "GMT that was local" back to local
	 * time.
	 */
	abs = tmnorm(tm);
	tm->tzoff = tzo;
	if(!zoned)
		tzoffset(tz, abs, tm);
	else if(zparsed != nil){
		tzoffset(zparsed, abs, tm);
		tzoffset(zparsed, abs + tm->tzoff, tm);
	}
	abs -= tm->tzoff;
	if(tz != nil || !zoned)
		tmtimens(tm, abs, tm->nsec, tz);
	return tm;
baddate:
	werrstr("invalid date %s", str);
	return nil;
badfmt:
	werrstr("garbled format %s near '%s'", fmt, p);
	return nil;			
}

Tmfmt
tmfmt(Tm *d, char *fmt)
{
	return (Tmfmt){fmt, d};
}

void
tmfmtinstall(void)
{
	fmtinstall(L'τ', τconv);
}

/* These legacy functions need access to τconv */
static char*
dotmfmt(Fmt *f, ...)
{
	static char buf[30];
	va_list ap;

	va_start(ap, f);
	f->runes = 0;
	f->start = buf;
	f->to = buf;
	f->stop = buf + sizeof(buf) - 1;
	f->flush = nil;
	f->farg = nil;
	f->nfmt = 0;
	f->args = ap;
	τconv(f);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	return buf;
}

char*
asctime(Tm* tm)
{
	Tmfmt tf;
	Fmt f;

	tf = tmfmt(tm, "WW MMM _D hh:mm:ss ZZZ YYYY\n");
	return dotmfmt(&f, tf);
}

