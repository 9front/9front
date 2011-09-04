#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include <ctype.h>
#include "mothra.h"
enum{
	IP=1,		/* url can contain //ipaddress[:port] */
	REL=2,	/* fill in ip address & root of name from current, if necessary */
	SSL=4,	/* use SSL/TLS encryption */
};
Scheme scheme[]={
	"http:",	HTTP,	IP|REL,		80,
	"https:",	HTTP,	IP|REL|SSL,	443,
	"ftp:",		FTP,	IP|REL,		21,
	"file:",	FILE,	REL,	0,
	"telnet:",	TELNET,	IP,	0,
	"mailto:",	MAILTO,	0,	0,
	"gopher:",	GOPHER,	IP,	70,
	0,		HTTP,	IP|REL,	80,
};
int endaddr(int c){
	return c=='/' || c==':' || c=='?' || c=='#' || c=='\0';
}
/*
 * Remove ., mu/.. and empty components from path names.
 * Empty last components of urls are significant, and
 * therefore preserved.
 */
void urlcanon(char *name){
	char *s, *t;
	char **comp, **p, **q;
	int rooted;
	rooted=name[0]=='/';
	/*
	 * Break the name into a list of components
	 */
	comp=emalloc((strlen(name)+2)*sizeof(char *));
	p=comp;
	*p++=name;
	for(s=name;;s++){
		if(*s=='/'){
			*p++=s+1;
			*s='\0';
		}
		else if(*s=='\0' || *s=='?')
			break;
	}
	*p=0;
	/*
	 * go through the component list, deleting components that are empty (except
	 * the last component) or ., and any .. and its non-.. predecessor.
	 */
	p=q=comp;
	while(*p){
		if(strcmp(*p, "")==0 && p[1]!=0
		|| strcmp(*p, ".")==0)
			p++;
		else if(strcmp(*p, "..")==0 && q!=comp && strcmp(q[-1], "..")!=0){
			--q;
			p++;
		}
		else
			*q++=*p++;
	}
	*q=0;
	/*
	 * rebuild the path name
	 */
	s=name;
	if(rooted) *s++='/';
	for(p=comp;*p;p++){
		t=*p;
		while(*t) *s++=*t++;
		if(p[1]!=0) *s++='/';
	}
	*s='\0';
	free(comp);
}
/*
 * True url parsing is a nightmare.
 * This assumes that there are two basic syntaxes
 * for url's -- with and without an ip address.
 * If the type identifier or the ip address and port number
 * or the relative address is missing from urlname or is empty, 
 * it is copied from cur.
 */
void crackurl(Url *url, char *urlname, Url *cur){
	char *relp, *tagp, *httpname;
	int len;
	Scheme *up;
	char buf[30];
	/*
	 * The following lines `fix' the most egregious urlname syntax errors
	 */
	while(*urlname==' ' || *urlname=='\t' || *urlname=='\n') urlname++;
	relp=strchr(urlname, '\n');
	if(relp) *relp='\0';
	/*
	 * In emulation of Netscape, attach a free "http://"
	 * to names beginning with "www.".
	 */
	if(strncmp(urlname, "www.", 4)==0){
		httpname=emalloc(strlen(urlname)+8);
		strcpy(httpname, "http://");
		strcat(httpname, urlname);
		crackurl(url, httpname, cur);
		free(httpname);
		return;
	}
	url->port=cur->port;
	strncpy(url->ipaddr, cur->ipaddr, sizeof(url->ipaddr));
	strncpy(url->reltext, cur->reltext, sizeof(url->reltext));
	if(strchr(urlname, ':')==0){
		up=cur->scheme;
		if(up==0){
			up=&scheme[0];
			cur->scheme=up;
		}
	}
	else{
		for(up=scheme;up->name;up++){
			len=strlen(up->name);
			if(strncmp(urlname, up->name, len)==0){
				urlname+=len;
				break;
			}
		}
		if(up->name==0) up=&scheme[0];	/* default to http: */
	}
	url->access=up->type;
	url->scheme=up;
	if(up!=cur->scheme)
		url->reltext[0]='\0';
	if(up->flags&IP && strncmp(urlname, "//", 2)==0){
		urlname+=2;
		for(relp=urlname;!endaddr(*relp);relp++);
		len=relp-urlname;
		strncpy(url->ipaddr, urlname, len);
		url->ipaddr[len]='\0';
		urlname=relp;
		if(*urlname==':'){
			urlname++;
			url->port=atoi(urlname);
			while(!endaddr(*urlname)) urlname++;
		}
		else
			url->port=up->port;
		if(*urlname=='\0') urlname="/";
	}
	url->ssl = up->flags&SSL;
		
	tagp=strchr(urlname, '#');
	if(tagp){
		*tagp='\0';
		strncpy(url->tag, tagp+1, sizeof(url->tag));
	}
	else
		url->tag[0]='\0';	
	if(!(up->flags&REL) || *urlname=='/')
		strncpy(url->reltext, urlname, sizeof(url->reltext));
	else if(urlname[0]){
		relp=strrchr(url->reltext, '/');
		if(relp==0)
			strncpy(url->reltext, urlname, sizeof(url->reltext));
		else
			strcpy(relp+1, urlname);
	}
	urlcanon(url->reltext);
	if(tagp) *tagp='#';
	/*
	 * The following mess of strcpys and strcats
	 * can't be changed to a few sprints because
	 * urls are not necessarily composed of legal utf
	 */
	strcpy(url->fullname, up->name);
	if(up->flags&IP){
		strncat(url->fullname, "//", sizeof(url->fullname));
		strncat(url->fullname, url->ipaddr, sizeof(url->fullname));
		if(url->port!=up->port){
			snprint(buf, sizeof(buf), ":%d", url->port);
			strncat(url->fullname, buf, sizeof(url->fullname));
		}
	}
	strcat(url->fullname, url->reltext);
	url->map=0;
}
