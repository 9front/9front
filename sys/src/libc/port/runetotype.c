#include <u.h>
#include <libc.h>

#include "runetotypedata"

Rune
toupperrune(Rune c)
{
	return c + upperlkup(c);
}

Rune
tolowerrune(Rune c)
{
	return c + lowerlkup(c);
}

Rune
totitlerune(Rune c)
{
	return c + titlelkup(c);
}
