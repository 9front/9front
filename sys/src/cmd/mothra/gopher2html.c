/*
 * Reads gopher output from a TCP port, outputs
 * html on standard output.
 * Usage: gopher2html gopher-string
 *	where gopher-string is the string sent to
 *	the gopher server to get the document.
 *
 * Gopher protocol is described in rfc1436
 */
#include <u.h>
#include <libc.h>
char *cmd;
int ifd;
void errexit(char *s, ...){
	static char buf[1024];
	char *out;
	va_list args;
	va_start(args, s);
	out = doprint(buf, buf+sizeof(buf), s, args);
	va_end(args);
	*out='\0';
	print("<head><title>%s error</title></head>\n", cmd);
	print("<body><h1>%s error</h1>\n", cmd);
	print("%s</body>\n", buf);
	exits("gopher error");
}
void wtext(char *buf, char *ebuf){
	char *bp;
	for(bp=buf;bp!=ebuf;bp++){
		if(*bp=='<' || *bp=='>' || *bp=='&' || *bp=='"'){
			if(bp!=buf) write(1, buf, bp-buf);
			buf=bp+1;
			switch(*bp){
			case '<': print("&lt;"); break;
			case '>': print("&gt;"); break;
			case '&': print("&amp;"); break;
			case '"': print("&quot;"); break;
			}
		}
	}
	if(bp!=buf) write(1, buf, bp-buf);
}
void savefile(char *name, char *type){
	int fd, n;
	char save[30], buf[1024];
	for(n=1;;n++){
		if(n==100) errexit("can't save binary file %s: %r", name);
		sprint(save, "gopher.save.%d", n);
		fd=create(save, OWRITE, 0444);
		if(fd!=-1) break;
	}
	print("<head><title>%s</title></head\n", name);
	print("<body><h1>%s</h1><p>\n", name);
	print("Saving %s file %s in <tt>%s</tt>...\n", type, name, save);
	while((n=read(ifd, buf, sizeof buf))>0) write(fd, buf, n);
	close(fd);
	print("done</body>\n");
}
void copyfile(char *title){
	char buf[1024];
	int n;
	print("<head><title>%s</title></head>\n", title);
	print("<body><h1>%s</h1><pre>\n", title);
	while((n=read(ifd, buf, sizeof buf))>0) wtext(buf, buf+n);
	print("</pre></body>\n");
}
/*
 * A directory entry contains
 *	type name selector host port
 * all tab separated, except type and name (type is one character)
 */
char ibuf[1024], *ibp, *eibuf;
#define	EOF	(-1)
int get(void){
	int n;
Again:
	if(ibp==eibuf){
		n=read(ifd, ibuf, sizeof(ibuf));
		if(n<=0) return EOF;
		eibuf=ibuf+n;
		ibp=ibuf;
	}
	if(*ibp=='\r'){
		ibp++;
		goto Again;
	}
	return *ibp++&255;
}
char *escape(char *in){
	static char out[516];
	char *op, *eop;
	eop=out+512;
	op=out;
	for(;*in;in++){
		if(op<eop){
			if(strchr("/$-_@.&!*'(),", *in)
			|| 'a'<=*in && *in<='z'
			|| 'A'<=*in && *in<='Z'
			|| '0'<=*in && *in<='9')
				*op++=*in;
			else{
				sprint(op, "%%%.2X", *in&255);
				op+=3;
			}
		}
	}
	*op='\0';
	return out;
}
void copydir(char *title){
	int type, c;
	char name[513], *ename;
	char selector[513];
	char host[513];
	char port[513];
	char *bp;
	print("<head><title>%s</title></head>\n", title);
	print("<body><h1>%s</h1><ul>\n", title);
	for(;;){
		type=get();
		if(type==EOF || type=='.') break;
		bp=name;
		while((c=get())!=EOF && c!='\t') if(bp!=&name[512]) *bp++=c;
		ename=bp;
		bp=selector;
		while((c=get())!=EOF && c!='\t') if(bp!=&selector[512]) *bp++=c;
		*bp='\0';
		bp=host;
		while((c=get())!=EOF && c!='\t') if(bp!=&host[512]) *bp++=c;
		*bp='\0';
		bp=port;
		while((c=get())!=EOF && c!='\t' && c!='\n') if(bp!=&port[512]) *bp++=c;
		while(c!=EOF && c!='\n') c=get();
		*bp='\0';
		switch(type){
		case '3':
			print("<li>");
			wtext(name, ename);
			break;
		case '7':
			print("<li><isindex action=\"gopher://%s:%s/%c%s\">",
				host, port, type, escape(selector));
			wtext(name, ename);
			break;
		default:
			print("<li><a href=\"gopher://%s:%s/%c%s\">",
				host, port, type, escape(selector));
			wtext(name, ename);
			print("</a>\n");
			break;
		}
	}
	print("</ul></body>\n");
}
int hexdigit(int c){
	if('0'<=c && c<='9') return c-'0';
	if('a'<=c && c<='f') return c-'a'+10;
	if('A'<=c && c<='F') return c-'A'+10;
	return -1;
}
void unescape(char *s){
	char *t;
	int hi, lo;
	t=s;
	while(*s){
		if(*s=='%'
		&& (hi=hexdigit(s[1]))>=0
		&& (lo=hexdigit(s[2]))>=0){
			*t++=hi*16+lo;
			s+=3;
		}
		else *t++=*s++;
	}
	*t='\0';
}
void main(int argc, char *argv[]){
	char dialstr[1024];
	char *name;
	cmd=argv[0];
	if(argc!=4) errexit("Usage: %s host port selector", argv[0]);
	sprint(dialstr, "tcp!%s!%s", argv[1], argv[2]);
	ifd=dial(dialstr, 0, 0, 0);
	if(ifd==-1) errexit("can't call %s:%s", argv[1], argv[2]);
	unescape(argv[3]);
	switch(argv[3][0]){
	case '/':
		fprint(ifd, "\r\n");
		copydir(argv[3]);
		break;
	case '\0':
		fprint(ifd, "\r\n");
		copydir(argv[1]);
		break;
	case '7':	/* index query */
		name=strchr(argv[3], '?');
		if(name!=0){
			if(name==argv[3]+1){
				argv[3][1]=argv[3][0];
				argv[3]++;
			}
			else
				*name='\t';
			name++;
		}
		else
			name=argv[3];
		fprint(ifd, "%s\r\n", argv[3]+1);
		copydir(name);
		break;
	default:
		fprint(ifd, "%s\r\n", argv[3]+1);
		name=strrchr(argv[3], '/');
		if(name==0) name=argv[3];
		else name++;
		switch(argv[3][0]){
		default:	errexit("sorry, can't handle %s (type %c)",
					argv[3]+1, argv[3][0]);
		case '0':	copyfile(name); break;
		case '1':	copydir(name); break;
		case '4':	savefile(name, "Macintosh BINHEX"); break;
		case '5':	savefile(name, "DOS binary"); break;
		case '6':	savefile(name, "uuencoded"); break;
		case '9':	savefile(name, "binary"); break;
		case 'g':	savefile(name, "GIF"); break;
		case 'I':	savefile(name, "some sort of image"); break;
		}
		break;
	}
	exits(0);
}
