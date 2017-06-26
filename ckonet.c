char *cknetv = "OS/2 Network support, 5A(008) 10 Jul 91";

/*  C K O N E T  --  Network support  */
/*
  Copyright (C) 1985, 1992, Trustees of Columbia University in the City of New
  York.  Permission is granted to any individual or institution to use this
  software as long as it is not sold for profit.  This copyright notice must be
  retained.  This software may not be included in commercial products without
  written permission of Columbia University.
*/

/* Currently supported network services:

 - DECnet LAT

 */

#include "ckcdeb.h"
#include "ckcker.h"
#include "ckcnet.h"

#include <io.h>
#include <fcntl.h>
#include <string.h>

int ttnproto = NP_NONE;			/* Network virtual terminal protocol */

#ifndef NETCONN
/*
  Network support not defined.
  Dummy functions here in case #ifdef's forgotten elsewhere.
*/
int					/* Open network connection */
netopen(name, lcl, nett) char *name; int *lcl, nett; {
    return(-1);
}
int					/* Close network connection */
netclos() {
    return(-1);
}
int					/* Check network input buffer */
nettchk() {
    return(-1);
}
int					/* Flush network input buffer */
netflui() {
    return(-1);
}
int					/* Send network BREAK */
netbreak() {
    return(-1);
}
int					/* Input character from network */
netinc(timo) int timo; {
}
int					/* Output character to network */
#ifdef CK_ANSIC
nettoc(char c)
#else
nettoc(c) char c;
#endif /* CK_ANSIC */
/* nettoc */ {
    return(-1);
}
int
nettol(s,n) char *s; int n; {
    return(-1);
}

#else /* NETCONN is defined (rest of this module...) */

#ifndef __32BIT__
#define far _far
#define near _near
#define pascal _pascal
#endif
#define	INCL_NOPM
#define	INCL_DOSPROCESS
#define	INCL_DOSMODULEMGR
#include <os2.h>

#include "ckolat.h"

#ifdef __32BIT__
short (* _Far16 _Pascal LATENTRY)(struct lat_cb *);
#else
int (_far _pascal *LATENTRY)(struct lat_cb *);
#endif

extern int duplex, debses, seslog, ttyfd, quiet; /* External variables */
extern int nettype;

int tn_init = 0;			/* Telnet protocol initialized flag */
int telnetfd;

char ipaddr[20];

static struct lat_cb lcb;

/*  N E T O P E N  --  Open a network connection.  */
/*  Returns 0 on success, -1 on failure.  */

/*
  Calling conventions same as ttopen(), except third argument is network
  type rather than modem type.  Designed to be called from within ttopen.
*/

int
netopen(name, lcl, nett) char *name; int *lcl, nett; {

    char failname[256];
    HMODULE hLatCalls;

    if ( LATENTRY == NULL )
    {
      if ( DosLoadModule(failname, sizeof(failname), "LATCALLS", &hLatCalls) )
        return -1;
#ifdef __32BIT__
      if ( DosQueryProcAddr(hLatCalls, 0, "LATENTRY", (PFN *) &LATENTRY) )
        return -1;
#else
      if ( DosGetProcAddr(hLatCalls, "LATENTRY", &LATENTRY) )
        return -1;
#endif
    }

    ttnproto = NP_LAT;

    printf("Trying connection to %s ... ", name);

    lcb.LatFunction = START_SESSION;
    lcb.BufferSize = strlen(name);
    lcb.BufferPtr = (void *) name;

    LATENTRY(&lcb);

    ttyfd = lcb.SessionHandle;
    ipaddr[0] = 0;

    printf(lcb.LatStatus ? "failed.\n" : "OK.\n");

    return (lcb.LatStatus == 0) ? 0 : -1;
}

/*  N E T C L O S  --  Close current network connection.  */

int
netclos() {
    int x = 0;

    if (ttyfd < 0)			/* Was open? */
      return(0);			/* Wasn't. */

    if (ttyfd > -1)			/* Was. */
    {
      lcb.LatFunction = STOP_SESSION;
      lcb.SessionHandle = ttyfd;

      LATENTRY(&lcb);

      x = (lcb.LatStatus == 0) ? 0 : -1;
    }

    ttyfd = -1;				/* Mark it as closed. */
    ipaddr[0] = 0;

    return(x);
}

/*  N E T T C H K  --  Check if network up, and how many bytes can be read */
/*
  Returns number of bytes waiting, or -1 if connection has been dropped.
*/
int					/* Check how many bytes are ready */
nettchk() {				/* for reading from network */
    return(0);
}

/*  N E T T I N C --  Input character from network */

int
netinc(timo) int timo; {

    static char buffer[256];
    static int size = 0, pos = 0;
    int chr;

    if ( pos < size )
      return buffer[pos++];

    lcb.LatFunction = GET_CHAR_BLK;
    lcb.SessionHandle = ttyfd;
    lcb.BufferSize = sizeof(buffer);
    lcb.BufferPtr = (void *) buffer;
    lcb.WaitTime = timo < 0 ? 10L * -timo : 1000L * timo;

    LATENTRY(&lcb);

    if ( (lcb.SessionStatus & 0xFF) == SS_Stopped )
      return -2;
    if ( lcb.LatStatus )
      return -1;

    pos = 0;
    size = lcb.BufferSize;

    chr = buffer[pos++];

    return chr;
}

/*  N E T T O C  --   Output character to network */
/*
  Call with character to be transmitted.
  Returns 0 if transmission was successful, or
  -1 upon i/o error, or -2 if called improperly.
*/
int
#ifdef CK_ANSIC
nettoc(char c)
#else
nettoc(c) char c;
#endif /* CK_ANSIC */
/* nettoc */ {

    lcb.LatFunction = SEND_CHAR;
    lcb.SessionHandle = ttyfd;
    lcb.CharByte = c;

    LATENTRY(&lcb);

    return (lcb.LatStatus == 0) ? 0 : -1;
}

/*  N E T T O L  --  Output a string of bytes to the network  */
/*
  Call with s = pointer to string, n = length.
  Returns number of bytes actuall written on success, or
  -1 on i/o error, -2 if called improperly.
*/
int
nettol(s,n) char *s; int n; {

    int b;

    for ( b = 0; b < n; b++, s++ )
      if ( nettoc(*s) )
        break;

    return(b);
}

/*  N E T F L U I  --  Flush network input buffer  */

int
netflui() {
    return(0);
}

/* Send network BREAK */
/*
  Returns -1 on error, 0 if nothing happens, 1 if BREAK sent successfully.
*/
int
netbreak() {
    lcb.LatFunction = SEND_BREAK;
    lcb.SessionHandle = ttyfd;

    LATENTRY(&lcb);

    return (lcb.LatStatus == 0) ? 0 : -1;
}

#endif /* NETCONN */
