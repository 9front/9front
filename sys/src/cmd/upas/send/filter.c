#include "common.h"
#include "send.h"
#include <regexp.h>

Biobuf	bin;
int	flagn;
int	flagx;
int	rmail;
int	tflg;
char	*subjectarg;

char*
findbody(char *p)
{
	if(*p == '\n')
		return p;

	while(*p){
		if(*p == '\n' && *(p+1) == '\n')
			return p+1;
		p++;
	}
	return p;
}

int
refuse(dest*, message *, char *cp, int, int)
{
	fprint(2, "%s", cp);
	exits("error");
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: upas/filter [-nbh] rcvr mailbox [regexp file] ...\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *cp, file[Pathlen];
	int i, header, body;
	message *mp;
	dest *dp;
	Reprog *p;
	Resub match[10];

	header = body = 0;
	ARGBEGIN {
	case 'n':
		flagn = 1;
		break;
	case 'h':
		header = 1;
		break;
	case 'b':
		header = 1;
		body = 1;
		break;
	default:
		usage();
	} ARGEND

	Binit(&bin, 0, OREAD);
	if(argc < 2)
		usage();
	mp = m_read(&bin, 1, 0);

	/* get rid of local system name */
	cp = strchr(s_to_c(mp->sender), '!');
	if(cp){
		cp++;
		mp->sender = s_copy(cp);
	}

	strecpy(file, file+sizeof file, argv[1]);
	cp = findbody(s_to_c(mp->body));
	for(i = 2; i < argc; i += 2){
		p = regcomp(argv[i]);
		if(p == 0)
			continue;
		if(regexec(p, s_to_c(mp->sender), match, 10)){
			regsub(argv[i+1], file, sizeof(file), match, 10);
			break;
		}
		if(header == 0 && body == 0)
			continue;
		if(regexec(p, s_to_c(mp->body), match, 10)){
			if(body == 0 && match[0].sp >= cp)
				continue;
			regsub(argv[i+1], file, sizeof(file), match, 10);
			break;
		}
	}
	dp = d_new(s_copy(argv[0]));
	dp->repl1 = s_copy(file);
	if(cat_mail(dp, mp) != 0)
		exits("fail");
	exits("");
}
