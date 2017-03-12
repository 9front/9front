#include "common.h"
#include "send.h"

/*
 *  Translate the last component of the sender address.  If the translation
 *  yields the same address, replace the sender with its last component.
 */
void
gateway(message *mp)
{
	char *base;
	String *s;

	/* first remove all systems equivalent to us */
	base = skipequiv(s_to_c(mp->sender));
	if(base != s_to_c(mp->sender)){
		s = mp->sender;
		mp->sender = s_copy(base);
		s_free(s);
	}
}
