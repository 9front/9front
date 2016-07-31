#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>
#include "authcmdlib.h"

void
getpass(Authkey *key, char *pass, int check, int confirm)
{
	char buf[PASSWDLEN], *s, *err;

	for(;; memset(s, 0, strlen(s)), free(s)){
		s = readcons("Password", nil, 1);
		if(s == nil)
			break;
		if(check){
			if(err = okpasswd(s)){
				print("%s, try again\n", err);
				continue;
			}
		}
		if(strlen(s) >= sizeof(buf)){
			print("password longer than %d characters\n", sizeof(buf)-1);
			continue;
		}
		strcpy(buf, s);
		memset(s, 0, strlen(s));
		free(s);
		if(confirm){
			s = readcons("Confirm password", nil, 1);
			if(s == nil)
				break;
			if(strcmp(s, buf) != 0){
				print("mismatch, try again\n");
				continue;
			}
			memset(s, 0, strlen(s));
			free(s);
		}
		if(key)
			passtokey(key, buf);
		if(pass)
			strcpy(pass, buf);
		memset(buf, 0, sizeof(buf));
		return;
	}
	error("no password");
}
