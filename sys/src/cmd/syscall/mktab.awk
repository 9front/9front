#!/bin/awk -f
{	e = e $2 ", "
	s = s sprintf("[%s] \"%s\", (int(*)(...))%s,\n",
		$2, tolower($2), tolower($2))
}
END{
	e = e "READ, WRITE, NTAB"
	s = s "[READ] \"read\", (int(*)(...))read,\n"
	s = s "[WRITE] \"write\", (int(*)(...))write,\n"
	s = s "[NTAB] nil, 0\n"
	
	print "enum{", e, "};"
	print "struct Call tab[] = {\n", s, "};"
}
