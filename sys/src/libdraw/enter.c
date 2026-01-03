#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>

extern int genenter(char *ask, char *buf, int len, Mouse *m, void *c, int (*_input)(Mouse*, void*, Rune*), Screen *scr);

static int
_input(Mouse *m, void *c, Rune *k)
{
	Alt a[3] = {
		{((Mousectl*)m)->c, m, CHANRCV},
		{(Channel*)c, k, CHANRCV},
		{nil, nil, CHANEND},
	};
	return alt(a);
}

int
enter(char *ask, char *buf, int len, Mousectl *mc, Keyboardctl *kc, Screen *scr)
{
	while(nbrecv(kc->c, nil) == 1)
		;
	return genenter(ask, buf, len, mc, kc->c, _input, scr);
}
