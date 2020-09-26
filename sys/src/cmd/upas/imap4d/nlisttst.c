#include "nlist.c"

Biobuf	bout;
Bin	*parsebin;

void
bye(char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	Bprint(&bout, "* bye ");
	Bvprint(&bout, fmt, arg);
	Bprint(&bout, "\r\n");
	Bflush(&bout);
	exits(0);
}

static char *stoplist[] =
{
	".",
	"dead.letter",
	"forward",
	"headers",
	"imap.subscribed",
	"mbox",
	"names",
	"pipefrom",
	"pipeto",
	0
};
int
okmbox(char *path)
{
	char *name;
	int i, c;

	name = strrchr(path, '/');
	if(name == nil)
		name = path;
	else
		name++;
	if(strlen(name) + STRLEN(".imp") >= Pathlen)
		return 0;
	for(i = 0; stoplist[i]; i++)
		if(strcmp(name, stoplist[i]) == 0)
			return 0;
	c = name[0];
	if(c == 0 || c == '-' || c == '/'
	|| isdotdot(name)
	|| isprefix("L.", name)
	|| isprefix("imap-tmp.", name)
	|| issuffix("-", name)
	|| issuffix(".00", name)
	|| issuffix(".imp", name)
	|| issuffix(".idx", name))
		return 0;

	return 1;
}

void
usage(void)
{
	fprint(2, "usage: nlist ref pat\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int lsub;

	lsub = 0;
	ARGBEGIN{
	case 'l':
		lsub = 1;
		break;
	default:
		usage();
	}ARGEND
	if(argc != 2)
		usage();
	Binit(&bout, 1, OWRITE);
	quotefmtinstall();
	if(lsub)
		Bprint(&bout, "lsub→%d\n", lsubboxes("lsub", argv[0], argv[1]));
	else
		Bprint(&bout, "→%d\n", listboxes("list", argv[0], argv[1]));
	Bterm(&bout);
	exits("");
}
