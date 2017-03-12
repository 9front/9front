#include "common.h"
#include "send.h"

/* exports */
dest *dlist;

dest*
d_new(String *addr)
{
	dest *dp;

	dp = (dest *)mallocz(sizeof(dest), 1);
	if (dp == 0)
		sysfatal("malloc: %r");
	dp->same = dp;
	dp->nsame = 1;
	dp->nchar = 0;
	dp->next = dp;
	dp->addr = escapespecial(addr);
	dp->parent = 0;
	dp->repl1 = dp->repl2 = 0;
	dp->status = d_undefined;
	return dp;
}

void
d_free(dest *dp)
{
	if (dp != 0) {
		s_free(dp->addr);
		s_free(dp->repl1);
		s_free(dp->repl2);
		free((char *)dp);
	}
}

/* The following routines manipulate an ordered list of items.  Insertions
 * are always to the end of the list.  Deletions are from the beginning.
 *
 * The list are circular witht the `head' of the list being the last item
 * added.
 */

/*  Get first element from a circular list linked via 'next'. */
dest*
d_rm(dest **listp)
{
	dest *dp;

	if (*listp == 0)
		return 0;
	dp = (*listp)->next;
	if (dp == *listp)
		*listp = 0;
	else
		(*listp)->next = dp->next;
	dp->next = dp;
	return dp;
}

/*  Insert a new entry at the end of the list linked via 'next'. */
void
d_insert(dest **listp, dest *new)
{
	dest *head;

	if (*listp == 0) {
		*listp = new;
		return;
	}
	if (new == 0)
		return;
	head = new->next;
	new->next = (*listp)->next;
	(*listp)->next = head;
	*listp = new;
	return;
}

/*  Get first element from a circular list linked via 'same'. */
dest*
d_rm_same(dest **listp)
{
	dest *dp;

	if (*listp == 0)
		return 0;
	dp = (*listp)->same;
	if (dp == *listp)
		*listp = 0;
	else
		(*listp)->same = dp->same;
	dp->same = dp;
	return dp;
}

/* Look for a duplicate on the same list */
int
d_same_dup(dest *dp, dest *new)
{
	dest *first = dp;

	if(new->repl2 == 0)
		return 1;
	do {
		if(strcmp(s_to_c(dp->repl2), s_to_c(new->repl2))==0)
			return 1;
		dp = dp->same;
	} while(dp != first);
	return 0;
}

/*
 * Insert an entry into the corresponding list linked by 'same'.  Note that
 * the basic structure is a list of lists.
 */
void
d_same_insert(dest **listp, dest *new)
{
	dest *dp;
	int len;

	if(new->status == d_pipe || new->status == d_cat) {
		len = 0;
		if(new->repl2)
			len = strlen(s_to_c(new->repl2));
		if(*listp != 0){
			dp = (*listp)->next;
			do {
				if(dp->status == new->status
				&& strcmp(s_to_c(dp->repl1), s_to_c(new->repl1))==0){
					/* remove duplicates */
					if(d_same_dup(dp, new))
						return;
					/* add to chain if chain small enough */
					if(dp->nsame < MAXSAME
					&& dp->nchar + len < MAXSAMECHAR){
						new->same = dp->same;
						dp->same = new;
						dp->nchar += len + 1;
						dp->nsame++;
						return;
					}
				}
				dp = dp->next;
			} while (dp != (*listp)->next);
		}
		if(s_to_c(new->repl1))
			new->nchar = strlen(s_to_c(new->repl1)) + len + 1;
		else
			new->nchar = 0;
	}
	new->next = new;
	d_insert(listp, new);
}

/*
 *  Form a To: if multiple destinations.
 *  The local! and !local! checks are artificial intelligence,
 *  there should be a better way.
 */
String*
d_to(dest *list)
{
	dest *np, *sp;
	String *s;
	int i, n;
	char *cp;

	s = s_new();
	s_append(s, "To: ");
	np = list;
	i = n = 0;
	do {
		np = np->next;
		sp = np;
		do {
			sp = sp->same;
			cp = s_to_c(sp->addr);

			/* hack to get local! out of the names */
			if(strncmp(cp, "local!", 6) == 0)
				cp += 6;

			if(n > 20){	/* 20 to appease mailers complaining about long lines */
				s_append(s, "\n\t");
				n = 0;
			}
			if(i != 0){
				s_append(s, ", ");
				n += 2;
			}
			s_append(s, cp);
			n += strlen(cp);
			i++;
		} while(sp != np);
	} while(np != list);

	return unescapespecial(s);
}


#define isspace(c) ((c)==' ' || (c)=='\t' || (c)=='\n')

/*  Get the next field from a String.  The field is delimited by white space.
 *  Anything delimited by double quotes is included in the string.
 */
static String*
s_parseq(String *from, String *to)
{
	int c;

	if (*from->ptr == '\0')
		return 0;
	if (to == 0)
		to = s_new();
	for (c = *from->ptr;!isspace(c) && c != 0; c = *(++from->ptr)){
		s_putc(to, c);
		if(c == '"'){
			for (c = *(++from->ptr); c && c != '"'; c = *(++from->ptr))
				s_putc(to, *from->ptr);
			s_putc(to, '"');
			if(c == 0)
				break;
		}
	}
	s_terminate(to);

	/* crunch trailing white */
	while(isspace(*from->ptr))
		from->ptr++;

	return to;
}

/* expand a String of destinations into a linked list of destiniations */
dest*
s_to_dest(String *sp, dest *parent)
{
	String *addr;
	dest *list=0;
	dest *new;

	if (sp == 0)
		return 0;
	addr = s_new();
	while (s_parseq(sp, addr)!=0) {
		addr = escapespecial(addr);
		if(shellchars(s_to_c(addr))){
			while(new = d_rm(&list))
				d_free(new);
			break;
		}
		new = d_new(addr);
		new->parent = parent;
		new->authorized = parent->authorized;
		d_insert(&list, new);
		addr = s_new();
	}
	s_free(addr);
	return list;
}
