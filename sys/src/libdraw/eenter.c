#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <keyboard.h>

extern int genenter(char *ask, char *buf, int len, Mouse *m, void *kc, int (*_input)(Mouse*, void*, Rune*), Screen *scr);

static int
_input(Mouse *m, void *, Rune *k)
{
	Event ev;

	switch(eread(Ekeyboard|Emouse, &ev)){
	case Emouse:
		*m = ev.mouse;
		return 0;
	case Ekeyboard:
		*k = ev.kbdc;
		return 1;
	}
	return -1;
}

int
eenter(char *ask, char *buf, int len, Mouse *m)
{
	while(ecankbd())
		ekbd();
	return genenter(ask, buf, len, m, nil, _input, nil);
}
