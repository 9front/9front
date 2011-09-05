/*
 * type=image is treated like submit
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "mothra.h"
#include "html.h"
typedef struct Field Field;
typedef struct Option Option;
struct Form{
	int method;
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
	SELECT,
	TEXTWIN,
	HIDDEN,
	INDEX,
};
struct Option{
	int selected;
	int def;
	char label[NLABEL+1];
	char *value;
	Option *next;
};
void h_checkinput(Panel *, int, int);
void h_radioinput(Panel *, int, int);
void h_submitinput(Panel *, int);
void h_submittype(Panel *, char *);
void h_submitindex(Panel *, char *);
void h_resetinput(Panel *, int);
void h_select(Panel *, int, int);
void h_cut(Panel *, int);
void h_paste(Panel *, int);
void h_snarf(Panel *, int);
void h_edit(Panel *);
char *selgen(Panel *, int);
char *nullgen(Panel *, int);
Field *newfield(Form *form){
	Field *f;
	f=emallocz(sizeof(Field), 1);
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
		g->form=emallocz(sizeof(Form), 1);
		s=pl_getattr(g->attr, "action");
		g->form->action=strdup((s && s[0]) ? s : g->dst->url->fullname);
		s=pl_getattr(g->attr, "method");
		if(s==0)
			g->form->method=GET;
		else if(cistrcmp(s, "post")==0)
			g->form->method=POST;
		else{
			if(cistrcmp(s, "get")!=0)
				htmlerror(g->name, g->lineno,
					"unknown form method %s\n", s);
			g->form->method=GET;
		}
		g->form->fields=0;

		g->form->next = g->dst->form;
		g->dst->form = g->form;

		break;
	case Tag_input:
		if(g->form==0){
		BadTag:
			htmlerror(g->name, g->lineno, "<%s> not in form, ignored\n",
				tag[g->tag].name);
			break;
		}
		f=newfield(g->form);
		s=pl_getattr(g->attr, "name");
		if(s==0)
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
		if(s==0)
			f->size=20;
		else
			f->size=atoi(s);
		s=pl_getattr(g->attr, "maxlength");
		if(s==0)
			f->maxlength=0x3fffffff;
		else
			f->maxlength=atoi(s);
		s=pl_getattr(g->attr, "type");
		/* bug -- password treated as text */
		if(s==0 || cistrcmp(s, "text")==0 || cistrcmp(s, "password")==0 || cistrcmp(s, "int")==0){
			s=pl_getattr(g->attr, "name");
			if(s!=0 && strcmp(s, "isindex")==0)
				f->type=INDEX;
			else
				f->type=TYPEIN;
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
		else if(cistrcmp(s, "checkbox")==0)
			f->type=CHECK;
		else if(cistrcmp(s, "radio")==0)
			f->type=RADIO;
		else if(cistrcmp(s, "submit")==0)
			f->type=SUBMIT;
		else if(cistrcmp(s, "image")==0){
			/* presotto's egregious hack to make image submits do something */
			if(f->name){
				free(f->name);
				f->name=0;
			}
			f->type=SUBMIT;
		} else if(cistrcmp(s, "reset")==0)
			f->type=RESET;
		else if(cistrcmp(s, "hidden")==0)
			f->type=HIDDEN;
		else{
			htmlerror(g->name, g->lineno, "bad field type %s, ignored", s);
			break;
		}
		if((f->type==CHECK || f->type==RADIO) && !pl_hasattr(g->attr, "value")){
			free(f->value);
			f->value=strdup("on");
		}
		if(f->type!=HIDDEN)
			pl_htmloutput(g, g->nsp, f->value[0]?f->value:"blank field", f);
		break;
	case Tag_select:
		if(g->form==0) goto BadTag;
		f=newfield(g->form);
		s=pl_getattr(g->attr, "name");
		if(s==0){
			f->name=strdup("select");
			htmlerror(g->name, g->lineno, "select has no name=\n");
		}
		else
			f->name=strdup(s);
		s=pl_getattr(g->attr, "size");
		if(s==0) f->size=4;
		else{
			f->size=atoi(s);
			if(f->size<=0) f->size=1;
		}
		f->multiple=pl_hasattr(g->attr, "multiple");
		f->type=SELECT;
		f->options=0;
		g->text=g->token;
		g->tp=g->text;
		g->etext=g->text;
		break;
	case Tag_option:
		if(g->form==0) goto BadTag;
		f=g->form->efields;
		o=emallocz(sizeof(Option), 1);
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
		if(s==0){
			f->name=strdup("enter text");
			htmlerror(g->name, g->lineno, "select has no name=\n");
		}
		else
			f->name=strdup(s);
		s=pl_getattr(g->attr, "rows");
		f->rows=s?atoi(s):8;
		s=pl_getattr(g->attr, "cols");
		f->cols=s?atoi(s):30;
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
		form->action=strdup((s && s[0]) ? s : g->dst->url->fullname);
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
	switch(g->tag){
	case Tag_form:
		g->form=0;
		break;
	case Tag_select:
		if(g->form==0)
			htmlerror(g->name, g->lineno, "</select> not in form, ignored\n");
		else
			pl_htmloutput(g, g->nsp, g->form->efields->name,g->form->efields);
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
	Panel *win, *scrl, *menu, *pop, *button;
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
	case SELECT:
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
		menu=plgroup(0,0);
		f->p=plframe(0,0);
		pllabel(f->p, PACKN|FILLX, f->name);
		scrl=plscrollbar(f->p, PACKW|FILLY);
		pop=plpopup(f->p, PACKN|FILLX, 0, menu, 0);
		f->textwin=pledit(pop, EXPAND, Pt(f->cols*chrwidth, f->rows*font->height),
			0, 0, h_edit);
		f->textwin->userp=f;
		button=plbutton(menu, PACKN|FILLX, "cut", h_cut);
		button->userp=f->textwin;
		button=plbutton(menu, PACKN|FILLX, "paste", h_paste);
		button->userp=f->textwin;
		button=plbutton(menu, PACKN|FILLX, "snarf", h_snarf);
		button->userp=f->textwin;
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
		t->hot=1;
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
	case PASSWD:
		plinitentry(f->p, 0, f->size*chrwidth, f->value, 0);
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
void h_edit(Panel *p){
	plgrabkb(p);
}
Rune *snarfbuf=0;
int nsnarfbuf=0;
void h_snarf(Panel *p, int){
	int s0, s1;
	Rune *text;
	p=p->userp;
	plegetsel(p, &s0, &s1);
	if(s0==s1) return;
	text=pleget(p);
	if(snarfbuf) free(snarfbuf);
	nsnarfbuf=s1-s0;
	snarfbuf=malloc(nsnarfbuf*sizeof(Rune));
	if(snarfbuf==0){
		fprint(2, "No mem\n");
		exits("no mem");
	}
	memmove(snarfbuf, text+s0, nsnarfbuf*sizeof(Rune));
}
void h_cut(Panel *p, int b){
	h_snarf(p, b);
	plepaste(p->userp, 0, 0);
}
void h_paste(Panel *p, int){
	plepaste(p->userp, snarfbuf, nsnarfbuf);
}
int ulen(char *s){
	int len;
	len=0;
	for(;*s;s++){
		if(strchr("/$-_@.!*'(), ", *s)
		|| 'a'<=*s && *s<='z'
		|| 'A'<=*s && *s<='Z'
		|| '0'<=*s && *s<='9')
			len++;
		else
			len+=3;
	}
	return len;
}
int hexdigit(int v){
	return 0<=v && v<=9?'0'+v:'A'+v-10;
}
char *ucpy(char *buf, char *s){
	for(;*s;s++){
		if(strchr("/$-_@.!*'(),", *s)
		|| 'a'<=*s && *s<='z'
		|| 'A'<=*s && *s<='Z'
		|| '0'<=*s && *s<='9')
			*buf++=*s;
		else if(*s==' ')
			*buf++='+';
		else{
			*buf++='%';
			*buf++=hexdigit((*s>>4)&15);
			*buf++=hexdigit(*s&15);
		}
	}
	*buf='\0';
	return buf;
}
char *runetou(char *buf, Rune r){
	char rbuf[2];
	if(r<=255){
		rbuf[0]=r;
		rbuf[1]='\0';
		buf=ucpy(buf, rbuf);
	}
	return buf;
}
/*
 * If there's exactly one button with type=text, then
 * a CR in the button is supposed to submit the form.
 */
void h_submittype(Panel *p, char *){
	int ntype;
	Field *f;
	ntype=0;
	for(f=((Field *)p->userp)->form->fields;f;f=f->next) if(f->type==TYPEIN) ntype++;
	if(ntype==1) h_submitinput(p, 0);
}
void h_submitindex(Panel *p, char *){
	h_submitinput(p, 0);
}
void h_submitinput(Panel *p, int){
	Form *form;
	int size, nrune;
	char *buf, *bufp, sep;
	Rune *rp;
	Field *f;
	Option *o;
	form=((Field *)p->userp)->form;
	if(form->method==GET) size=ulen(form->action)+1;
	else size=1;
	for(f=form->fields;f;f=f->next) switch(f->type){
	case TYPEIN:
	case PASSWD:
		size+=ulen(f->name)+1+ulen(plentryval(f->p))+1;
		break;
	case INDEX:
		size+=ulen(plentryval(f->p))+1;
		break;
	case CHECK:
	case RADIO:
		if(!f->state) break;
	case HIDDEN:
		size+=ulen(f->name)+1+ulen(f->value)+1;
		break;
	case SELECT:
		for(o=f->options;o;o=o->next)
			if(o->selected)
				size+=ulen(f->name)+1+ulen(o->value)+1;
		break;
	case TEXTWIN:
		size+=ulen(f->name)+1+plelen(f->textwin)*3+1;
		break;
	}
	buf=emalloc(size);
	if(form->method==GET){
		strcpy(buf, form->action);
		sep='?';
	}
	else{
		buf[0]='\0';
		sep=0;
	}
	bufp=buf+strlen(buf);
	if(form->method==GET && bufp!=buf && bufp[-1]=='?') *--bufp='\0'; /* spurious ? */
	for(f=form->fields;f;f=f->next) switch(f->type){
	case TYPEIN:
	case PASSWD:
		if(sep) *bufp++=sep;
		sep='&';
		bufp=ucpy(bufp, f->name);
		*bufp++='=';
		bufp=ucpy(bufp, plentryval(f->p));
		break;
	case INDEX:
		if(sep) *bufp++=sep;
		sep='&';
		bufp=ucpy(bufp, plentryval(f->p));
		break;
	case CHECK:
	case RADIO:
		if(!f->state) break;
	case HIDDEN:
		if(sep) *bufp++=sep;
		sep='&';
		bufp=ucpy(bufp, f->name);
		*bufp++='=';
		bufp=ucpy(bufp, f->value);
		break;
	case SELECT:
		for(o=f->options;o;o=o->next)
			if(o->selected){
				if(sep) *bufp++=sep;
				sep='&';
				bufp=ucpy(bufp, f->name);
				*bufp++='=';
				bufp=ucpy(bufp, o->value);
			}
		break;
	case TEXTWIN:
		if(sep) *bufp++=sep;
		sep='&';
		bufp=ucpy(bufp, f->name);
		*bufp++='=';
		rp=pleget(f->textwin);
		for(nrune=plelen(f->textwin);nrune!=0;--nrune)
			bufp=runetou(bufp, *rp++);
		*bufp='\0';
		break;
	}
	if(form->method==GET){
fprint(2, "GET %s\n", buf);
		geturl(buf, GET, 0, 0, 0);
	}
	else{
fprint(2, "POST %s: %s\n", form->action, buf);
		geturl(form->action, POST, buf, 0, 0);
	}
	free(buf);
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
