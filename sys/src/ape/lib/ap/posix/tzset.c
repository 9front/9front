#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

static char std[32] = "GMT0";
static char dst[32];
char *tzname[2] = {
	std, dst
};
long timezone;
long altzone;
int daylight;

void
tzset(void)
{
	char *env, *p, *q;
	
	if((p = getenv("timezone")) == 0)
		goto error;
	if((env = malloc(strlen(p) + 1)) == 0)
		goto error;
	strcpy(env, p);
	if((p = strchr(env, ' ')) == 0)
		goto error;
	*p = 0;
	strncpy(std, env, sizeof std);
	q = p + 1;
	if((p = strchr(q, ' ')) == 0)
		goto error;
	timezone = - atoi(q);
	q = p + 1;
	if((p = strchr(q, ' ')) == 0)
		goto nodst;
	*p = 0;
	strncpy(dst, q, sizeof dst);
	q = p + 1;
	altzone = - atoi(q);
	daylight = 1;
	free(env);
	return;

error:
	strcpy(std, "GMT0");
	dst[0] = '\0';
	timezone = 0;
	altzone = 0;
	daylight = 0;
	if(env != 0)
		free(env);
	return;

nodst:
	dst[0] = '\0';
	daylight = 0;
	altzone = timezone;
	free(env);
	return;
}
