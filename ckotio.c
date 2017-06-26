char *ckxv = "OS/2 Communications I/O, 16 Nov 92";

/* C K O T I O  --  Kermit communications I/O support for OS/2 systems */

/*
  Also contains code to emulate the Unix 'alarm' function under OS/2
  and a set of opendir/readdir/closedir etc. functions.
*/

/*
 Author: Chris Adie (C.Adie@uk.ac.edinburgh)
 Copyright (C) 1988 Edinburgh University Computing Service
 Permission is granted to any individual or institution to use, copy, or
 redistribute this software so long as it is not sold for profit, provided
 this copyright notice is retained.

 Many changes for 5A by Kai Uwe Rommel (rommel@informatik.tu-muenchen.de)
*/

/* Includes */

#include "ckcdeb.h"			/* Typedefs, debug formats, etc */
#include "ckcker.h"			/* Kermit definitions */
#include "ckcnet.h"			/* Kermit definitions */
#include "ckuxla.h"			/* Translation tables */

#include <ctype.h>			/* Character types */
#include <stdio.h>			/* Standard i/o */
#include <io.h>				/* File io function declarations */
#include <fcntl.h>
#include <process.h>			/* Process-control functions */
#include <string.h>			/* String manipulation declarations */
#include <stdlib.h>			/* Standard library declarations */
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>			/* Time functions */
#include <signal.h>
#include <assert.h>
#include <setjmp.h>

#include "ckodir.h"

#include "ckuver.h"			/* Version herald */
char ckxsystem[64] = HERALD;
char *ckxsys = ckxsystem;

#define DEL 127
#define HUPTIME 500			/* Milliseconds for hangup */

#ifndef __32BIT__
#define far _far
#define near _near
#define pascal _pascal
#endif
#define	INCL_NOPM
#define	INCL_ERRORS
#define	INCL_KBD
#define	INCL_DOSPROCESS
#define	INCL_DOSQUEUES
#define	INCL_DOSSIGNALS
#define	INCL_DOSDEVICES
#define	INCL_DOSDEVIOCTL
#define	INCL_DOSNLS
#include <os2.h>	/* This pulls in a whole load of stuff */
#undef COMMENT

#ifdef CHAR
#undef CHAR
#endif /* CHAR */

/*============================================================================*/

/*
 Variables available to outside world:

   dftty  -- Pointer to default tty name string, like "/dev/tty".
   dfloc  -- 0 if dftty is console, 1 if external line.
   dfprty -- Default parity
   dfflow -- Default flow control
   ckxech -- Flag for who echoes console typein:
     1 - The program (system echo is turned off)
     0 - The system (or front end, or terminal).
   functions that want to do their own echoing should check this flag
   before doing so.

 Functions for assigned communication line (either external or console tty):

   sysinit()               -- System dependent program initialization
   syscleanup()            -- System dependent program shutdown
   ttopen(ttname,local,mdmtyp) -- Open the named tty for exclusive access.
   ttclos()                -- Close & reset the tty, releasing any access lock.
   ttpkt(speed,flow,parity)-- Put the tty in packet mode
				or in DIALING or CONNECT modem control state.
   ttvt(speed,flow)        -- Put the tty in virtual terminal mode.
   ttinl(dest,max,timo)    -- Timed read line from the tty.
   ttinc(timo)             -- Timed read character from tty.
   ttchk()                 -- See how many characters in tty input buffer.
   ttxin(n,buf)            -- Read n characters from tty (untimed).
   ttol(string,length)     -- Write a string to the tty.
   ttoc(c)                 -- Write a character to the tty.
   ttflui()                -- Flush tty input buffer.
   ttgspd()                -- Speed of tty line.

Functions for console terminal:

   conraw()  -- Set console into Raw mode
   concooked() -- Set console into Cooked mode
   conoc(c)  -- Unbuffered output, one character to console.
   conol(s)  -- Unbuffered output, null-terminated string to the console.
   conola(s) -- Unbuffered output, array of strings to the console.
   conxo(n,s) -- Unbuffered output, n characters to the console.
   conchk()  -- Check if characters available at console (bsd 4.2).
		Check if escape char (^\) typed at console (System III/V).
   coninc(timo)  -- Timed get a character from the console.
 Following routines are dummies:
   congm()   -- Get console terminal mode.
   concb()   -- Put console into single char mode with no echo.
   conres()  -- Restore console to congm mode.
   conint()  -- Enable console terminal interrupts.
   connoi()  -- No console interrupts.

Time functions

   sleep(t)  -- Like UNIX sleep
   msleep(m) -- Millisecond sleep
   ztime(&s) -- Return pointer to date/time string
   rtimer() --  Reset timer
   gtimer()  -- Get elapsed time since last call to rtimer()
*/


/* Defines */

#define DEVNAMLEN	14

/* Declarations */

/* dftty is the device name of the default device for file transfer */
/* dfloc is 0 if dftty is the user's console terminal, 1 if an external line */

extern long speed;
extern int parity, fcharset, flow, ttcarr;

#ifdef COMMENT
/* This is to allow remote operation */
    char *dftty = "0"; /* stdin */
    int dfloc = 0;
#else
    char *dftty = "com1"; /* COM1 */
    int dfloc = 1;
#endif /* COMMENT */

    int dfprty = 0;			/* Default parity (0 = none) */
    int ttprty = 0;			/* Parity in use. */
    int ttmdm = 0;			/* Modem in use. */
    int dfflow = FLO_KEEP;		/* Default flow is KEEP, no change*/
    int backgrd = 0;			/* Assume in foreground (no '&' ) */
    int ttcarr = CAR_AUT;		/* Carrier handling mode. */

int ckxech = 1; /* 0 if system normally echoes console characters, else 1 */

/* Declarations of variables global within this module */

static struct rdchbuf_rec {		/* Buffer for serial characters */
	unsigned char buffer[256];
	UINT length, index;
} rdchbuf;

char startupdir[CCHMAXPATH] = ".";

static long tcount;			/* Elapsed time counter */
static int conmode, consaved;
static char ttnmsv[DEVNAMLEN];
static int islocal;
static int pid;

#ifdef COMMENT
int ttyfd = 0;		/* TTY file descriptor = stdin */
#else
int ttyfd = -1;		/* TTY file descriptor (not open yet) */
#endif /* COMMENT */

DCBINFO ttydcb;

extern KEY 	*keymap;
extern MACRO 	*macrotab;

int ttsetspd(long sp);

#ifdef __32BIT__

USHORT DosDevIOCtl32(PVOID pData, USHORT cbData, PVOID pParms, USHORT cbParms,
		     USHORT usFunction, USHORT usCategory, HFILE hDevice)
{
  ULONG ulParmLengthInOut = cbParms, ulDataLengthInOut = cbData;
  return (USHORT) DosDevIOCtl(hDevice, usCategory, usFunction,
			      pParms, cbParms, &ulParmLengthInOut,
			      pData, cbData, &ulDataLengthInOut);
}

#define DosDevIOCtl DosDevIOCtl32

#else

#define DosDevIOCtl(pd, cbd, pp, cbp, f, c, h) DosDevIOCtl(pd, pp, f, c, h)

#endif


void cc_trap(int sig)
{
  signal(sig, cc_trap);
#ifdef __EMX__
  signal(sig, SIG_ACK);
#endif
}

/* Saving/restoring of hot handles */

static int savedtty = 0;
static long savedspeed;
static LINECONTROL savedlc;
static DCBINFO saveddcb;
static BYTE savedstat;

savetty() {

  if (ttyfd != -1) {
    savedspeed = ttgspd();
    DosDevIOCtl(&savedlc,sizeof(savedlc),NULL,0,
		ASYNC_GETLINECTRL,IOCTL_ASYNC,ttyfd);
    DosDevIOCtl(&saveddcb,sizeof(saveddcb),NULL,0,
		ASYNC_GETDCBINFO,IOCTL_ASYNC,ttyfd);
    DosDevIOCtl(&savedstat,sizeof(savedstat),NULL,0,
		ASYNC_GETMODEMOUTPUT,IOCTL_ASYNC,ttyfd);
    savedtty = 1;
  }

  return 0;
}

restoretty() {
  MODEMSTATUS ms;
  UINT data;

  if (savedtty) {
    ttsetspd(savedspeed);
    DosDevIOCtl(NULL,0,&savedlc,sizeof(savedlc),
		ASYNC_SETLINECTRL,IOCTL_ASYNC,ttyfd);
    DosDevIOCtl(NULL,0,&saveddcb,sizeof(saveddcb),
		ASYNC_SETDCBINFO,IOCTL_ASYNC,ttyfd);
    ms.fbModemOn = 0;
    ms.fbModemOff = 255;
    if (savedstat & DTR_ON) ms.fbModemOn |= DTR_ON;
    else ms.fbModemOff &= DTR_OFF;
    if (savedstat & RTS_ON) ms.fbModemOn |= RTS_ON;
    else ms.fbModemOff &= RTS_OFF;
    DosDevIOCtl(&data,sizeof(data),&ms,sizeof(ms),
		ASYNC_SETMODEMCTRL,IOCTL_ASYNC,ttyfd);
    savedtty = 0;
  }

  return 0;
}

/*  S Y S I N I T  --  System-dependent program initialization.  */

sysinit() {
    char *ptr;
    int n;
#ifdef __32BIT__
    ULONG cbData, nCodePage[8];
#else
    USHORT cbData, nCodePage[8];
#endif
    if ( DosGetCp(sizeof(nCodePage), nCodePage, &cbData) == 0 )
      switch (nCodePage[0]) {
      case 437:
	fcharset = FC_CP437;
	break;
      case 850:
	fcharset = FC_CP850;
	break;
      case 852:
	fcharset = FC_CP852;
	break;
      }
    pid = getpid();
    signal(SIGINT, cc_trap);
    signal(SIGBREAK, cc_trap);
    sprintf(ckxsystem, " OS/2 %1d.%02d", _osmajor / 10, _osminor);
#ifdef __IBMC__
    setvbuf(stdout, NULL, _IONBF, 0);
#endif
    strcpy(startupdir, GetLoadPath());
    if ( (ptr = strrchr(startupdir, '\\')) != NULL )
      *ptr = 0;
    for (ptr = startupdir; *ptr; ptr++)	/* Convert backslashes to slashes */
      if (*ptr == '\\')
	*ptr = '/';
    n = (int)strlen(startupdir);	/* Add slash to end if necessary */
    if (n > -1 && n < CCHMAXPATH)
      if (startupdir[n-1] != '/') {
	  startupdir[n] = '/';
	  startupdir[n+1] = '\0';
      }
#ifdef COMMENT
/* This is a bit UNIXish and disconcerting */
    strlwr(startupdir);
#endif /* COMMENT */
#ifdef __IBMC__
    setmode(1, O_TEXT);
#endif /* __IBMC__ */
    strcpy(ttnmsv, dftty);
    islocal = isatty(0) && !ttiscom(0);
    if (!islocal) {
      os2setdtr(1);
      ttsettings(ttprty,0);
      os2setflow(flow);
      os2setcarr(ttcarr == CAR_ON);
    }
    return(0);
}


/*  S Y S C L E A N U P  --  System-dependent program cleanup.  */

syscleanup() {
    signal(SIGINT, SIG_DFL);
    signal(SIGBREAK, SIG_DFL);
    return(0);
}


/*  O S 2 S E T F L O W -- set flow state of tty */

os2setflow(int nflow)
{
    /* Get the current settings */
    if (DosDevIOCtl(&ttydcb,sizeof(ttydcb),NULL,0,
		    ASYNC_GETDCBINFO,IOCTL_ASYNC,ttyfd))
        return(-1);

    ttydcb.fbCtlHndShake = MODE_DTR_CONTROL; 
    ttydcb.fbFlowReplace &= ~(MODE_AUTO_RECEIVE | MODE_AUTO_TRANSMIT |
    /* clear only a few */    MODE_RTS_CONTROL  | MODE_RTS_HANDSHAKE);

    if (nflow == FLO_XONX) {
        ttydcb.fbFlowReplace |= 
	  (MODE_AUTO_RECEIVE | MODE_AUTO_TRANSMIT | MODE_RTS_CONTROL);
    }
    else if (nflow == FLO_RTSC) {
        ttydcb.fbCtlHndShake |= MODE_CTS_HANDSHAKE;
	ttydcb.fbFlowReplace |= MODE_RTS_HANDSHAKE;
    }
    else
        ttydcb.fbFlowReplace |= MODE_RTS_CONTROL;

    /* Read "some" data from line mode */
    ttydcb.fbTimeout &= ~MODE_NOWAIT_READ_TIMEOUT;
    ttydcb.fbTimeout |= MODE_WAIT_READ_TIMEOUT;

    /* Set DCB */
    if (DosDevIOCtl(NULL,0,&ttydcb,sizeof(ttydcb),
		    ASYNC_SETDCBINFO,IOCTL_ASYNC,ttyfd))
        return(-1);

    if (nflow != FLO_RTSC) {        /* keep RTS permanently on */
        MODEMSTATUS ms;
	UINT data;
	ms.fbModemOn = RTS_ON;
	ms.fbModemOff = 255;
	DosDevIOCtl(&data,sizeof(data),&ms,sizeof(ms),
		    ASYNC_SETMODEMCTRL,IOCTL_ASYNC,ttyfd);
    }

    return(0);
}


os2setcarr(int ncarr)
{
    /* Get the current settings */
    if (DosDevIOCtl(&ttydcb,sizeof(ttydcb),NULL,0,
		    ASYNC_GETDCBINFO,IOCTL_ASYNC,ttyfd))
        return(-1);

    if (ncarr)
      ttydcb.fbCtlHndShake |=  MODE_DCD_HANDSHAKE;
    else
      ttydcb.fbCtlHndShake &= ~MODE_DCD_HANDSHAKE;

    /* Set DCB */
    if (DosDevIOCtl(NULL,0,&ttydcb,sizeof(ttydcb),
		    ASYNC_SETDCBINFO,IOCTL_ASYNC,ttyfd))
        return(-1);

    return(0);
}


/*  O S 2 S E T D T R -- set state of DTR signal */

os2setdtr(int on)
{
    MODEMSTATUS ms;
    UINT data;

    if (ttyfd == -1) return(0);
    ms.fbModemOn = on ? DTR_ON : 0;
    ms.fbModemOff = on ? 255 : DTR_OFF;
    return(DosDevIOCtl(&data,sizeof(data),&ms,sizeof(ms),
		       ASYNC_SETMODEMCTRL,IOCTL_ASYNC,ttyfd));
}


/*  T T S E T T I N G S  --  Set the device driver parity and stop bits */

ttsettings(int par, int stop) {
    LINECONTROL lc;

    if (DosDevIOCtl(&lc,sizeof(lc),NULL,0,
		    ASYNC_GETLINECTRL,IOCTL_ASYNC,ttyfd))
      return(-1); /* Get line */

    switch (par) {
	case 'o':
	    lc.bDataBits = 7;	/* Data bits */
	    lc.bParity   = 1;
	    break;
    	case 'e':
	    lc.bDataBits = 7;	/* Data bits */
	    lc.bParity   = 2;
	    break;
	case 'm':
	    lc.bDataBits = 7;	/* Data bits */
	    lc.bParity   = 3;
	    break;
	case 's':
	    lc.bDataBits = 7;	/* Data bits */
	    lc.bParity   = 4;
	    break;
	default :
	    lc.bDataBits = 8;	/* Data bits */
	    lc.bParity   = 0;	/* No parity */
    }
    switch (stop) {
    	case 2:
    	    lc.bStopBits = 2;	/* Two stop bits */
    	    break;
    	case 1:
    	    lc.bStopBits = 0;	/* One stop bit */
    	    break;
    	default: 		/* No change */
    	    break;
    }
    if (DosDevIOCtl(NULL,0,&lc,sizeof(lc),
		    ASYNC_SETLINECTRL,IOCTL_ASYNC,ttyfd))
      return(-1); /* Set line */
    return(0);
}

/*  T T O P E N  --  Open a tty for exclusive access.  */

/*  Returns 0 on success, -1 on failure.  */
/*
  If called with lcl < 0, sets value of lcl as follows:
  0: the terminal named by ttname is the job's controlling terminal.
  1: the terminal named by ttname is not the job's controlling terminal.
  But watch out: if a line is already open, or if requested line can't
  be opened, then lcl remains (and is returned as) -1.
*/
ttopen(char *ttname, int *lcl, int modem, int spare)
{
    char *x; extern char* ttyname();
    char cname[DEVNAMLEN+4];
#ifdef __32BIT__
    ULONG action, res;
#else
    USHORT action, res;
#endif

    rdchbuf.length = rdchbuf.index = 0;

    if (ttyfd > -1) {			/* if device already opened */
        if (strncmp(ttname,ttnmsv,DEVNAMLEN)) /* are new & old names equal? */
          ttclos(ttyfd);		/* no, close old ttname, open new */
        else 				/* else same, ignore this call, */
	  return(0);			/* and return. */
    }

    if (*lcl == 0) return(-1);		/* Won't open in local mode */

    ttmdm = modem;			/* Make this available to other fns */
    strcpy(ttnmsv, ttname);		/* Keep copy of name locally. */

    if (modem < 0) return netopen(ttname, lcl, -modem);

/*
  This code lets you give Kermit an open file descriptor for a serial
  communication device, rather than a device name.  Kermit assumes that the
  line is already open, locked, conditioned with the right parameters, etc.
*/
    for (x = ttname; isdigit(*x); x++) ; /* Check for all digits */

    if (*x == '\0') {
	ttyfd = atoi(ttname);
	*lcl = 1;			/* Assume it's local. */
	if (ttiscom(ttyfd))
	    return savetty();
	ttyfd = -1;
	return(-4);
    }

    if (res = DosOpen(ttname,(PHFILE)&ttyfd,&action,0L,0,FILE_OPEN,
                      OPEN_ACCESS_READWRITE | OPEN_SHARE_DENYREADWRITE |
                      OPEN_FLAGS_FAIL_ON_ERROR ,0L)) {
	ttyfd = -1;
	return((res == ERROR_SHARING_VIOLATION) ? -5 : -1);
    }
    debug(F111,"ttopen ok",ttname,*lcl);

/* Caller wants us to figure out if line is controlling tty */
    if (*lcl == -1) {
	*lcl = 1;			/* Can never transfer with console */
    }
    if (!ttiscom(ttyfd)) {		/* Not a serial port */
        ttclos(0);
    	return(-4);
    }
    ttprty = dfprty;			/* Make parity the default parity */
    if (ttsettings(ttprty,0)) return(-1);
    return(ttflui());
}

/*  T T I S C O M  --  Is the given handle an open COM port? */

ttiscom(int f) {
    DCBINFO testdcb;
    /* Read DCB */
    if (DosDevIOCtl(&testdcb,sizeof(testdcb),NULL,0,
		    ASYNC_GETDCBINFO,IOCTL_ASYNC,f)) {
    	return( 0 );			/* Bad, not a serial port */
    }
    return( 1 );			/* Good */
}

/*  T T C L O S  --  Close the TTY.  */

ttclos(int spare)
{
    if (ttmdm < 0) return netclos();
    if (ttyfd == -1) return(0);		/* Wasn't open. */
    if (savedtty)
      restoretty();
    else
      DosClose(ttyfd);
    ttyfd = -1;
    return(0);
}

/*  T T G S P D  --  return speed of tt line, or of default line */

long ttgspd() {
    long sp = 0;
#ifdef __32BIT__
    struct {
      long rate;
      char fract;
    } speed;
#endif

    if (ttyfd == -1) /* if (ttopen(dftty,&sp,0,0)) */ return(-1);

#ifdef __32BIT__
    if (DosDevIOCtl(NULL,0,&speed,sizeof(speed),0x0063,IOCTL_ASYNC,ttyfd) == 0)
      return speed.rate;
    else
#endif
    if (DosDevIOCtl(&sp,sizeof(sp),NULL,0,
		    ASYNC_GETBAUDRATE,IOCTL_ASYNC,ttyfd) == 0) 
      return sp;
    else
      return -1;
}

ttsetspd(long sp) {
#ifdef __32BIT__
    struct {
      long rate;
      char fract;
    } speed;
#endif

    if (ttyfd == -1) return(-1);	/* Not open */

#ifdef __32BIT__
    if (sp > 65535) {
      speed.rate = sp;
      speed.fract = 0;
      return DosDevIOCtl(NULL,0,&speed,sizeof(speed),0x0043,IOCTL_ASYNC,ttyfd);
    } else
#endif
    return DosDevIOCtl(NULL,0,&sp,sizeof(sp),
		       ASYNC_SETBAUDRATE,IOCTL_ASYNC,ttyfd);
}



/*  T T H A N G -- Hangup phone line */

tthang() {
    if (os2setdtr(0)) return -1;
    msleep(HUPTIME);
    os2setdtr(1);
    return 1;
}


/*  T T R E S  --  Restore terminal to "normal" mode.  */

ttres() {				/* Restore the tty to normal. */
    if (ttyfd == -1) return(-1);	/* Not open */
    return(0);
}


/*  T T P K T  --  Condition the communication line for packets. */
/*		or for modem dialing */

/*  If called with speed > -1, also set the speed.  */
/*  Returns 0 on success, -1 on failure.  */

ttpkt(long speed, int flow, int parity)
{
    int s;

    if (ttmdm < 0) return(0);
    if (ttyfd < 0) return(-1);		/* Not open. */
    if (speed < 0) return(-1);

    ttprty = parity;
    os2setdtr(1);
    if (ttsetspd(speed)) return(-1);
    if (ttsettings(ttprty,0)) return(-1);
    if (os2setflow(flow)) return(-1);
    if (os2setcarr(ttcarr == CAR_ON && flow != FLO_DIAL)) return(-1);

    DosSetPrty(PRTYS_THREAD, PRTYC_FOREGROUNDSERVER, 0, 0);
    return(0);
}


/*  T T V T -- Condition communication line for use as virtual terminal  */

ttvt(long speed, int flow)
{

    if (ttmdm < 0) return(0);
    if (ttyfd < 0) return(-1);		/* Not open. */
    if (speed < 0) return(-1);

    ttprty = parity;
    os2setdtr(1);
    if (ttsetspd(speed)) return(-1);
    if (ttsettings(ttprty,0)) return(-1);
    if (os2setflow(flow)) return (-1);
    if (os2setcarr(ttcarr == CAR_ON || ttcarr == CAR_AUT)) return(-1);

    return(0);
}


/*  T T S S P D  --  Return the speed if OK, otherwise -1 */

ttsspd(int speed)
{
    long s;

    if (speed < 0) return(-1);
	switch (speed) {
	    case 11:
	    case 15:
	    case 30:
	    case 60:
	    case 120:
	    case 180:
	    case 240:
	    case 360:
	    case 480:
	    case 720:
	    case 960:
	    case 1440:
	    case 1920:
	    case 3840:
	    case 5760:
	        s = (long) speed * 10;
		ttsetspd(s);
                break;
	    default:
	    	return(-1);
		break;
	}
	return(0);
 }


/*  T T F L U I  --  Flush tty input buffer */

ttflui() {
    char parm=0;
    long int data;
    int i;

    rdchbuf.index = rdchbuf.length = 0;		/* Flush internal buffer */
    DosDevIOCtl(&data,sizeof(data),&parm,sizeof(parm),
		DEV_FLUSHINPUT,IOCTL_GENERAL,ttyfd); /* Flush driver */
    return(0);
}


/*  T T C H K  --  Tell how many characters are waiting in tty input buffer  */

ttchk() {
    USHORT data[2];
    if (ttmdm < 0) return nettchk();
    if(DosDevIOCtl(data,sizeof(data),NULL,0,
		   ASYNC_GETINQUECOUNT,IOCTL_ASYNC,ttyfd)) return(0);
    else return((rdchbuf.length-rdchbuf.index)+data[0]);
}


/*  T T X I N  --  Get n characters from tty input buffer  */

/*  Returns number of characters actually gotten, or -1 on failure  */

/*  Intended for use only when it is known that n characters are actually */
/*  available in the input buffer.  */

ttxin(int n, CHAR *buf)
{
    int i, j;

    if (ttyfd < 0) return(-1);		/* Not open. */
    i = 0;
    while (i < n) {
    	if ((j = ttinc(0)) < 0) break;
    	buf[i++] = j;
    }
    return(i);
}

/*  T T O L  --  Similar to "ttinl", but for writing.  */

ttol(CHAR *s, int n) {
    UINT i;

    if (ttmdm < 0) return nettol(s,n);

    if (ttyfd < 0) return(-1);		/* Not open. */
    if (DosWrite(ttyfd,s,n,(PVOID)&i)) return(-1);
    else return(i);
}


/*  T T O C  --  Output a character to the communication line  */

ttoc(char c) {
    UINT i;

    if (ttmdm < 0) return nettoc(c);

    if (ttyfd < 0) return(-1);		/* Not open. */
    if(DosWrite(ttyfd,&c,1,(PVOID)&i)) return(-1);
    else return(i);
}


/*  T T O C I  --  Output a character to the communication line immediately */

ttoci(char c) {

    if (ttmdm < 0) return nettoc(c);

    if (ttyfd < 0) return(-1);		/* Not open. */
    if(DosDevIOCtl(NULL,0,&c,sizeof(c),
		   ASYNC_TRANSMITIMM,IOCTL_ASYNC,ttyfd)) return(-1);
    else return(1);
}


/*  T T I N L  --  Read a record (up to break character) from comm line.  */
/*
  If no break character encountered within "max", return "max" characters,
  with disposition of any remaining characters undefined.  Otherwise, return
  the characters that were read, including the break character, in "dest" and
  the number of characters read as the value of the function, or 0 upon end of
  file, or -1 if an error occurred.  Times out & returns error if not completed
  within "timo" seconds.
*/
ttinl(CHAR *dest, int max, int timo, CHAR eol) {
    int x = 0, c, i, m;

    if (ttyfd < 0) return(-1);		/* Not open. */
    *dest = '\0';			/* Clear destination buffer */
    i = 0;				/* Next char to process */
    while (1) {
	if ((c = ttinc(timo)) == -1) {
	    x = -1;
	    break;
	}
        dest[i] = c;			/* Got one. */
	if (dest[i] == eol) {
		dest[++i] = '\0';
		return(i);
	}
	if (i++ > max) {
	    debug(F101,"ttinl buffer overflow","",i);
	    x = -1;
	    break;
	}
    }
    debug(F100,"ttinl timout","",0);	/* Get here on timeout. */
    debug(F111," with",dest,i);
    return(x);				/* and return error code. */
}


/*  T T I N C --  Read a character from the communication line  */

/* The time should be in secs for consitency with the other modules in      */
/* kermit.  To retain the option of using times of less than 1s a negative  */
/* parameter is interpreted as meaning multiples of 0.01s                  */

static rdch(void);

ttinc(timo) int timo; {
    int m, i;
    char ch = 0;

    if (ttmdm < 0) return netinc(timo);

    m = (ttprty) ? 0177 : 0377;		/* Parity stripping mask. */
    if (ttyfd < 0) return(-1);		/* Not open. */

    if (timo == 0) {			/* Untimed. */
        ttydcb.usReadTimeout = 9;       /* Test every  0.1s per call */
        if (DosDevIOCtl(NULL,0,&ttydcb,sizeof(ttydcb),
			ASYNC_SETDCBINFO,1,ttyfd))
          return(-1);
        do
          i = rdch();
        while (i < 0);   /* Wait for a character. */
        return(i & m);
    }

    if (timo < 0)
        timo= -timo - 1;
    else
        timo = timo * 100 - 1;

    if (timo != ttydcb.usReadTimeout + 1) { /* Set timeout value */
       ttydcb.usReadTimeout = timo;
       if (DosDevIOCtl(NULL,0,&ttydcb,sizeof(ttydcb),
		       ASYNC_SETDCBINFO,IOCTL_ASYNC,ttyfd))
          return(-1);
    }

    i = rdch();

    if (i < 0) return(-1);
    else return(i & m);
}

/*  RDCH -- Read characters from the serial port, maintaining an internal
            buffer of characters for the sake of efficiency. */
static rdch() {

    if (rdchbuf.index == rdchbuf.length) {
	rdchbuf.index = 0;
        if (DosRead(ttyfd,rdchbuf.buffer,sizeof(rdchbuf.buffer),
#ifdef __32BIT__
                    (PULONG) &rdchbuf.length)) {
#else
                    (PUSHORT) &rdchbuf.length)) {
#endif
	    rdchbuf.length = 0;
	    return(-1);
        }
    }

    return( (rdchbuf.index < rdchbuf.length)
            ? rdchbuf.buffer[rdchbuf.index++] : -1 );
}

/*  T T S C A R R  --  Set ttcarr variable, controlling carrier handling.
 *
 *  0 = Off: Always ignore carrier. E.g. you can connect without carrier.
 *  1 = On: Heed carrier, except during dialing. Carrier loss gives disconnect.
 *  2 = Auto: For "modem direct": The same as "Off".
 *            For real modem types: Heed carrier during connect, but ignore
 *                it anytime else.  Compatible with pre-5A C-Kermit versions.
 */

int
ttscarr(carrier) int carrier; {
    ttcarr = carrier;
    debug(F101, "ttscarr","",ttcarr);
    return(ttcarr);
}

/*  T T G M D M  --  Get modem signals  */
/*
 Looks for the modem signals CTS, DSR, and CTS, and returns those that are
 on in as its return value, in a bit mask as described for ttwmdm.  Returns:
 -3 Not implemented
 -2 if the line does not have modem control
 -1 on error.
 >= 0 on success, with a bit mask containing the modem signals that are on.
*/

int
ttgmdm() {
    BYTE instat, outstat;
    int modem = 0;

    if(DosDevIOCtl(&instat,sizeof(instat),NULL,0,
		   ASYNC_GETMODEMINPUT,IOCTL_ASYNC,ttyfd))
       return(-1);
    if(DosDevIOCtl(&outstat,sizeof(outstat),NULL,0,
		   ASYNC_GETMODEMOUTPUT,IOCTL_ASYNC,ttyfd))
       return(-1);

    /* Clear To Send */
    if (instat & CTS_ON) modem |= BM_CTS;
    /* Data Set Ready */
    if (instat & DSR_ON) modem |= BM_DSR;
    /* Carrier */
    if (instat & DCD_ON) modem |= BM_DCD;
    /* Ring Indicate */
    if (instat & RI_ON)  modem |= BM_RNG;

    /* Data Terminal Ready */
    if (outstat & DTR_ON) modem |= BM_DTR;
    /* Request To Send */
    if (outstat & RTS_ON) modem |= BM_RTS;

    return(modem);
}

/*  T T S N D B  --  Send a BREAK signal  */

ttsndb() {
    MODEMSTATUS ms;
    UINT data, i;

    if (ttmdm < 0) return netbreak();

    ms.fbModemOn = RTS_ON;
    ms.fbModemOff = 255;
    DosDevIOCtl(&data,sizeof(data),&ms,sizeof(ms),
		ASYNC_SETMODEMCTRL,IOCTL_ASYNC,ttyfd);

    DosDevIOCtl(&i,sizeof(i),NULL,0,
		ASYNC_SETBREAKON,IOCTL_ASYNC,ttyfd);	/* Break on */
    DosSleep(275L);					/* ZZZzzz */
    DosDevIOCtl(&i,sizeof(i),NULL,0,
		ASYNC_SETBREAKOFF,IOCTL_ASYNC,ttyfd);	/* Break off */
}

/*  T T S N D L B  --  Send a LONG BREAK signal  */

ttsndlb() {
    MODEMSTATUS ms;
    UINT data, i;

    if (ttmdm < 0) return netbreak();

    ms.fbModemOn = RTS_ON;
    ms.fbModemOff = 255;
    DosDevIOCtl(&data,sizeof(data),&ms,sizeof(ms),
		ASYNC_SETMODEMCTRL,IOCTL_ASYNC,ttyfd);

    DosDevIOCtl(&i,sizeof(i),NULL,0,
		ASYNC_SETBREAKON,IOCTL_ASYNC,ttyfd);	/* Break on */
    DosSleep(1800L);					/* ZZZzzz */
    DosDevIOCtl(&i,sizeof(i),NULL,0,
		ASYNC_SETBREAKOFF,IOCTL_ASYNC,ttyfd);	/* Break off */
}


#ifndef __EMX__
/*  S L E E P  --  Emulate the Unix sleep function  */

unsigned sleep(t) unsigned t; {
    DosSleep((long)t*1000);
}
#endif


/*  M S L E E P  --  Millisecond version of sleep().  */

/* Intended only for small intervals.  For big ones, just use sleep(). */

msleep(m) int m; {
    DosSleep((long)m);
}


/*  R T I M E R --  Reset elapsed time counter  */

void rtimer() {
    tcount = time((long *)NULL);
}


/*  G T I M E R --  Get current value of elapsed time counter in seconds  */

int gtimer(void) {
    int x;
    x = (int) (time( (long *) 0 ) - tcount);
    return( (x < 0) ? 0 : x );
}


/*  Z T I M E  --  Return date/time string  */

void ztime(char **s) {
    long clock_storage;

    clock_storage = time( (long *) 0 );
    *s = ctime( &clock_storage );
}

/*  C O N O C  --  Output a character to the console terminal  */

int conoc(char c) {
    write(1,&c,1);
}


/*  C O N X O  --  Write x characters to the console terminal  */

int conxo(int x, char *s) {
    write(1,s,x);
}


/*  C O N O L  --  Write a line to the console terminal  */

int conol(char *s) {
    int len;
    len = strlen(s);
    write(1,s,len);
}


/*  C O N O L A  --  Write an array of lines to the console terminal */

conola(s) char *s[]; {
    int i;
    for (i=0 ; *s[i] ; i++) conol(s[i]);
}


/*  C O N O L L  --  Output a string followed by CRLF  */

conoll(s) char *s; {
    conol(s);
    write(1,"\r\n",2);
}


/*  C O N C H K  --  Return how many characters available at console  */

conchk() {
    KBDKEYINFO k;

    KbdPeek(&k,0);
    return( (k.fbStatus & 0x40) ? 1 : 0 );
}


/*  C O N I N C  --  Get a character from the console  */

coninc(timo) int timo; {
    int c;

    while ( (c = congks(timo)) >= 0x100 );

    return c;
}

static jmp_buf kbbuf;
SIGTYP (*saval)(int);

SIGTYP kbdtimo(int sig)
{
#ifdef __EMX__
    signal(SIGALRM, SIG_ACK);
#endif
    signal(SIGALRM, saval);
    longjmp(kbbuf, 1);
}

congks(timo) int timo; {
    /* timeout isn't handled */
    KBDKEYINFO k;
    int c;

    if (!islocal) {
      c = 0;
      if ( read(ttyfd, &c, 1) < 1 )
	return -1;
      return c;
    };

    for (;;)
    {
        if (timo <= 0)
	  KbdCharIn(&k, IO_WAIT, 0);
	else 
	{
	  saval = signal(SIGALRM, kbdtimo);
	  alarm(timo);
	  
	  if (setjmp(kbbuf))
	    return -1;
	  else
	    KbdCharIn(&k, IO_WAIT, 0);

	  alarm(0);
	  signal(SIGALRM, saval);
	}

        if ( k.chChar || k.chScan )
        {
            c = k.chChar;

            if (c == 0x00) c = 0x100 | k.chScan;
            if (c == 0xE0) c = 0x200 | k.chScan;

	    switch (c) /* handle ambiguous keypad and space keys */
	    {
	    case ' ':
#ifdef KBDSTF_CONTROL
	      return (k.fsState & KBDSTF_CONTROL) ? 0x200 | c : c;
#else
	      return (k.fsState & CONTROL) ? 0x200 | c : c;
#endif
	    case '+':
	      return k.chScan == 0x4E ? 0x200 | c : c;
	    case '-':
	      return k.chScan == 0x4A ? 0x200 | c : c;
	    case '*':
	      return k.chScan == 0x37 ? 0x200 | c : c;
	    case '/':
	      return k.chScan == 0xE0 ? 0x200 | c : c;
	    case '\r':
	    case '\n':
	      return k.chScan == 0xE0 ? 0x200 | c : c;
	    case '\b':
	      return k.chScan == 0x0E ? DEL : c;
	    case DEL:
	      return 0x200 | c;
	    default:
	      return c;
	    }
        }

#ifdef KBDTRF_SHIFT_KEY_IN
        if ( (k.fbStatus & KBDTRF_SHIFT_KEY_IN) &&
#else
        if ( (k.fbStatus & SHIFT_KEY_IN) &&
#endif
#ifdef KBDSTF_SCROLLLOCK
             (k.fsState & KBDSTF_SCROLLLOCK) )
            if ( k.fsState & KBDSTF_SCROLLLOCK_ON )
#else
             (k.fsState & SCROLLLOCK) )
            if ( k.fsState & SCROLLLOCK_ON )
#endif
                return 0x2FF;
            else
                return 0x1FF;
    }
}


conraw() {
    KBDINFO k;

    if (!islocal) return(0);
    conmode = 1;
    k.cb = sizeof(k);
    KbdGetStatus(&k,0);
    k.fsMask = KEYBOARD_ECHO_OFF
             | KEYBOARD_BINARY_MODE
             | KEYBOARD_SHIFT_REPORT;
    return(KbdSetStatus(&k,0));
}

concooked() {
    KBDINFO k;

    if (!islocal) return(0);
    conmode = 0;
    k.cb = sizeof(k);
    KbdGetStatus(&k,0);
    k.fsMask = KEYBOARD_ECHO_ON
             | KEYBOARD_ASCII_MODE;
    return(KbdSetStatus(&k,0));
}


/* init key map */

void keymapinit() {
    /* predefine NUL characters */
    keymap[0x103] = 0x00;
    keymap[0x200 | ' ' ] = 0x00;
    /* predefine ambiguous keypad keys */
    keymap[0x200 | '+' ] = '+';
    keymap[0x200 | '-' ] = '-';
    keymap[0x200 | '*' ] = '*';
    keymap[0x200 | '/' ] = '/';
    keymap[0x200 | '\r'] = '\r';
    keymap[0x200 | '\n'] = '\n';
    keymap[0x200 | DEL ] = DEL;
}

/*  C O N B I N  --  Put console in binary mode  */

/*  Returns 0 if ok, -1 if not  */

conbin(char esc) {
    if (!islocal) return(0);          /* only for real ttys */
    conraw();
}

/*  C O N C B  -- Put console into single char mode with no echo. */

concb(char esc) {
    if (!islocal) return(0);          /* only for real ttys */
    concooked();
}


/*  C O N G M  -- Get console terminal mode. */

congm() {}

/*  C O N R E S -- Restore console to congm mode. */

conres() {}


/*  C O N I N T -- Enable console terminal interrupts. */

void conint(f, s) SIGTYP (*f)(int), (*s)(int); {
    signal(SIGINT, f);
    signal(SIGBREAK, f);
}


/*  C O N N O I -- No console interrupts. */

void connoi() {
    signal(SIGINT, cc_trap);
    signal(SIGBREAK, cc_trap);
}


/* privilege dummy */

int priv_chk() {return 0;}


#ifndef __EMX__

/* alarm() implementation for all others, emx/gcc already has it built-in */

#ifdef __32BIT__

#define STACK 8192
static BOOL alrm, running;
static UINT delay;

static long FAR alarm_thread(VOID *args)
{
  for (;;)
  {
    DosSleep(1000L);
    DosEnterCritSec();

    if ( alrm )
      if ( --delay == 0 ) {
	alrm = FALSE;
	DosKillProcess(DKP_PROCESS, pid);
      }

    DosExitCritSec();
  }

  running = FALSE;
  DosExit(EXIT_THREAD, 0);
}

static void alarm_signal(int sig)
{
  signal(SIGTERM, SIG_DFL); /* avoid making us kill-proof all the time */
  /* DosBeep(440, 10); */
  raise(SIGALRM);
}

unsigned alarm(unsigned sec)
{
  TID tid;
  unsigned old;

  DosEnterCritSec();

  old = delay;
  delay = sec;

  if ( alrm = (delay > 0) )
    signal(SIGTERM, alarm_signal);

  DosExitCritSec();

  if ( !running )
  {
    running = TRUE;
    DosCreateThread(&tid, (PFNTHREAD)alarm_thread, 0, 0, STACK);
    DosSetPrty(PRTYS_THREAD, PRTYC_REGULAR, 0, tid);
  }

  return old;
}

#else

#define STACK 2048
static PBYTE pstack;
static BOOL alrm, running;
static USHORT delay;

#pragma check_stack(off)

static VOID FAR alarm_thread(VOID)
{
  for (;;)
  {
    DosSleep(1000L);
    DosEnterCritSec();

    if ( alrm )
      if ( --delay == 0 ) {
	alrm = FALSE;
	DosFlagProcess(pid, FLGP_PID, PFLG_A, 1);
      }

    DosExitCritSec();
  }

  running = FALSE;
  DosExit(EXIT_THREAD, 0);
}

#pragma check_stack()

static VOID PASCAL FAR alarm_signal(USHORT sigarg, USHORT signum)
{
  PFNSIGHANDLER prev;
  USHORT action;
  DosSetSigHandler(alarm_signal, &prev, &action, SIGA_ACKNOWLEDGE, SIG_PFLG_A);
  raise(SIGALRM);
}

unsigned alarm(unsigned sec)
{
  PFNSIGHANDLER prev;
  USHORT action;
  TID tid;
  unsigned old;

  if ( pstack == NULL )
  {
    pstack = malloc(STACK);
    assert(pstack != NULL);
    DosSetSigHandler(alarm_signal, &prev, &action, SIGA_ACCEPT, SIG_PFLG_A);
  }

  DosEnterCritSec();

  old = delay;
  delay = sec;
  alrm = (delay > 0);

  DosExitCritSec();

  if ( !running )
  {
    running = TRUE;
    DosCreateThread(alarm_thread, &tid, pstack + STACK);
    DosSetPrty(PRTYS_THREAD, PRTYC_REGULAR, 0, tid);
  }

  return old;
}

#endif


/*
 *  A public domain implementation of BSD directory routines for
 *  MS-DOS.  Written by Michael Rendell ({uunet,utai}michael@garfield),
 *  August 1897
 *  Ported to OS/2 by Kai Uwe Rommel
 *  December 1989, February 1990
 *  Change for HPFS support, October 1990
 */

int attributes = A_DIR | A_HIDDEN;

static char *getdirent(char *);
static void free_dircontents(struct _dircontents *);

static HDIR hdir;
#ifdef __32BIT__
static ULONG count;
static FILEFINDBUF3 find;
#define DosFindFirst(p1, p2, p3, p4, p5, p6) \
        DosFindFirst(p1, p2, p3, p4, p5, p6, 1)
#define DosQCurDisk     DosQueryCurrentDisk
#define DosQFSAttach    DosQueryFSAttach
#define DosQFSInfo      DosQueryFSInfo
#else
static USHORT count;
static FILEFINDBUF find;
#define DosFindFirst(p1, p2, p3, p4, p5, p6) \
        DosFindFirst(p1, p2, p3, p4, p5, p6, 0)
#define DosQFSAttach(p1, p2, p3, p4, p5) \
        DosQFSAttach(p1, p2, p3, p4, p5, 0)
#endif

int IsFileSystemFAT(char *dir)
{
  static USHORT nLastDrive = -1, nResult;
  ULONG lMap;
  BYTE bData[64], bName[3];
#ifdef __32BIT__
  ULONG nDrive, cbData;
  FSQBUFFER2 *pData = (FSQBUFFER2 *) bData;
#else
  USHORT nDrive, cbData;
  FSQBUFFER *pData = (FSQBUFFER *) bData;
#endif

  /* We separate FAT and HPFS file systems here. */

  if ( isalpha(dir[0]) && (dir[1] == ':') )
    nDrive = toupper(dir[0]) - '@';
  else
    DosQCurDisk(&nDrive, &lMap);

  if ( nDrive == nLastDrive )
    return nResult;

  bName[0] = (char) (nDrive + '@');
  bName[1] = ':';
  bName[2] = 0;

  nLastDrive = nDrive;
  cbData = sizeof(bData);

  if ( !DosQFSAttach(bName, 0, FSAIL_QUERYNAME, (PVOID) pData, &cbData) )
    nResult = !strcmp(pData -> szFSDName + pData -> cbName, "FAT");
  else
    nResult = FALSE;

  return nResult;
}

DIR *opendir(char *name)
{
  struct stat statb;
  DIR *dirp;
  char c;
  char *s;
  struct _dircontents *dp;
  char nbuf[MAXPATHLEN + 1];
  int len;

  strcpy(nbuf, name);
  len = strlen (nbuf);
  s = nbuf + len;

  if ( ((c = nbuf[strlen(nbuf) - 1]) == '\\' || c == '/') &&
       (strlen(nbuf) > 1) )
  {
    nbuf[strlen(nbuf) - 1] = 0;

    if ( nbuf[strlen(nbuf) - 1] == ':' )
      strcat(nbuf, "\\.");
  }
  else
    if ( nbuf[strlen(nbuf) - 1] == ':' )
      strcat(nbuf, ".");

  if (stat(nbuf, &statb) < 0 || (statb.st_mode & S_IFMT) != S_IFDIR)
    return NULL;

  if ( (dirp = malloc(sizeof(DIR))) == NULL )
    return NULL;

  if ( nbuf[strlen(nbuf) - 1] == '.' )
    strcpy(nbuf + strlen(nbuf) - 1, "*.*");
  else
    if ( ((c = nbuf[strlen(nbuf) - 1]) == '\\' || c == '/') &&
         (strlen(nbuf) == 1) )
      strcat(nbuf, "*.*");
    else
      strcat(nbuf, "\\*.*");

  dirp -> dd_loc = 0;
  dirp -> dd_contents = dirp -> dd_cp = NULL;

  if ((s = getdirent(nbuf)) == NULL)
    return dirp;

  do
  {
    if (((dp = malloc(sizeof(struct _dircontents))) == NULL) ||
        ((dp -> _d_entry = malloc(strlen(s) + 1)) == NULL)      )
    {
      if (dp)
        free(dp);
      free_dircontents(dirp -> dd_contents);

      return NULL;
    }

    if (dirp -> dd_contents)
    {
      dirp -> dd_cp -> _d_next = dp;
      dirp -> dd_cp = dirp -> dd_cp -> _d_next;
    }
    else
      dirp -> dd_contents = dirp -> dd_cp = dp;

    strcpy(dp -> _d_entry, s);
    dp -> _d_next = NULL;

    dp -> _d_size = find.cbFile;
    dp -> _d_mode = find.attrFile;
    dp -> _d_time = *(unsigned *) &(find.ftimeLastWrite);
    dp -> _d_date = *(unsigned *) &(find.fdateLastWrite);
  }
  while ((s = getdirent(NULL)) != NULL);

  dirp -> dd_cp = dirp -> dd_contents;

  return dirp;
}


void closedir(DIR * dirp)
{
  free_dircontents(dirp -> dd_contents);
  free(dirp);
}


struct dirent *readdir(DIR * dirp)
{
  static struct dirent dp;

  if (dirp -> dd_cp == NULL)
    return NULL;

  dp.d_namlen = dp.d_reclen =
    strlen(strcpy(dp.d_name, dirp -> dd_cp -> _d_entry));

  dp.d_ino = 1;

  dp.d_size = dirp -> dd_cp -> _d_size;
  dp.d_mode = dirp -> dd_cp -> _d_mode;
  dp.d_time = dirp -> dd_cp -> _d_time;
  dp.d_date = dirp -> dd_cp -> _d_date;

  dirp -> dd_cp = dirp -> dd_cp -> _d_next;
  dirp -> dd_loc++;

  return &dp;
}


void seekdir(DIR * dirp, long off)
{
  long i = off;
  struct _dircontents *dp;

  if (off >= 0)
  {
    for (dp = dirp -> dd_contents; --i >= 0 && dp; dp = dp -> _d_next);

    dirp -> dd_loc = off - (i + 1);
    dirp -> dd_cp = dp;
  }
}


long telldir(DIR * dirp)
{
  return dirp -> dd_loc;
}


static void free_dircontents(struct _dircontents * dp)
{
  struct _dircontents *odp;

  while (dp)
  {
    if (dp -> _d_entry)
      free(dp -> _d_entry);

    dp = (odp = dp) -> _d_next;
    free(odp);
  }
}


char *getdirent(char *dir)
{
  int done;
  static int lower = TRUE;

  if (dir != NULL)
  {				       /* get first entry */
    lower = IsFileSystemFAT(dir);

    hdir = HDIR_CREATE;
    count = 1;
    done = DosFindFirst(dir, &hdir, attributes, &find, sizeof(find), &count);
  }
  else				       /* get next entry */
    done = DosFindNext(hdir, &find, sizeof(find), &count);

  if (done == 0)
  {
    if ( lower )
      strlwr(find.achName);
    return find.achName;
  }
  else
  {
    DosFindClose(hdir);
    return NULL;
  }
}

#endif /* __EMX__ */

#ifdef __IBMC__

/* quick hack because IBM C lacks popen() and pclose() */

int pids[64];

FILE *
popen(char *cmd, char *mode) {
  HFILE end1, end2, std, old1, old2, temp;
  FILE *file;
  char fail[256], cmd_line[256], *cmd_exe, *args;
  RESULTCODES res;
  int rc;

  if (DosCreatePipe(&end1, &end2, 4096))
    return NULL;

  std = (*mode == 'w') ? 0 /* stdin */ : 1 /* stdout */;
  if (std == 0) {
    temp = end1; end1 = end2; end2 = temp;
  }

  old1 = -1; /* save stdin or stdout */
  DosDupHandle(std, &old1);
  DosSetFHState(old1, OPEN_FLAGS_NOINHERIT);
  temp = std; /* redirect stdin or stdout */
  DosDupHandle(end2, &temp);

  if ( std == 1 ) {
    old2 = -1; /* save stderr */
    DosDupHandle(2, &old2);
    DosSetFHState(old2, OPEN_FLAGS_NOINHERIT);
    temp = 2;   /* redirect stderr */
    DosDupHandle(end2, &temp);
  }

  DosClose(end2);
  DosSetFHState(end1, OPEN_FLAGS_NOINHERIT);

  if ( (cmd_exe = getenv("COMSPEC")) == NULL )
    cmd_exe = "cmd.exe";

  strcpy(cmd_line, cmd_exe);
  args = cmd_line + strlen(cmd_line) + 1; /* skip zero */
  strcpy(args, "/c ");
  strcat(args, cmd);
  args[strlen(args) + 1] = '\0'; /* two zeroes */
  rc = DosExecPgm(fail, sizeof(fail), EXEC_ASYNCRESULT, 
		  cmd_line, 0, &res, cmd_exe);

  temp = std; /* restore stdin or stdout */
  DosDupHandle(old1, &temp);
  DosClose(old1);

  if ( std == 1 ) {
    temp = 2;   /* restore stderr */
    DosDupHandle(old2, &temp);
    DosClose(old2);
  }

  if (rc) {
    DosClose(end1);
    return NULL;
  }
  
  file = fdopen(end1, mode);
  pids[end1] = res.codeTerminate;
  return file;
}

int
pclose(FILE *pipe) {
  RESULTCODES rc;
  PID pid;
  int handle = fileno(pipe);
  fclose(pipe);
  if (pids[handle])
    DosWaitChild(DCWA_PROCESSTREE, DCWW_WAIT, &rc, &pid, pids[handle]);
  pids[handle] = 0;
  return rc.codeTerminate == 0 ? rc.codeResult : -1;
}
#endif

void
ChangeNameForFAT(char *name) {
  char *src, *dst, *next, *ptr, *dot, *start;
  static char invalid[] = ":;,=+\"[]<>| \t";

  if ( isalpha(name[0]) && (name[1] == ':') )
    start = name + 2;
  else
    start = name;

  src = dst = start;
  if ( (*src == '/') || (*src == '\\') )
    src++, dst++;

  while ( *src )
  {
    for ( next = src; *next && (*next != '/') && (*next != '\\'); next++ );

    for ( ptr = src, dot = NULL; ptr < next; ptr++ )
      if ( *ptr == '.' )
      {
        dot = ptr; /* remember last dot */
        *ptr = '_';
      }

    if ( dot == NULL )
      for ( ptr = src; ptr < next; ptr++ )
        if ( *ptr == '_' )
          dot = ptr; /* remember last _ as if it were a dot */

    if ( dot && (dot > src) &&
         ((next - dot <= 4) ||
          ((next - src > 8) && (dot - src > 3))) )
    {
      if ( dot )
        *dot = '.';

      for ( ptr = src; (ptr < dot) && ((ptr - src) < 8); ptr++ )
        *dst++ = *ptr;

      for ( ptr = dot; (ptr < next) && ((ptr - dot) < 4); ptr++ )
        *dst++ = *ptr;
    }
    else
    {
      if ( dot && (next - src == 1) )
        *dot = '.';           /* special case: "." as a path component */

      for ( ptr = src; (ptr < next) && ((ptr - src) < 8); ptr++ )
        *dst++ = *ptr;
    }

    *dst++ = *next; /* either '/' or 0 */

    if ( *next )
    {
      src = next + 1;

      if ( *src == 0 ) /* handle trailing '/' on dirs ! */
        *dst = 0;
    }
    else
      break;
  }

  for ( src = start; *src != 0; ++src )
    if ( strchr(invalid, *src) != NULL )
        *src = '_';
}


int IsFileNameValid(char *name)
{
  HFILE hf;
#ifdef __32BIT__
  ULONG uAction;
#else
  USHORT uAction;
#endif

  switch( DosOpen(name, &hf, &uAction, 0, 0, FILE_OPEN,
                  OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE, 0) )
  {
  case ERROR_INVALID_NAME:
  case ERROR_FILENAME_EXCED_RANGE:
    return FALSE;
  case NO_ERROR:
    DosClose(hf);
  default:
    return TRUE;
  }
}

long
zdskspace(int drive) {
  FSALLOCATE fsa;
  if ( DosQFSInfo(drive, 1, (PBYTE) &fsa, sizeof(fsa)) )
    return 0;
  return fsa.cUnitAvail * fsa.cSectorUnit * fsa.cbSector;
}

char *
GetLoadPath(void) {
#ifdef __32BIT__
  PTIB pptib;
  PPIB pppib;
  char *szPath;

  DosGetInfoBlocks(&pptib, &pppib);

  szPath = pppib -> pib_pchenv;

  while (*szPath)
    szPath = strchr(szPath, 0) + 1;

  return szPath + 1;
#else
  extern char *_pgmptr;
  return _pgmptr;
#endif
}
