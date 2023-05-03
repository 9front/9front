.HTML "Namespaces as Security Domains"
.TL
Namespaces as Security Domains
.AU
Jacob Moody
.AB
We aim to explore the use of Plan 9 namespaces
as ways of building isolated processes. We present
here code for increasing the ability and granularity
for which a process may isolate itself from others
on the system.
.AE
.SH
Introduction
.PP
.FS
First presented in a slightly different form at the 9th International Workshop on Plan 9.
.FE
.LP
In a Plan 9 system the kernel exposes hardware and system
interfaces through a myriad of filesystem trees. These trees, or
sharp devices, replace the functionality of many would be system calls
through use of standard file system operations. A standard Plan 9 environment
is comprised of a composition of these individual devices together, the collection
of such being the processes namespace.
.LP
With these principles it is quite easy for a process to build a slim namespace using only
what it may need for operation. This could be done in service to reduce the "blast radius"
of awry or malicious code to some effect. But to be fully effective a process must also be able
to remove the ability to bootstrap these capabilities back. We will explore different ways of
building isolated namespaces, their pitfalls, ways to address those issues, along with new solutions.
.SH
Outside World
.LP
There have been many solutions for sandboxing within the UNIX™
world. There are more classical approaches such as
.CW zones ,
[Price04] and
.CW jails ,
that all provide an abstraction of building some number of
smaller full unix boxes out of a single physical host. However these
interfaces are presented more as a systems management tool, the mechanisms
for which an administrator creates and manages these resources is unergonomic
to use on a per-process basis. Instead it seems more the fashion now to isolate
specific pieces of the system, and expect it possible that each process on the system
may choose to manage its environment. The most successful execution of this idea in the
wild is the OpenBSD project's
.CW unveil
and
.CW pledge
[Beck18] system calls, allowing a processes to cut off specific parts of the filesystem or
system call interfaces. Linux namespaces [Biederman06] implement this idea by allowing a process
to fork off private versions of specific global resources. In both these cases the sandboxing
of a process is through gradual steps, removing potentially dangerous tools one by one.
.SH
Existing Work
.LP
Let us first define the resources we are restricting access to. The aforementioned gradual solutions
provide ways in which a process can remove itself from specific kernel interfaces. In plan9 the kernel
exposes almost all of its functionality through individual filesystems. These devices are accessed
globally by prefixing a path with a sharp('#'), and have conventional places they are bound within the
namespace.
.LP
A processes namespace in plan9 is typically constructed using a namespace file. These files
are a collection of namespace operations formatted as one would expect to see them in a shell script.
They typically begin by binding in some number of sharp devices in to their expected location.
.P1
bind #d /fd
bind -c #e /env
bind #p /proc
bind -c #s /srv
.P2
Then using the globals provided, in particular /srv, to bring in the rest of the root filesystem.
A process can at any point choose to construct itself a new namespace, but it must do so when changing
users. This is done in part to ensure that each filesystem that the program would like to use has
their chance to authenticate and be notified. Because this information is only exchanged on attach,
the new user must construct a namespace from scratch.
.LP
Many programs, like network services, wish to drop their current user and become the special user
.CW none
user on startup, and in doing so must rebuild their namespace. The conventional default namespace
files used is /lib/namespace, but most programs allow the user to specify an alternative with a
flag. It is here that we already can approximate a chroot style environment by changing the root
filesystem used in a namespace file.
.P1
bind #s /srv
mount /srv/myboot /root
bind -a /root /
.P2
By having another filesystem exposed in /srv/myboot and modifying the provided namespace file,
we've allowed this process to work within an entirely separate root filesystem.
.SH
RFNOMNT
.LP
The issue in using these namespaces as security barriers is that there is nothing preventing
a process from bootstrapping a resource back. While our example code places a different root filesystem
in the namespace, nothing is preventing that process or its children from potentially rebootstrapping
the real root filesystem back. For this issue there is a special rfork flag
.CW RFNOMNT
the prevents a process from accessing any almost any sharp device of consequence. This is done by
preventing a process from walking to a device by its location within '#'. This allows existing
binds of resources to continue working within the namespace but restricts a process from binding
in new resources from the kernel.
.LP
While effective we found this to be too large a hammer in practice. Doing as its name implies
.CW RFNOMNT
also prevents a process from performing any mounts or binds. This in practice creates a single
point in time in which a process gives up all of its control, instead of the idealized gradual
process. This makes it quite hard to make use of in practice, only a single program in a chain
may be the one to invoke
.CW RFNOMNT
or must hope that no other program further in the chain may want to make use of its namespace.
The interface itself feels very clunky, there is a nice gradual addition of these kernel devices
to the namespace why must the removal be all at once?
.SH
Chdev
.LP
We propose a new write interface through /dev/drivers
that functionally replaces
.CW RFNOMNT .
/dev/drivers now accepts writes in the form of
.P1
chdev op devmask
.P2
Devmask is a string of sharp device characters. Op specifies how
devmask is interpreted. Op is one of
.TS
lw(1i) lw(4.5i).
\f(CW&\fP	T{
Permit access to just the devices specified in devmask.
T}
\f(CW&~\fP	T{
Permit access to all but the devices specified in devmask.
T}
\f(CW~\fP	T{
Remove access to all devices.  Devmask is ignored.
T}
.TE
.LP
This allows a process to selectively remove access to
sections of sharp devices with quite a bit of control.
In order to mimic all of
.CW RFNOMNT 's
features, removing access to
.CW devmnt ,
which is not normally accessible directly,
disables the processes ability to perform mount
and bind operations.
.LP
For the implementation, we extended the existing
.CW RFNOMNT
flag attached to the process namespace group
into a bit vector. Each bit representing an index
into
.CW devtab .
The following function illustrates how this vector is set.
.P1
void
devmask(Pgrp *pgrp, int invert, char *devs)
{
	int i, t, w;
	char *p;
	Rune r;
	u64int mask[nelem(pgrp->notallowed)];

	if(invert)
		memset(mask, 0xFF, sizeof mask);
	else		
		memset(mask, 0, sizeof mask);		

	w = sizeof mask[0] * 8;
	for(p = devs; *p != 0;){
		p += chartorune(&r, p);
		t = devno(r, 1);
		if(t == -1)
			continue;
		if(invert)
			mask[t/w] &= ~(1<<t%w);
		else
			mask[t/w] |= 1<<t%w;
	}

	wlock(&pgrp->ns);
	for(i=0; i < nelem(pgrp->notallowed); i++)
		pgrp->notallowed[i] |= mask[i];
	wunlock(&pgrp->ns);
}
.P2
Devmask is called from the write handler for /dev/drivers. This
bitmask is then consulted any time a name is resolved that begins
with '#'. This is done from within the
.CW namec ()
function using the following function to check
if a particular device
.CW r
is permitted.
.P1
int
devallowed(Pgrp *pgrp, int r)
{
	int t, w, b;

	t = devno(r, 1);
	if(t == -1)
		return 0;

	w = sizeof(u64int) * 8;
	rlock(&pgrp->ns);
	b = !(pgrp->notallowed[t/w] & 1<<t%w);
	runlock(&pgrp->ns);
	return b;
}
.P2
.LP
We found that once removal is made to a core verb of these sharp
devices it becomes easy to start to view access to them
as capabilities. This is aided by system functionally already neatly
organized into the various devices themselves. For example, one could
say a process is capable of accessing the broader internet if it has access
to the
.CW devip
device. This access can either be direct via it's path under '#' or through a
location in the namespace where this device had already been bound. With these
changes, the entire capability list of a process is on display through just its
/proc/$pid/ns file. This
.CW ns
file would indicate if a particular device is bound and now also includes
the list of devices a process has access to.
.LP
In practice, this results in a pattern of binding
in a sharp device, making use of them and removing
them when no longer needed. A namespace file for
a web server could now look like
.P1
bind #s /srv
# /srv/www created by srvfs www /lib/www
mount /srv/www /lib/
unmount /srv
chdev -r s # chdev &~ s
.P2
In this example we have created a new root for the process by
using exportfs to expose a little piece of the boot namespace.
We unmount
.CW devsrv
and remove access to it with
.CW chdev
ensuring there is no way for our process to talk to the real
.CW /srv/boot .
This provides a nice succinct lifetime of access to
.CW devsrv
and makes the removal of these sharp devices as easy as
it is to use them in the first place. 
.LP
Like
.CW RFNOMNT ,
.CW chdev
does not restrict access to sharp devices that had already been mounted.
This allows a process to use a subsection or only one piece of
sharp devices as well. One example of this may be to restrict a process
to just a single network stack
.P1
bind '#I1' /net
chdev -r I
.P2
.SH
/srv/clone
.LP
With this
.CW chdev
mechanism, the ability for a device to provide isolation of its
own became more powerful. Partially illustrated in the previous
.CW devip
example.
.CW Devsrv ,
the sharp device providing named pipes, was an ideal target for
adding isolation. Devsrv provides a bulletin board of all posted 9p services
for a given host. We wanted to provide a mechanism for a process, or
family tree of process to share a private
.CW devsrv
between themselves.
.LP
The design for this was borrowed from devip, one in which a process opens a
.CW clone
file to read its newly allocated slot number. This new 'board' appears as a sibling directory
to the
.CW clone
it was spawned from. This new board is itself a fully functioning
.CW devsrv
with its own clone file, making nesting to full trees of
.CW srvs
quite easy, and completely transparent. The following illustrates
how one could replace their global
.CW /srv
with a freshly allocated one.
.P1
</srv/clone {
	s='/srv/'^`{read}
	bind -c $s /srv
	exec p
}
.P2
Also like devip, once the last reference to the file descriptor returned by opening
.CW clone
is closed the board is closed and posters to that board receive an EOF. It is important
to bake this kind of ownership into the design, as self referential users of
.CW /srv
are quite common in current code.
.LP
This along with chdev can be used to create a sandbox for /srv quite easily,
the process allocates itself a new /srv then removes access to the global
root srv. This allows potentially untrusted process to still make use of the interface
without needing to worry about their access to the global state. The practice of having
new boards appear as subdirectories allows the entire state to easily be seen by inspecting the
root of devsrv itself.
.SH
Restricting Within a Mount
.LP
As shown earlier with the use of
.CW srvfs ,
an intermediate file server can be used to only service a small subsection of a larger
namespace. In that example we used this to expose only /lib/www from the host to processes
running a web server. This can be limited as the invocation of
.CW exportfs
can become more complicated if the user wishes to use multiple pieces from completely
separate places within the file tree. To address this a utility program
.CW protofs
was written to easily create convincing mimics of the filesystem it was run from.
.CW protofs
accepts a
.CW proto
file, a text file containing a description of file tree, and uses it to provide
dummy files mimicking the structure. These dummies can then be used by a process as targets
for bind mounts of its current namespace, providing the illusion of trimming all but select
pieces. This new root cannot be simply bound over the real one, that still allows an unmount
to escape back to the real system, but rexporting the namespace still works. To illustrate a
more involved setup than before:
.P1
# We want to provide our web server
# with /bin, /lib/www and /lib/git
; cat >>/tmp/proto <<.
bin	d775
lib	d775
	www	d775
	git	d775
.
; protofs -m /mnt/proto /tmp/prot
; bind /bin /mnt/proto/bin
; bind /lib/www /mnt/proto/lib/www
; bind /lib/git /mnt/proto/lib/git
# A private srv could be used, omitted for brevity
; srvfs webbox /mnt/proto
# Namespace file for using our new mini-root
; cat >>/tmp/ns <<.
mount #s/webbox /root
bind -b /root /
chdev -r s
.
; auth/newns -n /tmp/ns ls /
bin
lib
; 
.P2
.SH
Future Work
.LP
While we think these bring us closer to namespaces as security boundaries,
there is still plenty of work and understanding to be done. One particular
item of interest is attempting some kind of isolation of
.CW devproc ,
possibly in a similar fashion to the
.CW /srv/clone
implementation, but attempts have yet to be made. The exact nature of
.CW namespace
files and how they relate to sandboxing as a whole has yet to be fully
worked out. There is clear potential, but it is likely additional abilities may
be required. It is somewhat difficult to synthesize a namespace entirely
from nothing, which is something we found ourselves reaching for when building
alternative roots to run processes within. There is potential for some merger
of
.CW proto
and
.CW namespace
files to provide a template of the current namespace to graft on to the next one.
.LP
Both
.CW chdev
and
.CW /srv/clone
are merged into 9front and their implementations are freely available as part of the base system.
.SH REFERENCES
.LP
[Beck18]
Bob Beck,
``Pledge, and Unveil, in OpenBSD'',
.I "BSDCan Slides"
Ottawa,
July, 2018.
.LP
[Price04]
Daniel Price,
Andrew Tucker,
``Solaris Zones: Operating System Support for Consolidating Commercial Workloads'',
.I "Proceedings of the 18th Large Installation System Administration Conference"
pp. 241-254,
Atlanta,
November, 2004.
.LP
[Biederman06]
Eric W. Biederman
``Multiple Instances of the Global Linux Namespaces'',
.I "Proceedings of the 2006 Linux Symposium Volume One"
pp. 102-112,
Ottawa, Ontario
July, 2006.
