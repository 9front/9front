#include	<u.h>
#include	<libc.h>
#include	<keyboard.h>
#include	"compat.h"
#include	"kbd.h"
#include   "ksym2utf.h"

enum {
	VKSpecial = 0xff00,
};

static Rune vnckeys[] =
{
[0x00]	0,	0,	0,	0,	0,	0,	0,	0,
[0x08]	'\b',	'\t',	'\r',	0,	0,	'\n',	0,	0,
[0x10]	0,	0,	0,	0,	Kscroll,0,	0,	0,
[0x18]	0,	0,	0,	Kesc,	0,	0,	0,	0,
[0x20]	0,	0,	0,	0,	0,	0,	0,	0,
[0x28]	0,	0,	0,	0,	0,	0,	0,	0,
[0x30]	0,	0,	0,	0,	0,	0,	0,	0,
[0x38]	0,	0,	0,	0,	0,	0,	0,	0,
[0x40]	0,	0,	0,	0,	0,	0,	0,	0,
[0x48]	0,	0,	0,	0,	0,	0,	0,	0,
[0x50]	Khome,	Kleft,	Kup,	Kright,	Kdown,	Kpgup,	Kpgdown,Kend,
[0x58]	0,	0,	0,	0,	0,	0,	0,	0,
[0x60]	0,	Kprint,	0,	Kins,	0,	0,	0,	0,
[0x68]	0,	0,	0,	Kbreak,	0,	0,	0,	0,
[0x70]	0,	0,	0,	0,	0,	0,	0,	0,
[0x78]	0,	0,	0,	0,	0,	0,	0,	Knum,
[0x80]	0,	0,	0,	0,	0,	0,	0,	0,
[0x88]	0,	0,	0,	0,	0,	0,	0,	0,
[0x90]	0,	0,	0,	0,	0,	0,	0,	0,
[0x98]	0,	0,	0,	0,	0,	0,	0,	0,
[0xa0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xa8]	0,	0,	'*',	'+',	0,	'-',	'.',	'/',
[0xb0]	'0',	'1',	'2',	'3',	'4',	'5',	'6',	'7',
[0xb8]	'8',	'9',	0,	0,	0,	'=',	0,	0,
[0xc0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xc8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xd0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xd8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xe0]	0,	Kshift,	Kshift,	Kctl,	Kctl,	Kcaps,	Kcaps,	0,
[0xe8]	0,	Kalt,	Kalt,	0,	0,	0,	0,	0,
[0xf0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xf8]	0,	0,	0,	0,	0,	0,	0,	Kdel,
};

/*
 *  keyboard interrupt
 */
void
vncputc(int keyup, int c)
{
	char buf[16];

	/*
 	 *  character mapping
	 */
	if((c & VKSpecial) == VKSpecial){
		c = vnckeys[c & 0xff];
		if(c == 0)
			return;
	}
	/*
	 * map an xkeysym onto a utf-8 char
	 */
	if((c & 0xff00) && c < nelem(ksym2utf) && ksym2utf[c] != 0)
		c = ksym2utf[c];
	snprint(buf, sizeof(buf), "r%C", c);
	if(keyup)
		buf[0] = 'R';
	if(kbdin >= 0)
		write(kbdin, buf, strlen(buf)+1);
}
