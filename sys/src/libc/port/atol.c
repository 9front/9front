#include <u.h>
#include <libc.h>

long
atol(char *s)
{
	return strtol(s, nil, 10);
}

int
atoi(char *s)
{
	return strtol(s, nil, 10);
}
