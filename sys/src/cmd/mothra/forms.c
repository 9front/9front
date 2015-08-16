#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "rtext.h"
#include "mothra.h"
#include "html.h"

typedef struct Field Field;
typedef struct Option Option;
struct Form{
	int method;
	char *ctype;
	char *action;
	Field *fields, *efields;
	Form *next;
};
struct Field{
	Field *next;
	Form *form;
	char *name;
	char *value;
	int checked;
	int size;		/* should be a point, but that feature is deprecated */
	int maxlength;
	int type;
	int rows, cols;
	Option *options;
	int multiple;
	int state;		/* is the button marked? */
	Panel *p;
	Panel *pulldown;
	Panel *textwin;
};
/*
 * Field types
 */
enum{
	TYPEIN=1,
	CHECK,
	PASSWD,
	RADIO,
	SUBMIT,
	RESET,
	BUTTON,
	SELECT,
	TEXTWIN,
	HIDDEN,
	INDEX,
	FILE,
};
struct Option{
	int selected;
	int def;
	char label[NLABEL+1];
	char *value;
	Option *next;
};

#define BOUNDARY "nAboJ9uN6ZXsqoVGzLAdjKq97TWDTGjo"

void h_checkinput(Panel *, int, int);
void h_radioinput(Panel *, int, int);
void h_submitinput(Panel *, int);
void h_buttoninput(Panel *, int);
void h_fileinput(Panel *, int);
void h_submittype(Panel *, char *);
void h_submitindex(Panel *, char *);
void h_resetinput(Panel *, int);
void h_select(Panel *, int, int);
char *selgen(Panel *, int);
char *nullgen(Panel *, int);
Field *newfield(Form *form){
	Field *f;
	f=emalloc(sizeof(Field));
	if(form->efields==0)
		form->fields=f;
	else
		form->efields->next=f;
	form->efields=f;
	f->next=0;
	f->form=form;
	return f;
}
/*
 * Called by rdhtml on seeing a forms-related tag
 */
void rdform(Hglob *g){
	char *s;
	Field *f;
	Option *o, **op;
	Form *form;
	switch(g->tag){
	default:
		fprint(2, "Bad tag <%s> in rdform (Can't happen!)\n", g->token);
		return;
	case Tag_form:
		if(g->form){
			htmlerror(g->name, g->lineno, "nested forms illegal\n");
			break;
		}
		g->form=emalloc(sizeof(Form));
		s=pl_getattr(g->attr, "action");
		g->form->action=strdup((s && *s) ? s : g->dst->url->fullname);
		s=pl_getattr(g->attr, "method");
		if(s==0 || *s==0)
			g->form->method=GET;
		else if(cistrcmp(s, "post")==0)
			g->form->method=POST;
		else{
			if(cistrcmp(s, "get")!=0)
				htmlerror(g->name, g->lineno,
					"unknown form method %s\n", s);
			g->form->method=GET;
		}
		s=pl_getattr(g->attr, "enctype");
		if(s && cistrcmp(s, "multipart/form-data")==0)
			g->form->ctype = "multipart/form-data; boundary=" BOUNDARY;
		g->form->fields=0;

		g->form->next = g->dst->form;
		g->dst->form = g->form;
		break;
	case Tag_input:
	case Tag_button:
		if(g->form==0){
			/* no form, assume link button */
			form = emalloc(sizeof(Form));	
			form->method = 0;
			form->fields = 0;
			form->efields = 0;
			if(g->state->link)
				form->action = strdup(g->state->link);
			form->next = g->dst->form;
			g->dst->form = form;
			f=newfield(form);
		} else
			f=newfield(g->form);
		s=pl_getattr(g->attr, "name");
		if(s==0 || *s == 0)
			f->name=0;
		else
			f->name=strdup(s);
		s=pl_getattr(g->attr, "value");
		if(s==0)
			f->value=strdup("");
		else
			f->value=strdup(s);
		f->checked=pl_hasattr(g->attr, "checked");
		s=pl_getattr(g->attr, "size");
		if(s==0 || *s==0)
			f->size=20;
		else
			f->size=atoi(s);
		s=pl_getattr(g->attr, "maxlength");
		if(s==0 || *s==0)
			f->maxlength=0x3fffffff;
		else
			f->maxlength=atoi(s);
		s=pl_getattr(g->attr, "type");
		if((g->tag == Tag_button) && 
		   (s==0 || cistrcmp(s, "reset") || cistrcmp(s, "button")))
			s="submit";
		else if(s==0)
			s="text";
		if(cistrcmp(s, "checkbox")==0)
			f->type=CHECK;
		else if(cistrcmp(s, "radio")==0)
			f->type=RADIO;
		else if(cistrcmp(s, "submit")==0)
			f->type=SUBMIT;
		else if(cistrcmp(s, "image")==0){
			f->type=SUBMIT;
			s=pl_getattr(g->attr, "src");
			if(s && *s){
				free(g->state->image);
				g->state->image = strdup(s);
			}
			s=pl_getattr(g->attr, "width");
			if(s && *s)
				g->state->width=strtolength(g, HORIZ, s);
			s=pl_getattr(g->attr, "height");
			if(s && *s)
				g->state->height=strtolength(g, VERT, s);
			s=pl_getattr(g->attr, "alt");
			if(s==0 || *s == 0) s = f->value;
			pl_htmloutput(g, g->nsp, s, f);
			free(g->state->image);
			g->state->image = 0;
			g->state->width=0;
			g->state->height=0;
			break;
		}
		else if(cistrcmp(s, "button")==0)
			f->type=BUTTON;
		else if(cistrcmp(s, "file")==0)
			f->type=FILE;
		else if(cistrcmp(s, "reset")==0)
			f->type=RESET;
		else if(cistrcmp(s, "hidden")==0)
			f->type=HIDDEN;
		else{
			f->type=TYPEIN;
			if(cistrcmp(s, "password")==0)
				f->type=PASSWD;
			s=f->name;
			if(s && cistrcmp(s, "isindex")==0)
				f->type=INDEX;

			/*
			 * If there's exactly one attribute, use its value as the name,
			 * regardless of the attribute name.  This makes
			 * http://linus.att.com/ias/puborder.html work.
			 */
			if(s==0){
				if(g->attr[0].name && g->attr[1].name==0)
					f->name=strdup(g->attr[0].value);
				else
					f->name=strdup("no-name");
			}
		}
		if((f->type==CHECK || f->type==RADIO) && !pl_hasattr(g->attr, "value")){
			free(f->value);
			f->value=strdup("on");
		}
		if(f->type!=HIDDEN)
			pl_htmloutput(g, g->nsp, f->value[0]?f->value:"blank field", f);
		break;
	case Tag_select:
		if(g->form==0){
		BadTag:
			htmlerror(g->name, g->lineno, "<%s> not in form, ignored\n",
				tag[g->tag].name);
			break;
		}
		f=newfield(g->form);
		s=pl_getattr(g->attr, "name");
		if(s==0 || *s==0){
			f->name=strdup("select");
			htmlerror(g->name, g->lineno, "select has no name=\n");
		}
		else
			f->name=strdup(s);
		f->multiple=pl_hasattr(g->attr, "multiple");
		f->type=SELECT;
		f->size=0;
		f->options=0;
		g->text=g->token;
		g->tp=g->text;
		g->etext=g->text;
		break;
	case Tag_option:
		if(g->form==0) goto BadTag;
		if((f=g->form->efields)==0) goto BadTag;
		if(f->size<8)
			f->size++;
		o=emalloc(sizeof(Option));
		for(op=&f->options;*op;op=&(*op)->next);
		*op=o;
		o->next=0;
		g->text=o->label;
		g->tp=o->label;
		g->etext=o->label+NLABEL;
		memset(o->label, 0, NLABEL+1);
		*g->tp++=' ';
		o->def=pl_hasattr(g->attr, "selected");
		o->selected=o->def;
		if(pl_hasattr(g->attr, "disabled"))
			o->selected=0;
		s=pl_getattr(g->attr, "value");
		if(s==0)
			o->value=o->label+1;
		else
			o->value=strdup(s);
		break;
	case Tag_textarea:
		if(g->form==0) goto BadTag;
		f=newfield(g->form);
		s=pl_getattr(g->attr, "name");
		if(s==0 || *s==0){
			f->name=strdup("enter text");
			htmlerror(g->name, g->lineno, "select has no name=\n");
		}
		else
			f->name=strdup(s);
		s=pl_getattr(g->attr, "rows");
		f->rows=(s && *s)?atoi(s):8;
		s=pl_getattr(g->attr, "cols");
		f->cols=(s && *s)?atoi(s):30;
		f->type=TEXTWIN;
		/* suck up initial text */
		pl_htmloutput(g, g->nsp, f->name, f);
		break;
	case Tag_isindex:
		/*
		 * Make up a form with one tag, of type INDEX
		 * I have seen a page with <ISINDEX PROMPT="Enter a title here ">,
		 * which is nonstandard and not handled here.
		 */
		form=emalloc(sizeof(Form));
		form->fields=0;
		form->efields=0;
		s=pl_getattr(g->attr, "action");
		form->action=strdup((s && *s) ? s : g->dst->url->fullname);
		form->method=GET;
		form->fields=0;
		f=newfield(form);
		f->name=0;
		f->value=strdup("");
		f->size=20;
		f->maxlength=0x3fffffff;
		f->type=INDEX;
		pl_htmloutput(g, g->nsp, f->value[0]?f->value:"blank field", f);
		break;
	}
}
/*
 * Called by rdhtml on seeing a forms-related end tag
 */
void endform(Hglob *g){
	Field *f;

	switch(g->tag){
	case Tag_form:
		g->form=0;
		break;
	case Tag_select:
		if(g->form==0)
			htmlerror(g->name, g->lineno, "</select> not in form, ignored\n");
		else if((f=g->form->efields)==0)
			htmlerror(g->name, g->lineno, "spurious </select>\n");
		else
			pl_htmloutput(g, g->nsp, f->name, f);
		break;
	case Tag_textarea:
		break;
	}
}
char *nullgen(Panel *, int ){
	return 0;
}
char *selgen(Panel *p, int index){
	Option *a;
	Field *f;
	f=p->userp;
	if(f==0) return 0;
	for(a=f->options;index!=0 && a!=0;--index,a=a->next);
	if(a==0) return 0;
	a->label[0]=a->selected?'*':' ';
	return a->label;
}
char *seloption(Field *f){
	Option *a;
	for(a=f->options;a!=0;a=a->next)
		if(a->selected)
			return a->label+1;
	return f->name;
}
void mkfieldpanel(Rtext *t){
	Action *a;
	Panel *win, *scrl;
	Field *f;

	if((a = t->user) == nil)
		return;
	if((f = a->field) == nil)
		return;

	f->p=0;
	switch(f->type){
	case TYPEIN:
		f->p=plentry(0, 0, f->size*chrwidth, f->value, h_submittype);
		break;
	case PASSWD:
		f->p=plentry(0, USERFL, f->size*chrwidth, f->value, h_submittype);
		break;
	case CHECK:
		f->p=plcheckbutton(0, 0, "", h_checkinput);
		f->state=f->checked;
		plsetbutton(f->p, f->checked);
		break;
	case RADIO:
		f->p=plradiobutton(0, 0, "", h_radioinput);
		f->state=f->checked;
		plsetbutton(f->p, f->checked);
		break;
	case SUBMIT:
		f->p=plbutton(0, 0, f->value[0]?f->value:"submit", h_submitinput);
		break;
	case RESET:
		f->p=plbutton(0, 0, f->value[0]?f->value:"reset", h_resetinput);
		break;
	case BUTTON:
		f->p=plbutton(0, 0, f->value[0]?f->value:"button", h_buttoninput);
		break;
	case FILE:
		f->p=plbutton(0, 0, f->value[0]?f->value:"file", h_fileinput);
		break;
	case SELECT:
		if(f->size <= 0)
			f->size=1;
		f->pulldown=plgroup(0,0);
		scrl=plscrollbar(f->pulldown, PACKW|FILLY);
		win=pllist(f->pulldown, PACKN, nullgen, f->size, h_select);
		win->userp=f;
		plinitlist(win, PACKN, selgen, f->size, h_select);
		plscroll(win, 0, scrl);
		plpack(f->pulldown, Rect(0,0,1024,1024));
		f->p=plpulldown(0, FIXEDX, seloption(f), f->pulldown, PACKS);
		f->p->fixedsize.x=f->pulldown->r.max.x-f->pulldown->r.min.x;
		break;
	case TEXTWIN:
		f->p=plframe(0,0);
		pllabel(f->p, PACKN|FILLX, f->name);
		scrl=plscrollbar(f->p, PACKW|FILLY);
		f->textwin=pledit(f->p, EXPAND, Pt(f->cols*chrwidth, f->rows*font->height), 0, 0, 0);
		f->textwin->userp=f;
		plscroll(f->textwin, 0, scrl);
		break;
	case INDEX:
		f->p=plentry(0, 0, f->size*chrwidth, f->value, h_submitindex);
		break;
	}
	if(f->p){
		f->p->userp=f;
		free(t->text);
		t->text=0;
		t->p=f->p;
		t->flags|=PL_HOT;
	}
}
void h_checkinput(Panel *p, int, int v){
	((Field *)p->userp)->state=v;
}
void h_radioinput(Panel *p, int, int v){
	Field *f, *me;
	me=p->userp;
	me->state=v;
	if(v){
		for(f=me->form->fields;f;f=f->next)
			if(f->type==RADIO && f!=me && strcmp(f->name, me->name)==0){
				plsetbutton(f->p, 0);
				f->state=0;
				pldraw(f->p, screen);
			}
	}
}
void h_select(Panel *p, int, int index){
	Option *a;
	Field *f;
	f=p->userp;
	if(f==0) return;
	if(!f->multiple) for(a=f->options;a;a=a->next) a->selected=0;
	for(a=f->options;index!=0 && a!=0;--index,a=a->next);
	if(a==0) return;
	a->selected=!a->selected;
	plinitpulldown(f->p, FIXEDX, seloption(f), f->pulldown, PACKS);
	pldraw(f->p, screen);
}
void h_resetinput(Panel *p, int){
	Field *f;
	Option *o;
	for(f=((Field *)p->userp)->form->fields;f;f=f->next) switch(f->type){
	case TYPEIN:
		plinitentry(f->p, 0, f->size*chrwidth, f->value, 0);
		break;
	case PASSWD:
		plinitentry(f->p, USERFL, f->size*chrwidth, f->value, 0);
		break;
	case FILE:
		free(f->value);
		f->value=strdup("");
		if(f->p==nil) break;
		f->p->state=0;
		pldraw(f->p, screen);
		break;
	case CHECK:
	case RADIO:
		f->state=f->checked;
		plsetbutton(f->p, f->checked);
		break;
	case SELECT:
		for(o=f->options;o;o=o->next)
			o->selected=o->def;
		break;
	}
	pldraw(text, screen);
}

void h_buttoninput(Panel *p, int){
	Field *f;

	f = p->userp;
	if(f && f->form && f->form->method != POST && f->form->action)
		geturl(f->form->action, -1, 0, 0);
}

void h_fileinput(Panel *p, int){
	char name[NNAME];
	Field *f;

	f = p->userp;
	nstrcpy(name, f->value, sizeof(name));
	for(;;){
		if(eenter("Upload file", name, sizeof(name), &mouse) <= 0)
			break;
		if(access(name, AREAD) == 0)
			break;
	}
	free(f->value);
	f->value = strdup(name);
	p->state = name[0] != 0;
	pldraw(f->p, screen);
}

/*
 * If there's exactly one button with type=text, then
 * a CR in the button is supposed to submit the form.
 */
void h_submittype(Panel *p, char *){
	int ntype;
	Field *f;
	ntype=0;
	for(f=((Field *)p->userp)->form->fields;f;f=f->next)
		if(f->type==TYPEIN || f->type==PASSWD)
			ntype++;
	if(ntype==1) h_submitinput(p, 0);
}
void h_submitindex(Panel *p, char *){
	h_submitinput(p, 0);
}

void mencodeform(Form *form, int fd){
	char *b, *p, *sep;
	int ifd, n, nb;
	Option *o;
	Field *f;
	Rune *rp;

	sep = "--" BOUNDARY;
	for(f=form->fields;f;f=f->next)switch(f->type){
	case TYPEIN:
	case PASSWD:
		fprint(fd, "%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s",
			sep, f->name, plentryval(f->p));
		sep = "\r\n--" BOUNDARY;
		break;
	case CHECK:
	case RADIO:
	case SUBMIT:
		if(!f->state) break;
	case HIDDEN:
		if(f->name==0 || f->value==0)
			break;
		fprint(fd, "%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s",
			sep, f->name, f->value);
		sep = "\r\n--" BOUNDARY;
		break;
	case SELECT:
		if(f->name==0) break;
		for(o=f->options;o;o=o->next)
			if(o->selected && o->value){
				fprint(fd, "%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s",
					sep, f->name, o->value);
				sep = "\r\n--" BOUNDARY;
			}
		break;
	case TEXTWIN:
		if(f->name==0) break;
		n=plelen(f->textwin);
		rp=pleget(f->textwin);
		p=b=malloc(UTFmax*n+1);
		if(b == nil)
			break;
		while(n > 0){
			p += runetochar(p, rp);
			rp++;
			n--;
		}
		*p = 0;
		fprint(fd, "%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s",
			sep, f->name, b);
		sep = "\r\n--" BOUNDARY;
		free(b);
		break;
	case FILE:
		if(f->name==0 || f->value[0]==0)
			break;
		if(p = strrchr(f->value, '/'))
			p++;
		if(p == 0 || *p == 0)
			p = f->value;
		if((b = malloc(nb = 8192)) == nil)
			break;
		if((ifd = open(f->value, OREAD)) >= 0){
			if(filetype(ifd, b, nb) < 0)
				strcpy(b, "application/octet-stream");
			fprint(fd, "%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\""
				"\r\nContent-Type: %s\r\n\r\n", sep, f->name, p, b);
			sep = "\r\n--" BOUNDARY;
			while((n = read(ifd, b, nb)) > 0)
				if(write(fd, b, n) != n)
					break;
			close(ifd);
		}
		free(b);
		break;
	}
	fprint(fd, "%s--\r\n", sep);
}

void uencodeform(Form *form, int fd){
	char *b, *p, *sep;
	Option *o;
	Field *f;
	Rune *rp;
	int n;

	sep = "";
	for(f=form->fields;f;f=f->next) switch(f->type){
	case TYPEIN:
	case PASSWD:
		fprint(fd, "%s%U=%U", sep, f->name, plentryval(f->p));
		sep = "&";
		break;
	case INDEX:
		fprint(fd, "%s%U", sep, plentryval(f->p));
		sep = "&";
		break;
	case CHECK:
	case RADIO:
	case SUBMIT:
		if(!f->state) break;
	case HIDDEN:
		if(f->name==0 || f->value==0)
			break;
		fprint(fd, "%s%U=%U", sep, f->name, f->value);
		sep = "&";
		break;
	case SELECT:
		if(f->name==0) break;
		for(o=f->options;o;o=o->next)
			if(o->selected && o->value){
				fprint(fd, "%s%U=%U", sep, f->name, o->value);
				sep = "&";
			}
		break;
	case TEXTWIN:
		if(f->name==0) break;
		n=plelen(f->textwin);
		rp=pleget(f->textwin);
		p=b=malloc(UTFmax*n+1);
		if(b == nil)
			break;
		while(n > 0){
			p += runetochar(p, rp);
			rp++;
			n--;
		}
		*p = 0;
		fprint(fd, "%s%U=%U", sep, f->name, b);
		sep = "&";
		free(b);
		break;
	}
}

void h_submitinput(Panel *p, int){
	char buf[NNAME];
	Form *form;
	Field *f;
	int n, fd;

	f = p->userp;
	form=f->form;
	for(f=form->fields;f;f=f->next)
		if(f->type==SUBMIT)
			f->state = (f->p == p);

	switch(form->method){
	case GET:
		strcpy(buf, "/tmp/mfXXXXXXXXXXX");
		fd = create(mktemp(buf), ORDWR|ORCLOSE, 0600);
		break;
	case POST:
		fd = urlpost(selurl(form->action), form->ctype);
		break;
	default:
		return;
	}

	if(fd < 0){
		message("submit: %r");
		return;
	}
	if(form->method==GET){
		fprint(fd, "%s?", form->action);
		uencodeform(form, fd);
		seek(fd, 0, 0);
		n = readn(fd, buf, sizeof(buf));
		close(fd);
		if(n < 0 || n >= sizeof(buf)){
			message("submit: too large");
			return;
		}
		buf[n] = 0;
		geturl(buf, -1, 0, 0);
	} else {
		/* only set for multipart/form-data */
		if(form->ctype)
			mencodeform(form, fd);
		else
			uencodeform(form, fd);
		geturl(form->action, fd, 0, 0);
	}
}

void freeform(void *p)
{
	Form *form;
	Field *f;
	Option *o;

	while(form = p){
		p = form->next;
		free(form->action);
		while(f = form->fields){
			form->fields = f->next;

			if(f->p!=0)
				plfree(f->p);

			free(f->name);
			free(f->value);

			while(o = f->options){
				f->options = o->next;
				if(o->value != o->label+1)
					free(o->value);
				free(o);
			}

			free(f);
		}
		free(form);
	}
}

int Ufmt(Fmt *f){
	char *s = va_arg(f->args, char*);
	for(; *s; s++){
		if(strchr("/$-_@.!*'(),", *s)
		|| 'a'<=*s && *s<='z'
		|| 'A'<=*s && *s<='Z'
		|| '0'<=*s && *s<='9')
			fmtprint(f, "%c", *s);
		else if(*s==' ')
			fmtprint(f, "+");
		else
			fmtprint(f, "%%%.2X", *s & 0xFF);
	}
	return 0;
}
