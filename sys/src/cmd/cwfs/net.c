/* network i/o */

#include "all.h"
#include "io.h"

/*
 * the kernel file server read packets directly from
 * its ethernet(s) and did all the protocol processing.
 * if the incoming packets were 9p (over il/ip), they
 * were queued for the server processes to operate upon.
 *
 * in user mode, we have one process per incoming connection
 * instead, and those processes get just the data, minus
 * tcp and ip headers, so they just see a stream of 9p messages,
 * which they then queue for the server processes.
 *
 * there used to be more queueing (in the kernel), with separate
 * processes for ethernet input, il input, 9p processing, il output
 * and ethernet output, and queues connecting them.  we now let
 * the kernel's network queues, protocol stacks and processes do
 * much of this work.
 *
 * partly as a result of this, we can now process 9p messages
 * transported via tcp, exploit multiple x86 processors, and
 * were able to shed 70% of the file server's source, by line count.
 *
 * the upshot is that Ether (now Network) is no longer a perfect fit for
 * the way network i/o is done now.  the notion of `connection'
 * is being introduced to complement it.
 */

typedef struct Network Network;

/* a network, not necessarily an ethernet */
struct Network {
	int	ctlrno;
	char	name[NAMELEN];

	char	*dialstr;
	char	anndir[40];
	char	lisdir[40];
	int	annfd;			/* fd from announce */
};

static Network netif[Maxnets];

char *annstrs[Maxnets];

static void
neti(void *v)
{
	int lisfd, accfd;
	NetConnInfo *nci;
	Network *net;

	net = v;
	for(;;) {
		if((lisfd = listen(net->anndir, net->lisdir)) < 0){
			fprint(2, "%s: listen %s failed: %r\n", argv0, net->anndir);
			break;
		}
		/* got new call on lisfd */
		if((accfd = accept(lisfd, net->lisdir)) < 0){
			fprint(2, "%s: accept %d (from %s) failed: %r\n", argv0, lisfd, net->lisdir);
			close(lisfd);
			continue;
		}
		nci = getnetconninfo(net->lisdir, accfd);
		if(srvchan(accfd, nci->raddr) == nil){
			fprint(2, "%s: srvchan failed for: %s\n", argv0, nci->raddr);
			close(accfd);
		}
		freenetconninfo(nci);
	}
}

void
netstart(void)
{
	Network *net;

	for(net = &netif[0]; net < &netif[Maxnets]; net++){
		if(net->dialstr == nil || *net->anndir == 0)
			continue;
		sprint(net->name, "net%di", net->ctlrno);
		newproc(neti, net, net->name);
	}
}

void
netinit(void)
{
	Network *net;

	for (net = netif; net < netif + Maxnets; net++) {
		net->dialstr = annstrs[net - netif];
		if(net->dialstr == nil)
			continue;
		if((net->annfd = announce(net->dialstr, net->anndir)) < 0){
			fprint(2, "can't announce %s: %r", net->dialstr);
			net->dialstr = nil;
			continue;
		}
		if(chatty)
			print("netinit: announced on %s\n", net->dialstr);
	}
}
