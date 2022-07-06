/*
 *          Kenji Okamoto   August 4, 2000
 *		Osaka Prefecture Univ.
 *            okamoto@granite.cias.osakafu-u.ac.jp
 */

#define HASHSIZE 257

/*
 * Structure for Dictionary's header word (in Hiragana)
 */
typedef	struct DicList DicList;
struct DicList {
	struct KouhoList *kouho;
	struct DicList *nextitem; /* for a future extension */
	char kanahead[1];
};

/*
 * Structure for Kouho of each index word in the dictionary
 */
typedef	struct KouhoList KouhoList;
struct KouhoList {
	struct KouhoList *nextkouho;
	struct KouhoList *prevkouho;
	struct DicList *dicitem;
	char kouhotop[1]; /* top of the kouhos */
} ;

typedef	struct Hash Hash;
struct Hash {
	DicList *dicindex; /* pointer to a KouhoList and kanahead etc */
	short length;
	struct Hash *next;
};

typedef	struct Dictionary Dictionary;
struct Dictionary {
	DicList *dlist; /* for a future extension, having more than one dictionaries */
	Hash *dhash[HASHSIZE];
};
