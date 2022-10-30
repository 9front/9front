#include <u.h>
#include <libc.h>

int
strtotm(char *s, Tm *t)
{
	char **f, *fmt[] = {
		"WW MMM DD hh:mm:ss ?Z YYYY",
		"WW MMM DD hh:mm:ss YYYY",
		"?WW ?DD ?MMM ?YYYY hh:mm:ss ?Z",
		"?WW ?DD ?MMM ?YYYY hh:mm:ss",
		"?WW, DD-?MM-YY",
		"?DD ?MMM ?YYYY hh:mm:ss ?Z",
		"?DD ?MMM ?YYYY hh:mm:ss",
		"?DD-?MM-YY hh:mm:ss ?Z",
		"?DD-?MM-YY hh:mm:ss",
		"?DD-?MM-YY",
		"?MMM/?DD/?YYYY hh:mm:ss ?Z",
		"?MMM/?DD/?YYYY hh:mm:ss",
		"?MMM/?DD/?YYYY",
		nil,
	};

	for(f = fmt; *f; f++)
		if(tmparse(t, *f, s, nil, nil) != nil)
			return 0;
	return -1;
}
