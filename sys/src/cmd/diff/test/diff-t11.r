.\"	$OpenBSD: t11.2,v 1.1 2003/07/21 20:16:21 otto Exp $
.\"
.Dd May 2, 1993
.Dt ED 1
.Os
.Sh NAME
.Nm ed
.Nd text editor
.Sh SYNOPSIS
.Nm ed
.Op Fl
.Op Fl sx
.Op Fl p Ar string
.Op Ar file
.Sh DESCRIPTION
.Nm
is a line-oriented text editor.
It is used to create, display, modify, and otherwise manipulate text files.
If invoked with a
.Ar file
argument, then a copy of
.Ar file
is read into the editor's buffer.
Changes are made to this copy and not directly to
.Ar file
itself.
Upon quitting
.Nm ed ,
any changes not explicitly saved with a
.Em w
command are lost.
.Pp
Editing is done in two distinct modes:
.Em command
and
.Em input .
When first invoked,
.Nm
is in command mode.
In this mode, commands are read from the standard input and
executed to manipulate the contents of the editor buffer.
.Pp
A typical command might look like:
.Bd -literal -offset indent
,s/old/new/g
.Ed
.Pp
which replaces all occurrences of the string
.Pa old
with
.Pa new .
.Pp
When an input command, such as
.Em a
(append),
.Em i
(insert),
or
.Em c
(change) is given,
.Nm
enters input mode.
This is the primary means of adding text to a file.
In this mode, no commands are available;
instead, the standard input is written directory to the editor buffer.
Lines consist of text up to and including a newline character.
Input mode is terminated by entering a single period
.Pq Ql \&.
on a line.
.Pp
All
.Nm
commands operate on whole lines or ranges of lines; e.g.,
the
.Em d
command deletes lines; the
.Em m
command moves lines, and so on.
It is possible to modify only a portion of a line by means of replacement,
as in the example above.
However, even here, the
.Em s
command is applied to whole lines at a time.
.Pp
In general,
.Nm
commands consist of zero or more line addresses, followed by a single
character command and possibly additional parameters; i.e.,
commands have the structure:
.Bd -literal -offset indent
[address [,address]]command[parameters]
.Ed
.Pp
The address(es) indicate the line or range of lines to be affected by the
command.
If fewer addresses are given than the command accepts, then
default addresses are supplied.
.Pp
The options are as follows:
.Bl -tag -width Ds
.It Fl
Same as the
.Fl s
option (deprecated).
.It Fl s
Suppress diagnostics.
This should be used if
.Nm
standard input is from a script.
.Fl s
flag.
.It Fl x
Prompt for an encryption key to be used in subsequent reads and writes
(see the
.Em x
command).
.It Fl p Ar string
Specifies a command prompt.
This may be toggled on and off with the
.Em P
command.
.It Ar file
Specifies the name of a file to read.
If
.Ar file
is prefixed with a
bang
.Pq Ql \&! ,
then it is interpreted as a shell command.
In this case, what is read is the standard output of
.Ar file
executed via
.Xr sh 1 .
To read a file whose name begins with a bang, prefix the
name with a backslash
.Pq Ql \e .
The default filename is set to
.Ar file
only if it is not prefixed with a bang.
.El
.Ss LINE ADDRESSING
An address represents the number of a line in the buffer.
.Nm
maintains a
.Em current address
which is typically supplied to commands as the default address
when none is specified.
When a file is first read, the current address is set to the last line
of the file.
In general, the current address is set to the last line affected by a command.
.Pp
A line address is
constructed from one of the bases in the list below, optionally followed
by a numeric offset.
The offset may include any combination of digits, operators (i.e.,
.Em + ,
.Em - ,
and
.Em ^ ) ,
and whitespace.
Addresses are read from left to right, and their values are computed
relative to the current address.
.Pp
One exception to the rule that addresses represent line numbers is the
address
.Em 0
(zero).
This means
.Dq before the first line ,
and is legal wherever it makes sense.
.Pp
An address range is two addresses separated either by a comma or semi-colon.
The value of the first address in a range cannot exceed the
value of the second.
If only one address is given in a range,
then the second address is set to the given address.
If an
.Em n Ns No -tuple
of addresses is given where
.Em n > 2 ,
then the corresponding range is determined by the last two addresses in the
.Em n Ns No -tuple.
If only one address is expected, then the last address is used.
.Pp
Each address in a comma-delimited range is interpreted relative to the
current address.
In a semi-colon-delimited range, the first address is
used to set the current address, and the second address is interpreted
relative to the first.
.Pp
The following address symbols are recognized:
.Bl -tag -width Ds
.It Em \&.
The current line (address) in the buffer.
.It Em $
The last line in the buffer.
.It Em n
The
.Em n Ns No th
line in the buffer where
.Em n
is a number in the range
.Em [0,$] .
.It Em - No or Em ^
The previous line.
This is equivalent to
.Em -1
and may be repeated with cumulative effect.
.It Em -n No or Em ^n
The
.Em n Ns No th
previous line, where
.Em n
is a non-negative number.
.It Em +
The next line.
This is equivalent to
.Em +1
and may be repeated with cumulative effect.
.It Em +n
The
.Em n Ns No th
next line, where
.Em n
is a non-negative number.
.It Em \&, No or Em %
The first through last lines in the buffer.
This is equivalent to the address range
.Em 1,$ .
.It Em \&;
The current through last lines in the buffer.
This is equivalent to the address range
.Em .,$ .
.It Em / Ns No re Ns Em /
The next line containing the regular expression
.Em re .
The search wraps to the beginning of the buffer and continues down to the
current line, if necessary.
.Em //
repeats the last search.
.It Em ? Ns No re Ns Em ?
The previous line containing the regular expression
.Em re .
The search wraps to the end of the buffer and continues up to the
current line, if necessary.
.Em ??
repeats the last search.
.It Em \&\' Ns No lc
The line previously marked by a
.Em k
(mark) command, where
.Em lc
is a lower case letter.
.El
.Ss REGULAR EXPRESSIONS
Regular expressions are patterns used in selecting text.
For example, the
.Nm
command
.Bd -literal -offset indent
g/string/
.Ed
.Pp
prints all lines containing
.Em string .
Regular expressions are also used by the
.Em s
command for selecting old text to be replaced with new.
.Pp
In addition to a specifying string literals, regular expressions can
represent classes of strings.
Strings thus represented are said to be matched by the
corresponding regular expression.
If it is possible for a regular expression to match several strings in
a line, then the leftmost longest match is the one selected.
.Pp
The following symbols are used in constructing regular expressions:
.Bl -tag -width Dsasdfsd
.It Em c
Any character
.Em c
not listed below, including
.Em { Ns No ,
.Em } Ns No ,
.Em \&( Ns No ,
.Em \&) Ns No ,
.Em < Ns No ,
and
.Em >
matches itself.
.It Em \ec
Any backslash-escaped character
.Em c Ns No ,
except for
.Em { Ns No ,
.Em } Ns No ,
.Em \&( Ns No ,
.Em \&) Ns No ,
.Em < Ns No , and
.Em >
matches itself.
.It Em \&.
Matches any single character.
.It Em [char-class]
Matches any single character in
.Em char-class .
To include a
.Ql \&]
in
.Em char-class Ns No ,
it must be the first character.
A range of characters may be specified by separating the end characters
of the range with a
.Ql - ;
e.g.,
.Em a-z
specifies the lower case characters.
The following literal expressions can also be used in
.Em char-class
to specify sets of characters:
.Pp
.Em \ \ [:alnum:]\ \ [:cntrl:]\ \ [:lower:]\ \ [:space:]
.Em \ \ [:alpha:]\ \ [:digit:]\ \ [:print:]\ \ [:upper:]
.Em \ \ [:blank:]\ \ [:graph:]\ \ [:punct:]\ \ [:xdigit:]
.Pp
If
.Ql -
appears as the first or last character of
.Em char-class Ns No ,
then it matches itself.
All other characters in
.Em char-class
match themselves.
.Pp
Patterns in
.Em char-class
of the form
.Em [.col-elm.] No or Em [=col-elm=]
where
.Em col-elm
is a collating element are interpreted according to
.Xr locale 5
(not currently supported).
See
.Xr regex 3
for an explanation of these constructs.
.It Em [^char-class]
Matches any single character, other than newline, not in
.Em char-class Ns No .
.Em char-class
is defined as above.
.It Em ^
If
.Em ^
is the first character of a regular expression, then it
anchors the regular expression to the beginning of a line.
Otherwise, it matches itself.
.It Em $
If
.Em $
is the last character of a regular expression,
it anchors the regular expression to the end of a line.
Otherwise, it matches itself.
.It Em \e<
Anchors the single character regular expression or subexpression
immediately following it to the beginning of a word.
(This may not be available.)
.It Em \e>
Anchors the single character regular expression or subexpression
immediately following it to the end of a word.
(This may not be available.)
.It Em \e( Ns No re Ns Em \e)
Defines a subexpression
.Em re .
Subexpressions may be nested.
A subsequent backreference of the form
.Em \en Ns No ,
where
.Em n
is a number in the range [1,9], expands to the text matched by the
.Em n Ns No th
subexpression.
For example, the regular expression
.Em \e(.*\e)\e1
matches any string consisting of identical adjacent substrings.
Subexpressions are ordered relative to their left delimiter.
.It Em *
Matches the single character regular expression or subexpression
immediately preceding it zero or more times.
If
.Em *
is the first character of a regular expression or subexpression,
then it matches itself.
The
.Em *
operator sometimes yields unexpected results.
For example, the regular expression
.Em b*
matches the beginning of the string
.Em abbb
(as opposed to the substring
.Em bbb Ns No ),
since a null match is the only leftmost match.
.Sm off
.It Xo Em \e{ No n,m
.Em \e}\ \e{ No n, Em \e}\
.Em \e{ No n Em \e}
.Xc
.Sm on
Matches the single character regular expression or subexpression
immediately preceding it at least
.Em n
and at most
.Em m
times.
If
.Em m
is omitted, then it matches at least
.Em n
times.
If the comma is also omitted, then it matches exactly
.Em n
times.
.El
.Pp
Additional regular expression operators may be defined depending on the
particular
.Xr regex 3
implementation.
.Ss COMMANDS
All
.Nm
commands are single characters, though some require additional parameters.
If a command's parameters extend over several lines, then
each line except for the last must be terminated with a backslash
.Pq Ql \e .
.Pp
In general, at most one command is allowed per line.
However, most commands accept a print suffix, which is any of
.Em p No (print),
.Em l No (list),
or
.Em n No (enumerate),
to print the last line affected by the command.
.Pp
An interrupt (typically ^C) has the effect of aborting the current command
and returning the editor to command mode.
.Pp
.Nm
recognizes the following commands.
The commands are shown together with
the default address or address range supplied if none is
specified (in parentheses), and other possible arguments on the right.
.Bl -tag -width Dxxs
.It (.) Ns Em a
Appends text to the buffer after the addressed line.
Text is entered in input mode.
The current address is set to last line entered.
.It (.,.) Ns Em c
Changes lines in the buffer.
The addressed lines are deleted from the buffer,
and text is appended in their place.
Text is entered in input mode.
The current address is set to last line entered.
.It (.,.) Ns Em d
Deletes the addressed lines from the buffer.
If there is a line after the deleted range, then the current address is set
to this line.
Otherwise the current address is set to the line before the deleted range.
.It Em e No file
Edits
.Em file Ns No ,
and sets the default filename.
If
.Em file
is not specified, then the default filename is used.
Any lines in the buffer are deleted before the new file is read.
The current address is set to the last line read.
.It Em e No !command
Edits the standard output of
.Em !command Ns No ,
(see
.Em ! No command
below).
The default filename is unchanged.
Any lines in the buffer are deleted before the output of
.Em command
is read.
The current address is set to the last line read.
.It Em E No file
Edits
.Em file
unconditionally.
This is similar to the
.Em e
command, except that unwritten changes are discarded without warning.
The current address is set to the last line read.
.It Em f No file
Sets the default filename to
.Em file Ns No .
If
.Em file
is not specified, then the default unescaped filename is printed.
.It (1,$) Ns Em g Ns No /re/command-list
Applies
.Em command-list
to each of the addressed lines matching a regular expression
.Em re Ns No .
The current address is set to the line currently matched before
.Em command-list
is executed.
At the end of the
.Em g
command, the current address is set to the last line affected by
.Em command-list Ns No .
.Pp
Each command in
.Em command-list
must be on a separate line,
and every line except for the last must be terminated by
.Em \e No (backslash).
Any commands are allowed, except for
.Em g Ns No ,
.Em G Ns No ,
.Em v Ns No ,
and
.Em V Ns No .
A newline alone in
.Em command-list
is equivalent to a
.Em p
command.
.It (1,$) Ns Em G Ns No /re/
Interactively edits the addressed lines matching a regular expression
.Em re Ns No .
For each matching line, the line is printed, the current address is set,
and the user is prompted to enter a
.Em command-list Ns No .
At the end of the
.Em g
command, the current address is set to the last line affected by (the last)
.Em command-list Ns No .
.Pp
The format of
.Em command-list
is the same as that of the
.Em g
command.
A newline alone acts as a null command list.
A single
.Em &
repeats the last non-null command list.
.It Em H
Toggles the printing of error explanations.
By default, explanations are not printed.
It is recommended that
.Nm
scripts begin with this command to aid in debugging.
.It Em h
Prints an explanation of the last error.
.It (.) Ns Em i
Inserts text in the buffer before the current line.
Text is entered in input mode.
The current address is set to the last line entered.
.It (.,.+1) Ns Em j
Joins the addressed lines.
The addressed lines are deleted from the buffer and replaced by a single
line containing their joined text.
The current address is set to the resultant line.
.It (.) Ns Em klc
Marks a line with a lower case letter
.Em lc Ns No \&.
The line can then be addressed as
.Em \&'lc
(i.e., a single quote followed by
.Em lc Ns No )
in subsequent commands.
The mark is not cleared until the line is deleted or otherwise modified.
.It (.,.) Ns Em l
Prints the addressed lines unambiguously.
If a single line fills more than one screen (as might be the case
when viewing a binary file, for instance), a
.Dq --More--
prompt is printed on the last line.
.Nm
waits until the RETURN key is pressed before displaying the next screen.
The current address is set to the last line printed.
.It (.,.) Ns Em m Ns No (.)
Moves lines in the buffer.
The addressed lines are moved to after the
right-hand destination address, which may be the address
.Em 0
(zero).
The current address is set to the last line moved.
.It (.,.) Ns Em n
Prints the addressed lines along with their line numbers.
The current address is set to the last line printed.
.It (.,.) Ns Em p
Prints the addressed lines.
The current address is set to the last line printed.
.It Em P
Toggles the command prompt on and off.
Unless a prompt was specified by with command-line option
.Fl p Ar string Ns No ,
the command prompt is by default turned off.
.It Em q
Quits
.Nm ed .
.It Em Q
Quits
.Nm
unconditionally.
This is similar to the
.Em q
command, except that unwritten changes are discarded without warning.
.It ($) Ns Em r No file
Reads
.Em file
to after the addressed line.
If
.Em file
is not specified, then the default filename is used.
If there was no default filename prior to the command,
then the default filename is set to
.Em file Ns No .
Otherwise, the default filename is unchanged.
The current address is set to the last line read.
.It ($) Ns Em r No !command
Reads to after the addressed line the standard output of
.Em !command Ns No ,
(see the
.Em !
command below).
The default filename is unchanged.
The current address is set to the last line read.
.Sm off
.It Xo (.,.) Em s No /re/replacement/ , \ (.,.)
.Em s No /re/replacement/ Em g , No \ (.,.)
.Em s No /re/replacement/ Em n
.Xc
.Sm on
Replaces text in the addressed lines matching a regular expression
.Em re
with
.Em replacement Ns No .
By default, only the first match in each line is replaced.
If the
.Em g
(global) suffix is given, then every match to be replaced.
The
.Em n
suffix, where
.Em n
is a positive number, causes only the
.Em n Ns No th
match to be replaced.
It is an error if no substitutions are performed on any of the addressed
lines.
The current address is set the last line affected.
.Pp
.Em re
and
.Em replacement
may be delimited by any character other than space and newline
(see the
.Em s
command below).
If one or two of the last delimiters is omitted, then the last line
affected is printed as though the print suffix
.Em p
were specified.
.Pp
An unescaped
.Ql \e
in
.Em replacement
is replaced by the currently matched text.
The character sequence
.Em \em Ns No ,
where
.Em m
is a number in the range [1,9], is replaced by the
.Em m Ns No th
backreference expression of the matched text.
If
.Em replacement
consists of a single
.Ql % ,
then
.Em replacement
from the last substitution is used.
Newlines may be embedded in
.Em replacement
if they are escaped with a backslash
.Pq Ql \e .
.It (.,.) Ns Em s
Repeats the last substitution.
This form of the
.Em s
command accepts a count suffix
.Em n Ns No ,
or any combination of the characters
.Em r Ns No ,
.Em g Ns No ,
and
.Em p Ns No .
If a count suffix
.Em n
is given, then only the
.Em n Ns No th
match is replaced.
The
.Em r
suffix causes
the regular expression of the last search to be used instead of the
that of the last substitution.
The
.Em g
suffix toggles the global suffix of the last substitution.
The
.Em p
suffix toggles the print suffix of the last substitution
The current address is set to the last line affected.
.It (.,.) Ns Em t Ns No (.)
Copies (i.e., transfers) the addressed lines to after the right-hand
destination address, which may be the address
.Em 0
(zero).
The current address is set to the last line copied.
.It Em u
Undoes the last command and restores the current address
to what it was before the command.
The global commands
.Em g Ns No ,
.Em G Ns No ,
.Em v Ns No ,
and
.Em V Ns No .
are treated as a single command by undo.
.Em u
is its own inverse.
.It (1,$) Ns Em v Ns No /re/command-list
Applies
.Em command-list
to each of the addressed lines not matching a regular expression
.Em re Ns No .
This is similar to the
.Em g
command.
.It (1,$) Ns Em V Ns No /re/
Interactively edits the addressed lines not matching a regular expression
.Em re Ns No .
This is similar to the
.Em G
command.
.It (1,$) Ns Em w No file
Writes the addressed lines to
.Em file Ns No .
Any previous contents of
.Em file
is lost without warning.
If there is no default filename, then the default filename is set to
.Em file Ns No ,
otherwise it is unchanged.
If no filename is specified, then the default filename is used.
The current address is unchanged.
.It (1,$) Ns Em wq No file
Writes the addressed lines to
.Em file Ns No ,
and then executes a
.Em q
command.
.It (1,$) Ns Em w No !command
Writes the addressed lines to the standard input of
.Em !command Ns No ,
(see the
.Em !
command below).
The default filename and current address are unchanged.
.It (1,$) Ns Em W No file
Appends the addressed lines to the end of
.Em file Ns No .
This is similar to the
.Em w
command, expect that the previous contents of file is not clobbered.
The current address is unchanged.
.It Em x
Prompts for an encryption key which is used in subsequent reads and writes.
If a newline alone is entered as the key, then encryption is turned off.
Otherwise, echoing is disabled while a key is read.
Encryption/decryption is done using the
.Xr bdes 1
algorithm.
.It (.+1) Ns Em z Ns No n
Scrolls
.Em n
lines at a time starting at addressed line.
If
.Em n
is not specified, then the current window size is used.
The current address is set to the last line printed.
.It ($) Ns Em =
Prints the line number of the addressed line.
.It (.+1) Ns Em newline
Prints the addressed line, and sets the current address to that line.
.It Em ! Ns No command
Executes
.Em command
via
.Xr sh 1 .
If the first character of
.Em command
is
.Em ! Ns No ,
then it is replaced by text of the previous
.Em !command Ns No .
.Nm
does not process
.Em command
for
.Em \e
(backslash) escapes.
However, an unescaped
.Em %
is replaced by the default filename.
When the shell returns from execution, a
.Em !
is printed to the standard output.
The current line is unchanged.
.El
.Sh LIMITATIONS
.Nm
processes
.Em file
arguments for backslash escapes, i.e., in a filename,
any characters preceded by a backslash
.Pq Ql \e
are interpreted literally.
.Pp
If a text (non-binary) file is not terminated by a newline character,
then
.Nm
appends one on reading/writing it.
In the case of a binary file,
.Nm
does not append a newline on reading/writing.
.Sh DIAGNOSTICS
When an error occurs,
.Nm
prints a
.Dq ?
and either returns to command mode or exits if its input is from a script.
An explanation of the last error can be printed with the
.Em h
(help) command.
.Pp
Since the
.Em g
(global) command masks any errors from failed searches and substitutions,
it can be used to perform conditional operations in scripts; e.g.,
.Bd -literal -offset indent
g/old/s//new/
.Ed
.Pp
replaces any occurrences of
.Em old
with
.Em new Ns No .
.Pp
If the
.Em u
(undo) command occurs in a global command list, then
the command list is executed only once.
.Pp
If diagnostics are not disabled, attempting to quit
.Nm
or edit another file before writing a modified buffer results in an error.
If the command is entered a second time, it succeeds,
but any changes to the buffer are lost.
.Sh FILES
.Bl -tag -width /tmp/ed.* -compact
.It Pa /tmp/ed.*
buffer file
.It Pa ed.hup
where
.Nm
attempts to write the buffer if the terminal hangs up
.El
.Sh SEE ALSO
.Xr bdes 1 ,
.Xr sed 1 ,
.Xr sh 1 ,
.Xr vi 1 ,
.Xr regex 3
.Pp
USD:12-13
.Rs
.%A B. W. Kernighan
.%A P. J. Plauger
.%B Software Tools in Pascal
.%O Addison-Wesley
.%D 1981
.Re
.Sh HISTORY
An
.Nm
command appeared in
.At v1 .
