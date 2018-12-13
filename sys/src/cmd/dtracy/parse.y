%{
#include <u.h>
#include <libc.h>
#include <dtracy.h>
#include <bio.h>
#include "dat.h"
#include "fns.h"
%}

%union{
	Node *n;
	Symbol *sym;
	DTExpr *e;
	s64int num;
	char *str;
	Type *t;
}

%type <n> expr optexpr
%type <sym> optsym
%type <t> type
%type <str> probe

%token <sym> TSYM
%token <num> TNUM
%token <str> TSTR
%token TPRINT TPRINTF
%token TIF
%token TU8 TU16 TU32 TU64
%token TS8 TS16 TS32 TS64
%token TSTRING

%right '?'
%left TOR
%left TAND
%left '|'
%left '^'
%left '&'
%left TEQ TNE
%left '<' '>' TLE TGE
%left TLSL TLSR
%left '+' '-'
%left '*' '/' '%'
%left unary
%right castprec

%%

program: | program clause

clause: { clausebegin(); } probes optpredicate optaction { clauseend(); }

optpredicate: | TIF expr { addpred(codegen(exprcheck($2, 1))); }

optaction:
	{
		addstat(STATPRINT);
		addarg(node(OSYM, getsym("probe")));
	}
	| action
action: '{' stats '}'
stats: | stats0 | stats0 ';'
stats0: stat | stats0 ';' stat

stat: expr { addstat(STATEXPR, exprcheck($1, 0)); }
| TPRINT { addstat(STATPRINT); } pelist
| TPRINTF { addstat(STATPRINTF); } pelist
| '@' optsym '[' expr ']' '=' TSYM '(' optexpr ')' { addstat(STATAGG, $2, $4, $7, $9); }
optsym: TSYM | { $$ = nil; }
optexpr: expr | { $$ = nil; }

pelist:
	'(' ')'
	| '(' arg ',' ')'
	| '(' elist2 optcomma ')'
	| arg optcomma
	| elist2 optcomma
elist2: arg ',' arg | elist2 ',' arg
arg: expr { addarg(exprcheck($1, 0)); }
optcomma: | ','

expr:
	TSYM { $$ = node(OSYM, $1); }
	| TNUM { $$ = node(ONUM, $1); }
	| TSTR { $$ = node(OSTR, $1); }
	| expr '+' expr { $$ = node(OBIN, OPADD, $1, $3); }
	| expr '-' expr { $$ = node(OBIN, OPSUB, $1, $3); }
	| expr '*' expr { $$ = node(OBIN, OPMUL, $1, $3); }
	| expr '/' expr { $$ = node(OBIN, OPDIV, $1, $3); }
	| expr '%' expr { $$ = node(OBIN, OPMOD, $1, $3); }
	| expr '&' expr { $$ = node(OBIN, OPAND, $1, $3); }
	| expr '|' expr { $$ = node(OBIN, OPOR, $1, $3); }
	| expr '^' expr { $$ = node(OBIN, OPXOR, $1, $3); }
	| expr TLSL expr { $$ = node(OBIN, OPLSH, $1, $3); }
	| expr TLSR expr { $$ = node(OBIN, OPRSH, $1, $3); }
	| expr TEQ expr { $$ = node(OBIN, OPEQ, $1, $3); }
	| expr TNE expr { $$ = node(OBIN, OPNE, $1, $3); }
	| expr '<' expr { $$ = node(OBIN, OPLT, $1, $3); }
	| expr TLE expr { $$ = node(OBIN, OPLE, $1, $3); }
	| expr '>' expr { $$ = node(OBIN, OPLT, $3, $1); }
	| expr TGE expr { $$ = node(OBIN, OPLE, $3, $1); }
	| expr TAND expr { $$ = node(OBIN, OPLAND, $1, $3); }
	| expr TOR expr { $$ = node(OBIN, OPLOR, $1, $3); }
	| '-' expr %prec unary { $$ = node(OBIN, OPSUB, node(ONUM, 0LL), $2); }
	| '~' expr %prec unary { $$ = node(OBIN, OPXNOR, node(ONUM, 0LL), $2); }
	| '!' expr %prec unary { $$ = node(OLNOT, $2); }
	| '(' expr ')' { $$ = $2; }
	| expr '?' expr ':' expr %prec '?' { $$ = node(OTERN, $1, $3, $5); }
	| '(' type ')' expr %prec castprec { $$ = node(OCAST, $2, $4); }

type:
	TU8 { $$ = type(TYPINT, 1, 0); }
	| TS8 { $$ = type(TYPINT, 1, 1); }
	| TU16 { $$ = type(TYPINT, 2, 0); }
	| TS16 { $$ = type(TYPINT, 2, 1); }
	| TU32 { $$ = type(TYPINT, 4, 0); }
	| TS32 { $$ = type(TYPINT, 4, 1); }
	| TU64 { $$ = type(TYPINT, 8, 0); }
	| TS64 { $$ = type(TYPINT, 8, 1); }
	| TSTRING { $$ = type(TYPSTRING); }

probes:
	probe { addprobe($1); }
	| probes ',' probe { addprobe($3); }

probe:
	TSYM { $$ = $1->name; }
	| TSTR { $$ = $1; }

%%
