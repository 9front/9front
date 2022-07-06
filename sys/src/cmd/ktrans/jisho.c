/* 
 *  open jisho file, and set the size of this jisho etc
 *
 *          Kenji Okamoto   August 4, 2000
 *		Osaka Prefecture Univ.
 *            okamoto@granite.cias.osakafu-u.ac.jp
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include "jisho.h"

Dictionary *openQDIC(char *);
void freeQDIC(Dictionary*);
KouhoList *getKouhoHash(Dictionary*, char *);
KouhoList *getKouhoFile(DicList*, char *);
void selectKouho(KouhoList **, KouhoList*);
int hashVal(char *);
void addHash(Hash **, DicList*);



/*
 * Open QuickDIC (hashed personal dictionary)
 *   open skk styled ktrans dictionary file, and make its hash table 
 *    based on individual header kana strings
 *
 *                                        KouhoList
 *                                       |---------|
 *                  Hash         |---->kouho---->kouhotop
 *               |-------|       |
 *    dic---->dhash---->dicindex---->kanahead
 *     |--------|         |--------|
 *     Dictionary           DicList
 *
 */
Dictionary *
openQDIC(char *dicname)
{
	Biobuf *f;
	void *Bbuf;
	Dictionary *dic;
	DicList *dicitem;			/* for a future extension */
	char buf[1024], *startstr, *endstr;
	int i;

	SET(dicitem);		 /* yes, I know I'm wrong, but... */

	dic = (Dictionary*)malloc(sizeof(Dictionary));
  	      /* make room for pointer array (size=HASHSIZE) of hash table */
	for(i=0; i< HASHSIZE; i++) dic->dhash[i] = 0;
	dic->dlist = 0;		/* for a future extension (more than one dics ^_^ */

	if ((f = Bopen(dicname, OREAD)) == 0)
	    return dic;

    /* make hash table by the dic's header word */

	while(Bbuf = Brdline(f, '\n')) {
	   strncpy(buf, (char *)Bbuf, Blinelen(f));

	   if (buf[0] == ';')    /* comment line */
		continue;
	   else {
    	  	/* get header word from jisho */
	  	startstr = buf;
	  	if(!(endstr = utfutf(startstr, "\t"))) break;
	  	*endstr = '\0';
			/* dicitem includes each header word from the jisho */

		dicitem = (DicList*)malloc(sizeof(DicList)+(endstr-startstr+1));
		dicitem->nextitem = 0;		/* for a future extension */
		strcpy(dicitem->kanahead, startstr);

		dicitem->kouho = getKouhoFile(dicitem, endstr);    /* read kouho from jisho */
		addHash(dic->dhash, dicitem);
	   }
	   continue;
	}
	dic->dlist = dicitem;
	Bterm(f);
	return dic;
}

/* 
 * free dynamically allocated memory
 */
void
freeQDIC(Dictionary *dic)
{
   Hash *hash1, *hash2;
   DicList *dlist, *dlist2;
   int l;

	for (dlist = dic->dlist;
		dlist != 0;
		dlist2 = dlist, dlist = dlist->nextitem, free((void *)dlist2));
	for (l = 0; l < HASHSIZE; l++) {
		for (hash1 = dic->dhash[l]; hash1; hash1 = hash2) {
			if (hash1->next !=0) {
				hash2 = hash1->next;
				free((void *)hash1);
			}else
				break;
		}
	}
	free((void *)dic);
}

int
hashVal(char *s)
{
	uint h;

	h = 0x811c9dc5;
	while(*s != 0)
		h = (h^(uchar)*s++) * 0x1000193;
	return h % HASHSIZE;
}

void
addHash(Hash **hash, DicList *ditem)
{
	Hash *h;
	int v;

	v = hashVal(ditem->kanahead);
	h = (Hash*)malloc(sizeof(Hash));
	h->dicindex = ditem;
	h->length = strlen(ditem->kanahead);
	h->next = hash[v];
	hash[v] = h;
}

/* 
 * read Kouho list from the jisho file defined by Biobuf descriptor f
 * 
 *  revised for Plan 9 by K.Okamoto
 */
KouhoList *
getKouhoFile(DicList *dicitem, char * endstr)
{
	char *kouhostart, *kouhoend;
	KouhoList *kouhoitem, *currntkouhoitem=0, *prevkouhoitem;

	prevkouhoitem = 0;
	kouhostart = endstr + 1;
	while((kouhoend = utfutf(kouhostart, " ")) || 
			(kouhoend = utfutf(kouhostart, "\n"))) {
	   *kouhoend = '\0';

	   kouhoitem = (KouhoList*)malloc(sizeof(KouhoList)+(kouhoend-kouhostart+1));
	   kouhoitem->nextkouho = 0;
	   kouhoitem->prevkouho = prevkouhoitem;
	   kouhoitem->dicitem = dicitem;
	   strcpy(kouhoitem->kouhotop, kouhostart);
	   if (prevkouhoitem)
		prevkouhoitem->nextkouho = kouhoitem;
	   else
		currntkouhoitem = kouhoitem;
	   prevkouhoitem = kouhoitem;
	   kouhostart = kouhoend + 1;
	}
	return currntkouhoitem;
}

/*
 * get matched kouho from the hash table of header word of the dict
 * if found, returns pointer to the first candidate in the hash table.
 * if not found, returns 0.
 * 
 * from getCand() in skklib.c by Akinori Ito et al.,(aito@ei5sun.yz.yamagata-u.ac.jp)
 */
KouhoList *
getKouhoHash(Dictionary *dic, char *s)
{
	int l, v;
	Hash *h;

	l = strlen(s);
	v = hashVal(s);
	for (h = dic->dhash[v]; h != 0; h = h->next) {
		if (h->length != l ||
		    strcmp(h->dicindex->kanahead, s)) continue;
		return h->dicindex->kouho;      /* return matched kouho */
	}
	return 0;
}

/* 
 * from skklib.c by Akinori Ito et al.,(aito@ei5sun.yz.yamagata-u.ac.jp)
 * just modified to read easier for current purpose
 */
void
selectKouho(KouhoList **first, KouhoList *current)
{
	/* take off currentkouho from the kouholist table */
	if (current->prevkouho) {
		current->prevkouho->nextkouho = current->nextkouho;
		if (current->nextkouho)
			current->nextkouho->prevkouho = current->prevkouho;
		current->prevkouho = 0;
	}
	/* take place of firstkouho by currentkouho  */
	if (*first != current) {
		(*first)->prevkouho = current;
		current->nextkouho = *first;
		*first = current;
	}
}
