#include "common.h"

/*
 *  WARNING!  This turns all upper case names into lower case
 *  local ones.
 */

static	String	*getdbfiles(void);
static	int	translate(char*, char**, String*, String*);
static	int	lookup(String**, String*, String*);
static	char	*mklower(char*);

static	int	debug;
static	int	from;
static	char	*namefiles = "namefiles";

#define dprint(...) if(debug)fprint(2, __VA_ARGS__); else {}

void
usage(void)
{
	fprint(2, "usage: aliasmail [-df] [-n namefile] [names ...]\n");
	exits("usage");
}

void
main(int argc, char *argv[])
{
	char *alias, **names, *p;		/* names of this system */
	int i, rv;
	String *s, *salias, *files;

	ARGBEGIN {
	case 'd':
		debug = 1;
		break;
	case 'f':
		from = 1;
		break;
	case 'n':
		namefiles = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND
	if (chdir(UPASLIB) < 0)
		sysfatal("chdir: %r");

	names = sysnames_read();
	files = getdbfiles();
	salias = s_new();

	/* loop through the names to be translated (from standard input) */
	for(i=0; i<argc; i++) {
		s = unescapespecial(s_copy(mklower(argv[i])));
		if(strchr(s_to_c(s), '!') == 0)
			rv = translate(s_to_c(s), names, files, salias);
		else
			rv = -1;
		alias = s_to_c(salias);
		if(from){
			if (rv >= 0 && *alias != '\0'){
				if(p = strchr(alias, '\n'))
					*p = 0;
				if(p = strchr(alias, '!')) {
					*p = 0;
					print("%s", alias);
				} else {
					if(p = strchr(alias, '@'))
						print("%s", p+1);
					else
						print("%s", alias);
				}
			}
		} else {
			if (rv < 0 || *alias == '\0')
				print("local!%s\n", s_to_c(s));
			else
				/* this must be a write, not a print */
				write(1, alias, strlen(alias));
		}
		s_free(s);
	}
	exits(0);
}

/* get the list of dbfiles to search */
static String *
getdbfiles(void)
{
	char *nf;
	Sinstack *sp;
	String *files;

	if(from)
		nf = "fromfiles";
	else
		nf = namefiles;

	/* system wide aliases */
	files = s_new();
	if ((sp = s_allocinstack(nf)) != 0){
		while(s_rdinstack(sp, files))
			s_append(files, " ");
		s_freeinstack(sp);
	}

	dprint("files are %s\n", s_to_c(files));
	return files;
}

/* loop through the translation files */
static int
translate(char *name, char **namev,	String *files, String *alias)
{
	int n, rv;
	String *file, **fullnamev;

	rv = -1;
	file = s_new();

	dprint("translate(%s, %s, %s)\n", name,
		s_to_c(files), s_to_c(alias));

	/* create the full name to avoid loops (system!name) */
	for(n = 0; namev[n]; n++)
		;

	fullnamev = (String**)malloc(sizeof(String*)*(n+2));
	n = 0;
	fullnamev[n++] = s_copy(name);
	for(; *namev; namev++){
		fullnamev[n] = s_copy(*namev);
		s_append(fullnamev[n], "!");
		s_append(fullnamev[n], name);
		n++;
	}
	fullnamev[n] = 0;

	/* look at system-wide names */
	s_restart(files);
	while (s_parse(files, s_restart(file)) != 0)
		if (lookup(fullnamev, file, alias)==0) {
			rv = 0;
			goto out;
		}

out:
	for(n = 0; fullnamev[n]; n++)
		s_free(fullnamev[n]);
	s_free(file);
	free(fullnamev);
	return rv;
}

/*
 *  very dumb conversion to bang format
 */
static String*
attobang(String *token)
{
	char *p;
	String *tok;

	p = strchr(s_to_c(token), '@');
	if(p == 0)
		return token;

	p++;
	tok = s_copy(p);
	s_append(tok, "!");
	s_nappend(tok, s_to_c(token), p - s_to_c(token) - 1);

	return tok;
}

/*  Loop through the entries in a translation file looking for a match.
 *  Return 0 if found, -1 otherwise.
 */
#define compare(a, b) cistrcmp(s_to_c(a), b)

static int
lookup(String **namev, String *file, String *alias)
{
	char *name;
	int i, rv;
	String *line, *token, *bangtoken;
	Sinstack *sp;

	dprint("lookup(%s, %s, %s, %s)\n", s_to_c(namev[0]), s_to_c(namev[1]),
		s_to_c(file), s_to_c(alias));

	rv = -1;
	name = s_to_c(namev[0]);
	line = s_new();
	token = s_new();
	s_reset(alias);
	if ((sp = s_allocinstack(s_to_c(file))) == 0)
		return -1;

	/* look for a match */
	while (s_rdinstack(sp, s_restart(line))!=0) {
		dprint("line is %s\n", s_to_c(line));
		s_restart(token);
		if (s_parse(s_restart(line), token)==0)
			continue;
		if (compare(token, "#include")==0){
			if(s_parse(line, s_restart(token))!=0) {
				if(lookup(namev, line, alias) == 0)
					break;
			}
			continue;
		}
		if (compare(token, name)!=0)
			continue;
		/* match found, get the alias */
		while(s_parse(line, s_restart(token))!=0) {
			bangtoken = attobang(token);

			/* avoid definition loops */
			for(i = 0; namev[i]; i++)
				if(compare(bangtoken, s_to_c(namev[i]))==0) {
					s_append(alias, "local");
					s_append(alias, "!");
					s_append(alias, name);
					break;
				}

			if(namev[i] == 0)
				s_append(alias, s_to_c(token));
			s_append(alias, "\n");

			if(bangtoken != token)
				s_free(bangtoken);
		}
		rv = 0;
		break;
	}
	s_free(line);
	s_free(token);
	s_freeinstack(sp);
	return rv;
}

static char*
mklower(char *name)
{
	char c, *p;

	for(p = name; c = *p; p++)
		if(c >= 'A' && c <= 'Z')
			*p = c + 0x20;
	return name;
}
