#include "imap4d.h"

char	logfile[28]	= "imap4d";

void
debuglog(char *fmt, ...)
{
	char buf[1024];
	va_list arg;

	if(debug == 0)
		return;
	va_start(arg, fmt);
	vseprint(buf, buf + sizeof buf, fmt, arg);
	va_end(arg);
	syslog(0, logfile, "[%s:%d] %s", username, getpid(), buf);
}

void
ilog(char *fmt, ...)
{
	char buf[1024];
	va_list arg;

	va_start(arg, fmt);
	vseprint(buf, buf + sizeof buf, fmt, arg);
	va_end(arg);
	syslog(0, logfile, "[%s:%d] %s", username, getpid(), buf);

}
