#include <u.h>
#include <libc.h>

typedef struct Tzabbrev Tzabbrev;
typedef struct Tzoffpair Tzoffpair;

#define Ctimefmt "W MMM _D hh:mm:ss ZZZ YYYY\n"
enum {
	Tzsize		= 150,
	Nsec		= 1000*1000*1000,
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
	char	tzname[16];
	char	stname[4];
	char	dlname[4];
	long	stdiff;
	long	dldiff;
	long	dlpairs[Tzsize];
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

static int
isleap(int y)
{
	return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

static int
rdname(char **f, char *p)
{
	int c, i;

	while((c = *(*f)++) != 0)
		if(c != ' ' && c != '\n')
			break;
	for(i=0; i<3; i++) {
		if(c == ' ' || c == '\n')
			return 1;
		*p++ = c;
		c = *(*f)++;
	}
	if(c != ' ' && c != '\n')
		return 1;
	*p = 0;
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
		if(c < '0' || c > '9')
			return 1;
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
	p = buf;
	if(rdname(&p, tz->stname))
		return -1;
	if(rdlong(&p, &tz->stdiff))
		return -1;
	if(rdname(&p, tz->dlname))
		return -1;
	if(rdlong(&p, &tz->dldiff))
		return -1;
	for(i=0; i < Tzsize; i++) {
		if(rdlong(&p, &tz->dlpairs[i]))
			return -1;
		if(tz->dlpairs[i] == 0)
			return 0;
	}
	return -1;
}

Tzone*
tmgetzone(char *tzname)
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
getzoneoff(Tzone *tz, vlong abs, Tm *tm)
{
	long dl, *p;
	dl = 0;
	if(tz == nil){
		snprint(tm->zone, sizeof(tm->zone), "GMT");
		tm->tzoff = 0;
		return;
	}
	for(p = tz->dlpairs; *p; p += 2)
		if(abs >= p[0] && abs < p[1])
			dl = 1;
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

	tm->abs = abs;
	zrel = abs + tm->tzoff;
	t = zrel % Daysec;
	e = zrel / Daysec;
	if(t < 0){
		t += Daysec;
		e -= 1;
	}

	t += nsec/Nsec;
	tm->sec = t % 60;
	t /= 60;
	tm->min = t % 60;
	t /= 60;
	tm->hour = t;
	tm->wday = (e + 4) % 7;

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
	if(m > 1 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
		tm->yday++;
	tm->year = y - 1900;
	tm->mon = m - 1;
	tm->mday = d;
	tm->nsec = nsec%Nsec;
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
	getzoneoff(tz, abs, tm);
	return tmfill(tm, abs, ns);
}

Tm*
tmnow(Tm *tm, Tzone *tz)
{
	vlong ns;

	ns = nsec();
	return tmtimens(tm, nsec()/Nsec, ns%Nsec, tz);
}

Tm*
tmnorm(Tm *tm)
{
	vlong c, yadj, j, abs, y, m, d;

	if(tm->mon > 1){
		m = tm->mon - 2;
		y = tm->year + 1900;
	}else{
		m = tm->mon + 10;
		y = tm->year - 1901;
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
	abs -= tm->tzoff;
	return tmfill(tm, abs, tm->nsec);
}

static int
τconv(Fmt *f)
{
	int depth, n, w, h, m, c0, sgn, pad, off;
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
			case 2:	n += fmtprint(f, "%*s%02d", pad-2, "", tm->mday);		break;
			default: goto badfmt;
			}
			break;
		case 'W':
			switch(w){
			case 1:	n += fmtprint(f, "%*.3s", pad, wday[tm->wday]);	break;
			case 2:	n += fmtprint(f, "%*s", pad, wday[tm->wday]);		break;
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
tmparse(Tm *tm, char *fmt, char *str, Tzone *tz)
{
	int depth, w, c0, zs, z0, z1, ampm, zoned, sloppy, tzo, ok;
	char *s, *p, *q;
	Tzone *zparsed;
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
				break;
			case 1:	tm->wday = lookup(&s, wday, 3, &ok);	break;
			case 2:	tm->wday = lookup(&s, wday, -1, &ok);	break;
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
			switch(w){
			case -1:
			case 3:
				for(a = tzabbrev; a->abbr; a++)
					if(strncmp(s, a->abbr, strlen(a->abbr)) == 0)
						break;
				if(a->abbr != nil){
					s += strlen(a->abbr);
					zparsed = tmgetzone(a->name);
					if(zparsed == nil){
						werrstr("unloadable zone %s (%s)", a->abbr, a->name);
						return nil;
					}
					break;
				}
				for(m = milabbrev; m->abbr != nil; m++)
					if(strncmp(s, m->abbr, strlen(m->abbr)) == 0)
						break;
				if(m->abbr != nil){
					snprint(tm->zone, sizeof(tm->zone), "%s", m->abbr);
					tzo = m->off;
					break;
				}
				/* fall through */
			case 1:
				/* offset: [+-]hhmm */
				q = s;
				z0 = getnum(&s, 4, &ok);
				if(s - q == 4){
					z1 = z0 % 100;
					if(z0/100 > 13 || z1 >= 60)
						goto baddate;
					tzo = zs*(3600*z0/100 + 60*z1);
					snprint(tm->zone, sizeof(tm->zone), "%c%02d%02d", zs<0?'-':'+', z0/100, z1);
					break;
				}
				if(w != -1)
					goto baddate;
				s = q;
				/* fall through */
			case 2:
				/* offset: [+-]hh:mm */
				z0 = getnum(&s, 2, &ok);
				if(*s++ != ':')
					goto baddate;
				z1 = getnum(&s, 2, &ok);
				if(z1 > 60)
					goto baddate;
				tzo = zs*(3600*z0 + 60*z1);
				snprint(tm->zone, sizeof(tm->zone), "%c%d02:%02d", zs<0?'-':'+', z0, z1);
				break;
			}
			break;
		case 'A':
		case 'a':
			if(cistrncmp(s, "am", 2) == 0)
				ampm = 0;
			else if(cistrncmp(s, "pm", 2) == 0)
				ampm = 1;
			else
				goto baddate;
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
		case ',':
		case ' ':
		case '\t':
			if(*s != ' ' && *s != '\t' && *s != ',')
				goto baddate;
			while(*p == ' ' || *p == '\t' || *p == ',')
				p++;
			while(*s == ' ' || *s == '\t' || *s == ',')
				s++;
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
	
	if(!sloppy && ampm != -1 && tm->hour > 12)
		goto baddate;
	if(ampm == 1)
		tm->hour += 12;
	/*
	 * If we're allowing sloppy date ranges,
	 * we'll normalize out of range values.
	 */
	if(!sloppy){
		if(tm->yday < 0 && tm->yday > 365 + isleap(tm->year + 1900))
			goto baddate;
		if(tm->wday < 0 && tm->wday > 6)
			goto baddate;
		if(tm->mon < 0 || tm->mon > 11)
			goto baddate;
		if(tm->mday < 0 || tm->mday > mdays[tm->mon])
			goto baddate;
		if(tm->hour < 0 || tm->hour > 24)
			goto baddate;
		if(tm->min < 0 || tm->min > 59)
			goto baddate;
		if(tm->sec < 0 || tm->sec > 60)
			goto baddate;
		if(tm->nsec < 0 || tm->nsec > Nsec)
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
 	tmnorm(tm);
	tm->tzoff = tzo;
	if(!zoned)
		getzoneoff(tz, tm->abs, tm);
	else if(zparsed != nil)
		getzoneoff(zparsed, tm->abs, tm);
	tm->abs -= tm->tzoff;
	if(tz != nil || !zoned)
		tmtime(tm, tm->abs, tz);
	return tm;
baddate:
	werrstr("invalid date %s near '%s'", str, s);
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

	tf = tmfmt(tm, nil);
	return dotmfmt(&f, tf);
}
