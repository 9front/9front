#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>

/*
 *  make the hash table completely in memory and then write as a file
 */

uchar *ht;
ulong hlen;
Ndb *db;
ulong nextchain;
char err[ERRMAX];

void
enter(char *val, ulong dboff)
{
	ulong h;
	uchar *last;
	ulong ptr;

	assert(dboff < NDBSPEC);

	h = ndbhash(val, hlen);
	h *= NDBPLEN;
	last = &ht[h];
	ptr = NDBGETP(last);
	if(ptr == NDBNAP){
		NDBPUTP(dboff, last);
		return;
	}

	assert(nextchain < NDBSPEC);

	if(ptr & NDBCHAIN){
		/* walk the chain to the last entry */
		for(;;){
			ptr &= ~NDBCHAIN;
			last = &ht[ptr+NDBPLEN];
			ptr = NDBGETP(last);
			if(ptr == NDBNAP){
				NDBPUTP(dboff, last);
				return;
			}
			if(!(ptr & NDBCHAIN)){
				NDBPUTP(nextchain|NDBCHAIN, last);
				break;
			}
		}
	} else
		NDBPUTP(nextchain|NDBCHAIN, last);

	/* add a chained entry */
	NDBPUTP(ptr, &ht[nextchain]);
	NDBPUTP(dboff, &ht[nextchain + NDBPLEN]);
	nextchain += 2*NDBPLEN;
}

void
main(int argc, char **argv)
{
	Ndbtuple *t, *nt;
	int n;
	Dir *d;
	uchar buf[NDBHLEN];
	char *file;
	int fd;
	ulong off;
	uchar *p;

	if(argc != 3){
		fprint(2, "usage: mkhash file attribute\n");
		exits("usage");
	}
	db = ndbopen(argv[1]);
	while(db != nil && strcmp(db->file, argv[1]) != 0)
		db = db->next;
	if(db == nil){
		errstr(err, sizeof(err));
		fprint(2, "mkhash: can't open %s\n", argv[1]);
		exits(err);
	}

	/* count entries to calculate hash size */
	n = 0;

	while(nt = ndbparse(db)){
		for(t = nt; t; t = t->entry){
			if(strcmp(t->attr, argv[2]) == 0)
				n++;
		}
		ndbfree(nt);
	}

	if(Boffset(&db->b) >= NDBSPEC){
		fprint(2, "mkhash: db file offset overflow\n");
		exits("overflow");
	}

	/* allocate an array large enough for worst case */
	hlen = 2*n+1;
	n = hlen*NDBPLEN + hlen*2*NDBPLEN;
	ht = mallocz(n, 1);
	if(ht == nil){
		fprint(2, "mkhash: not enough memory\n");
		exits("not enougth memory");
	}
	for(p = ht; p < &ht[n]; p += NDBPLEN)
		NDBPUTP(NDBNAP, p);
	nextchain = hlen*NDBPLEN;

	/* create the in core hash table */
	Bseek(&db->b, 0, 0);
	off = 0;
	while(nt = ndbparse(db)){
		for(t = nt; t; t = t->entry){
			if(strcmp(t->attr, argv[2]) == 0)
				enter(t->val, off);
		}
		ndbfree(nt);
		off = Boffset(&db->b);
	}

	/* create the hash file */
	file = smprint("%s.%s", argv[1], argv[2]);
	fd = create(file, OWRITE, DMTMP|0664);
	if(fd < 0){
		errstr(err, sizeof(err));
		fprint(2, "mkhash: can't create %s: %s\n", file, err);
		exits(err);
	}
	NDBPUTUL(db->mtime, buf);
	NDBPUTUL(hlen, buf+NDBULLEN);
	if(write(fd, buf, NDBHLEN) != NDBHLEN){
		errstr(err, sizeof(err));
		fprint(2, "mkhash: writing %s: %s\n", file, err);
		remove(file);
		exits(err);
	}
	if(write(fd, ht, nextchain) != nextchain){
		errstr(err, sizeof(err));
		fprint(2, "mkhash: writing %s: %s\n", file, err);
		remove(file);
		exits(err);
	}
	close(fd);

	/* make sure file didn't change while we were making the hash */
	d = dirstat(argv[1]);
	if(d == nil || d->qid.path != db->qid.path || d->qid.vers != db->qid.vers){
		fprint(2, "mkhash: %s changed underfoot\n", argv[1]);
		remove(file);
		exits("changed");
	}

	exits(nil);
}
