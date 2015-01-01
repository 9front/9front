#!/bin/awk -f
# makes a table of character sets from http://www.iana.org/assignments/character-sets/character-sets.xml
# and tcs.txt

/<name>/, /<\/name>/ {
	gsub(/[<>\/]+/, " ")
	i = 0
	name = tolower($2)
	names[name] = name
	alias[name i] = name
	nalias[name] = ++i
	next
}

/<alias>/, /<\/alias>/ {
	gsub(/[<>\/]+/, " ")
	a = tolower($2)
	names[a] = name
	alias[name i] = a
	nalias[name] = ++i
	next
}

END {
	while(getline <ARGV[2]){
		tcs = $1
		if(tcs in names){
			name = names[tcs]
			for(n = 0; n < nalias[name]; n++)
				print "\"" alias[name n] "\", \"" $2 "\","
		}
	}
}
