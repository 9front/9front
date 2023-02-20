#include <u.h>
#include <libc.h>
void
chars(void)
{
	char *z = "";
	char *v = "foo bar ss";
	char *e;

	e = strchr(z, 0);
	assert(e == z);
	e = strchr(z, 'z');
	assert(e == nil);
	e = strchr(v, L'z');
	assert(e == nil);
	e = strchr(v, L'a');
	assert(e == v+5);
	e = strchr(v, 0);
	assert(e == v+10);
}

void
runes(void)
{
	Rune *z = L"";
	Rune *c = L"foo βαρ ß";
	Rune *e;

	e = runestrchr(z, 0);
	assert(e == z);
	e = runestrchr(z, L'z');
	assert(e == nil);
	e = runestrchr(c, L'z');
	assert(e == nil);
	e = runestrchr(c, L'α');
	assert(e == c+5);
	e = runestrchr(c, 0);
	assert(e == c+9);
}

void
main(void)
{
	chars();
	runes();
	exits(nil);
}
