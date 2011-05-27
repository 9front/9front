// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: m_bbox.c,v 1.1 1997/02/03 22:45:10 b1 Exp $";

#include "doomdef.h"
#include "doomstat.h"

// #include "i_system.h"
// #include "d_event.h"
#include "d_net.h"
#include "m_argv.h"

#include "i_net.h"


//
// I_InitNetwork
//
void I_InitNetwork (void)
{
printf("PORTME i_net.c I_InitNetwork (use 9P)\n");

	doomcom = malloc (sizeof(*doomcom));
	memset (doomcom, 0, sizeof(*doomcom));

	/* set up for network */
	doomcom->ticdup = 1;
	doomcom->extratics = 0;

//	netsend = PacketSend;
//	netget = PacketGet;
//	netgame = true;

	/* parse player number and host list */
//	doomcom->consoleplayer = myargv[i+1][0]-'1';

	doomcom->numnodes = 1;	// this node for sure

	doomcom->id = DOOMCOM_ID;
	doomcom->numplayers = doomcom->numnodes;

/*
    boolean		trueval = true;
    int			i;
    int			p;
    struct hostent*	hostentry;	// host information entry
	
    doomcom = malloc (sizeof (*doomcom) );
    memset (doomcom, 0, sizeof(*doomcom) );
    
    // set up for network
    i = M_CheckParm ("-dup");
    if (i && i< myargc-1)
    {
	doomcom->ticdup = myargv[i+1][0]-'0';
	if (doomcom->ticdup < 1)
	    doomcom->ticdup = 1;
	if (doomcom->ticdup > 9)
	    doomcom->ticdup = 9;
    }
    else
	doomcom-> ticdup = 1;
	
    if (M_CheckParm ("-extratic"))
	doomcom-> extratics = 1;
    else
	doomcom-> extratics = 0;
		
    p = M_CheckParm ("-port");
    if (p && p<myargc-1)
    {
	DOOMPORT = atoi (myargv[p+1]);
	printf ("using alternate port %i\n",DOOMPORT);
    }
    
    // parse network game options,
    //  -net <consoleplayer> <host> <host> ...
    i = M_CheckParm ("-net");
    if (!i)
    {
	// single player game
	netgame = false;
	doomcom->id = DOOMCOM_ID;
	doomcom->numplayers = doomcom->numnodes = 1;
	doomcom->deathmatch = false;
	doomcom->consoleplayer = 0;
	return;
    }

    netsend = PacketSend;
    netget = PacketGet;
    netgame = true;

    // parse player number and host list
    doomcom->consoleplayer = myargv[i+1][0]-'1';

    doomcom->numnodes = 1;	// this node for sure
	
    i++;
    while (++i < myargc && myargv[i][0] != '-')
    {
	sendaddress[doomcom->numnodes].sin_family = AF_INET;
	sendaddress[doomcom->numnodes].sin_port = htons(DOOMPORT);
	if (myargv[i][0] == '.')
	{
	    sendaddress[doomcom->numnodes].sin_addr.s_addr 
		= inet_addr (myargv[i]+1);
	}
	else
	{
	    hostentry = gethostbyname (myargv[i]);
	    if (!hostentry)
		I_Error ("gethostbyname: couldn't find %s", myargv[i]);
	    sendaddress[doomcom->numnodes].sin_addr.s_addr 
		= *(int *)hostentry->h_addr_list[0];
	}
	doomcom->numnodes++;
    }
	
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes;
*/
}


void I_NetCmd (void)
{
/*
    if (doomcom->command == CMD_SEND)
    {
	netsend ();
    }
    else if (doomcom->command == CMD_GET)
    {
	netget ();
    }
    else
	I_Error ("Bad net cmd: %i\n",doomcom->command);
*/
}
