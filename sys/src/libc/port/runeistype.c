#include <u.h>
#include <libc.h>

#include "runeistypedata"

int
isspacerune(Rune c)
{
	return (mergedlkup(c) & Lspace) == Lspace;
}

int
isalpharune(Rune c)
{
	return (mergedlkup(c) & Lalpha) == Lalpha;
}

int
isdigitrune(Rune c)
{
	return (mergedlkup(c) & Ldigit) == Ldigit;
}

int
isupperrune(Rune c)
{
	return (mergedlkup(c) & Lupper) == Lupper;
}

int
islowerrune(Rune c)
{
	return (mergedlkup(c) & Llower) == Llower;
}

int
istitlerune(Rune c)
{
	return (mergedlkup(c) & Ltitle) == Ltitle;
}
