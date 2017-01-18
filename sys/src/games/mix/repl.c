#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <avl.h>
#include "mix.h"

int
getf(char *line)
{
	long a, b;

	if(*line == '\0')
		return 5;
	if(*line++ != '(') 
		return -1;
	a = strtol(line, &line, 10);
	if(*line++ != ':')
		return -1;
	b = strtol(line, &line, 10);
	if(*line != ')')
		return -1;
	return F(a, b);
}	

int
disp(char *line)
{
	int f;
	long m;

	if(setjmp(errjmp) == 1)
		return -1;
	m = strtol(line, &line, 10);
	if((f = getf(line)) == -1)
		return -1;
	print("%d\n", V(cells[m], f));
	return 0;
}

int
dispreg(char *line)
{
	vlong rax;
	char c;
	int i, f;
	u32int reg;

	if(setjmp(errjmp) == 1)
		return -1;

	switch(c = *line++) {
	case 'a':
		if(*line == 'x') {
			rax = ra & MASK5;
			rax <<= 5 * BITS;
			rax |= rx & MASK5;
			if(ra >> 31)
				rax = -rax;
			print("%lld\n", rax);
			return 0;
		} else
			reg = ra;
		break;
	case 'x':
		reg = rx;
		break;
	case 'j':
		reg = ri[0];
		break;
	default:
		if(!isdigit(c))
			return -1;
		i = c - '0';
		if(i < 1 || i > 6)
			return -1;
		reg = ri[i];
	}

	if((f = getf(line)) == -1)
		return -1;

	print("%d\n", V(reg, f));
	return 0;
}

int
breakp(char *line)
{
	long l;

	line = strskip(line);
	if(!isdigit(*line))
		return -1;
	l = strtol(line, nil, 10);
	if(l < 0 || l > 4000)
		return -1;
	bp[l] ^= 1;
	return 0;
}

int
asm(char *l)
{
	char *file;

	if(yydone) {
		print("Assembly complete\n");
		return 0;
	}
	l = strskip(l);
	if(*l++ == '<') {
		Bterm(&bin);
		l = strskip(l);
		file = estrdup(strim(l));
		if(asmfile(file) == -1) {
			free(file);
			return -1;
		}
		Binit(&bin, 0, OREAD);
		free(file);
		return 0;
	}

	line = 1;
	filename = "<stdin>";
	if(setjmp(errjmp) == 0)
		yyparse();
	Bterm(&bin);
	Binit(&bin, 0, OREAD);
	return 0;
}

int
disasm(char *line)
{
	long l;

	line = strskip(line);
	if(!isdigit(*line))
		return -1;

	l = strtol(line, nil, 10);
	if(l < 0 || l > 4000)
		return -1;
	print("%I\n", cells[l]);
	return 0;
}

void
mixprint(int m, int words)
{
	int i;
	u32int *wp, w;
	Rune buf[6], *rp;

	wp = cells+m;
	while(words-- > 0) {
		rp = buf;
		w = *wp++;
		for(i = 4; i > -1; i--)
			*rp++ = mixtorune(w>>i*BITS & MASK1);
		*rp = '\0';
		print("%S", buf);
	}
	print("\n");
}

int
out(char *line)
{
	long l, i;

	line = strskip(line);
	i = 1;
	if(*line == '(') {
		l = strtol(strskip(line+1), &line, 10);
		line = strskip(line);
		if(*line != ',')
			return -1;
		i = strtol(strskip(line+1), &line, 10);
		line = strskip(line);
		if(*line != ')')
			return -1;
	} else {
		if(!isdigit(*line))
			return -1;
		l = strtol(line, nil, 10);
	}
	mixprint(l, i);
	return 0;
}

void
clearsyms(Sym *s)
{
	if(s == nil)
		return;

	clearsyms((Sym*)s->c[0]);
	clearsyms((Sym*)s->c[1]);
	free(s);
}

void
repl(int go)
{
	char *line, c;
	int len, once;
		

	Binit(&bin, 0, OREAD);

	if(go && vmstart != -1) {
		once = 0;
		goto Go;
	}

	for(;;) {
		print("MIX ");

		if((line = Brdline(&bin, '\n')) == nil)
			return;

		if((len = Blinelen(&bin)) == 1)
			continue;

		line[len-1] = '\0';

		once = 0;
		switch(c = line[0]) {
		Err:
			print("?\n");
			break;
		default:
			if(!isdigit(c)) 
				goto Err;
			if(disp(line) == -1)
				goto Err;
			break;
		case 'a':
			if(asm(line+1) == -1)
				goto Err;
			break;
		case 'b':
			if(breakp(line+1) == -1)
				goto Err;
			break;
		case 'c':
			ra = rx = ri[0] = ri[1] = ri[2] = ri[3] = ri[4] = ri[5] = ri[6] = 0;
			memset(cells, 0, sizeof(cells));
			vmstart = -1;
			yydone = 0;
			clearsyms((Sym*)syms->root);
			syms->root = nil;
			sinit();
			break;
		case 'd':
			if(disasm(line+1) == -1)
				goto Err;
			break;
		case 'o':
			if(out(line+1) == -1)
				goto Err;
			break;
		case 'r':
			if(dispreg(line+1) == -1)
				goto Err;
			break;
		case 's':
			once = 1;
		case 'g':
		Go:
			if(vmstart == -1)
				goto Err;
			if(setjmp(errjmp) == 0)
				vmstart = mixvm(vmstart, once);
			else
				break;
			if(go)
				exits(nil);
			if(vmstart == -1)
				print("halted\n");
			else
				print("at %d:\t%I\n", vmstart, cells[vmstart]);
			break;
		case 'x':
			return;
		}
	}
}
