/* posix */
#include <sys/types.h>
#include <unistd.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

char*
gai_strerror(int err)
{
	static char *tab[] = {
		/* 0 */			"No error",
		/* EAI_BADFLAGS */	"Invalid value for `ai_flags' field",
		/* EAI_NONAME */	"NAME or SERVICE is unknown",
		/* EAI_AGAIN */		"Temporary failure in name resolution",
		/* EAI_FAIL */		"Non-recoverable failure in name resolution",
		/* EAI_NODATA */	"No address associated with NAME",
		/* EAI_FAMILY */	"`ai_family' not supported",
		/* EAI_SOCKTYPE */	"`ai_socktype' not supported",
		/* EAI_SERVICE */	"SERVICE not supported for `ai_socktype'",
		/* EAI_ADDRFAMILY */	"Address family for NAME not supported",
		/* EAI_MEMORY */	"Memory allocation failure",
		/* EAI_SYSTEM */	"System error returned in `errno'",
		/* EAI_OVERFLOW */	"Argument buffer overflow",
	};

	err = -err;
	if(err < 0 || err >= (sizeof(tab)/sizeof(tab[0])))
		return "Unknown error";
	return tab[err];
}
