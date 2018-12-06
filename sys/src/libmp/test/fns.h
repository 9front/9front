ldint* ldnew(int);
void ldfree(ldint *);
void testgen(int, ldint *);
int ldmpeq(ldint *, mpint *);
mpint* ldtomp(ldint *, mpint *);
void mptarget(mpint *);
void tests(void);
void mpdiv_(mpint *, mpint *, mpint *, mpint *);
void convtests(void);
