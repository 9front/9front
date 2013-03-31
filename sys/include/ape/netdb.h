#ifndef __NETDB_H__
#define __NETDB_H__

#ifndef _BSD_EXTENSION
    This header file is an extension to ANSI/POSIX
#endif

#pragma lib "/$M/lib/ape/libbsd.a"

/*-
 * Copyright (c) 1980, 1983, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)netdb.h	5.11 (Berkeley) 5/21/90
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Structures returned by network data base library.  All addresses are
 * supplied in host order, and returned in network order (suitable for
 * use in system calls).
 */
struct	hostent {
	char	*h_name;	/* official name of host */
	char	**h_aliases;	/* alias list */
	int	h_addrtype;	/* host address type */
	int	h_length;	/* length of address */
	char	**h_addr_list;	/* list of addresses from name server */
#define	h_addr	h_addr_list[0]	/* address, for backward compatiblity */
};

/*
 * Assumption here is that a network number
 * fits in 32 bits -- probably a poor one.
 */
struct	netent {
	char		*n_name;	/* official name of net */
	char		**n_aliases;	/* alias list */
	int		n_addrtype;	/* net address type */
	unsigned long	n_net;		/* network # */
};

struct	servent {
	char	*s_name;	/* official service name */
	char	**s_aliases;	/* alias list */
	int	s_port;		/* port # */
	char	*s_proto;	/* protocol to use */
};

struct	protoent {
	char	*p_name;	/* official protocol name */
	char	**p_aliases;	/* alias list */
	int	p_proto;	/* protocol # */
};

/* from 4.0 RPCSRC */
struct rpcent {
	char	*r_name;	/* name of server for this rpc program */
	char	**r_aliases;	/* alias list */
	int	r_number;	/* rpc program number */
};

extern struct hostent	*gethostbyname(const char *),
			*gethostbyaddr(const void *, int, int),
			*gethostent(void);
extern struct netent	*getnetbyname(const char *),
			*getnetbyaddr(long, int),
			*getnetent(void);
extern struct servent	*getservbyname(const char *, const char *),
			*getservbyport(int, const char *),
			*getservent(void);
extern struct protoent	*getprotobyname(const char *),
			*getprotobynumber(int),
			*getprotoent(void);
extern struct rpcent	*getrpcbyname(const char *), 
			*getrpcbynumber(int), 
			*getrpcent(void);
extern void sethostent(int),  endhostent(void),
	    setnetent(int),   endnetent(void),
	    setservent(int),  endservent(void),
	    setprotoent(int), endprotoent(void),
	    setrpcent(int),   endrpcent(void);

/*
 * Error return codes from gethostbyname() and gethostbyaddr()
 * (left in extern int h_errno).
 */
extern int h_errno;
extern void herror(const char *);
extern char *hstrerror(int);

#define	HOST_NOT_FOUND	1 /* Authoritative Answer Host not found */
#define	TRY_AGAIN	2 /* Non-Authoritive Host not found, or SERVERFAIL */
#define	NO_RECOVERY	3 /* Non recoverable errors, FORMERR, REFUSED, NOTIMP */
#define	NO_DATA		4 /* Valid name, no data record of requested type */
#define	NO_ADDRESS	NO_DATA		/* no address, look for MX record */

#define __HOST_SVC_NOT_AVAIL 99		/* libc internal use only */

struct	addrinfo {
	int		ai_flags;	/* Input flags.  */
	int		ai_family;	/* Protocol family for socket.  */
	int		ai_socktype;	/* Socket type.  */
	int		ai_protocol;	/* Protocol for socket.  */
	int		ai_addrlen;	/* Length of socket address.  */
	struct sockaddr	*ai_addr;	/* Socket address for socket.  */
	char		*ai_canonname;	/* Canonical name for service location.  */
	struct addrinfo	*ai_next;	/* Pointer to next in list.  */
};

extern int	getaddrinfo(char *, char *, struct addrinfo *, struct addrinfo **);
extern void	freeaddrinfo(struct addrinfo *);
extern int	getnameinfo(struct sockaddr *, int, char *, int, char *, int, unsigned int);
extern char	*gai_strerror(int);

/* Possible values for `ai_flags' field in `addrinfo' structure.  */
#define AI_PASSIVE	0x0001	/* Socket address is intended for `bind'.  */
#define AI_CANONNAME	0x0002	/* Request for canonical name.  */
#define AI_NUMERICHOST	0x0004	/* Don't use name resolution.  */
#define AI_V4MAPPED	0x0008	/* IPv4 mapped addresses are acceptable.  */
#define AI_ALL		0x0010	/* Return IPv4 mapped and IPv6 addresses.  */
#define AI_ADDRCONFIG	0x0020	/* Use configuration of this host to choose returned address type..  */
#define AI_NUMERICSERV	0x0400	/* Don't use name resolution.  */

/* getnameinfo flags */
#define NI_NOFQDN	0x0001	/* Only the nodename portion of the FQDN is returned for local hosts. */
#define NI_NUMERICHOST	0x0002	/* The numeric form of the node's address is returned instead of its name. */
#define NI_NAMEREQD	0x0004	/* Return an error if the node's name cannot be located in the database. */
#define NI_NUMERICSERV	0x0008	/* The numeric form of the service address is returned instead of its name. */
#define NI_NUMERICSCOPE	0x0010	/* For IPv6 addresses, the numeric form of the scope identifier is returned
				   instead of its name. */
#define NI_DGRAM	0x0020	/* Indicates that the service is a datagram service (SOCK_DGRAM). */

/* Error values for `getaddrinfo' and `getnameinfo' functions.  */
#define EAI_BADFLAGS	  -1	/* Invalid value for `ai_flags' field */
#define EAI_NONAME	  -2	/* NAME or SERVICE is unknown */
#define EAI_AGAIN	  -3	/* Temporary failure in name resolution */
#define EAI_FAIL	  -4	/* Non-recoverable failure in name resolution */
#define EAI_NODATA	  -5	/* No address associated with NAME */
#define EAI_FAMILY	  -6	/* `ai_family' not supported */
#define EAI_SOCKTYPE	  -7	/* `ai_socktype' not supported */
#define EAI_SERVICE	  -8	/* SERVICE not supported for `ai_socktype' */
#define EAI_ADDRFAMILY	  -9	/* Address family for NAME not supported */
#define EAI_MEMORY	  -10	/* Memory allocation failure */
#define EAI_SYSTEM	  -11	/* System error returned in `errno' */
#define EAI_OVERFLOW	  -12	/* Argument buffer overflow */

#ifdef __cplusplus
}
#endif

#endif /* !__NETDB_H__ */
