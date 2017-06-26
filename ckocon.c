char *connv = "OS/2 CONNECT command, 25 Oct 92";

/* C K O C O N  --  Kermit connect command for OS/2 systems */

/*
 * Author: Chris Adie (C.Adie@uk.ac.edinburgh) Copyright (C) 1988 Edinburgh
 * University Computing Service Permission is granted to any individual or
 * institution to use, copy, or redistribute this software so long as it is
 * not sold for profit, provided this copyright notice is retained.
 *
 * Incorporates a VT102 emulator, together with its screen access routines.
 * If the code looks a bit funny sometimes, it's because it was machine
 * translated to 'C'.
 *
 * Many changes for 5A by Kai Uwe Rommel (rommel@informatik.tu-muenchen.de)
 *
 * And more by Frank da Cruz (fdc@columbia.edu), mostly bug fixes (tabs,
 * cursor/attributes save/restore, keypad modes, etc), hooking into new
 * SET TERMINAL commands, and cosmetic.
 */

/*
 *
 * =============================#includes=====================================
 */

#include "ckcdeb.h"		/* Typedefs, debug formats, etc */
#include "ckcker.h"		/* Kermit definitions */
#include "ckcasc.h"             /* ASCII character symbols */
#include "ckcxla.h"		/* Character set translation */
#include "ckcnet.h"             /* Network support */
#include "ckuusr.h"		/* For terminal type definitions, etc. */

#include <ctype.h>		/* Character types */
#include <io.h>			/* File io function declarations */
#include <process.h>		/* Process-control function declarations */
#include <stdlib.h>		/* Standard library declarations */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifndef __32BIT__
#define far _far
#define near _near
#define pascal _pascal
#endif
#define	INCL_NOPM
#define	INCL_VIO
#define	INCL_DOSPROCESS
#define	INCL_DOSSEMAPHORES
#include <os2.h>
#undef COMMENT

#ifndef min
#define min(a,b) ((a) <= (b) ? (a) : (b))
#endif

/*
 *
 * =============================#defines======================================
 */

#ifdef TRUE
#undef TRUE
#endif /* TRUE */
#define TRUE 1

#ifdef FALSE
#undef FALSE
#endif /* FALSE */
#define FALSE 0

#define	UPWARD		6
#define	DOWNWARD	7
#define	LBUFSIZE	240	/* No of lines in extended buffer (was 144) */

#define DEFTABS		\
"0T0000000T0000000T0000000T0000000T0000000T0000000T0000000T0000000T0000000\
T0000000T0000000T0000000T0000000T0000000T0000000T0000000T000";

/*
 *
 * =============================typedefs======================================
 */

typedef char    fulstring[256];
typedef int     bool;

typedef struct ascreen_rec {	/* Structure for saving screen info */
    unsigned char   ox;
    unsigned char   oy;
    unsigned char   att;
    char            *scrncpy;
} ascreen;

/*
 *
 * =============================externals=====================================
 */

extern CHAR (*xls[MAXTCSETS+1][MAXFCSETS+1])(CHAR);  /* Character set xlate */
extern CHAR (*xlr[MAXTCSETS+1][MAXFCSETS+1])(CHAR);  /* functions. */
extern int language;		/* Current language. */
extern struct langinfo langs[];	/* Language info. */
extern struct csinfo fcsinfo[]; /* File character set info */
extern int tcsr, tcsl;		/* Terminal character sets, remote & local. */

extern int tnlm;		/* Terminal newline mode */
extern int tt_crd;		/* Carriage-return display mode */
extern long	speed;
extern int      local, escape, duplex, parity, flow, seslog, sessft,
                cmdmsk, cmask, sosi, xitsta, debses, mdmtyp, carrier;

extern int      network, nettype, ttnproto;

extern KEY 	*keymap;
extern MACRO 	*macrotab;
extern char     ttname[], sesfil[];
extern void	cc_trap();

/*
 *
 * =============================variables==============================
 */

#ifdef MONO
int     colornormal     = 0x07;
int     colorunderline  = 0x01;
int     colorreverse    = 0x70;
int     colorstatus     = 0x70;
int     colorhelp       = 0x70;
#else
int     colornormal     = 0x30;
int     colorunderline  = 0x3E;
int     colorreverse    = 0x70;
int     colorstatus     = 0x74;
int     colorhelp       = 0x27;
#endif
int	colorcmd;
int     scrninitialised;

static long	twochartimes;
static char	termessage[132];
static FILE    *lst = NULL;
static bool     lstclosed = TRUE;
static int      xsize = -1, ysize = -1;
static ascreen  vt100screen, commandscreen, savedscreen;
static unsigned char
  attribute = NUL,
  savedattribute = NUL,
  defaultattribute = NUL;

static struct {
    unsigned        reversed:1;
    unsigned        blinking:1;
    unsigned        underlined:1;
    unsigned        bold:1;
    unsigned        invisible:1;
}               attrib, savedattrib;

static struct paging_record {
    unsigned char   numlines;	/* no. of lines in extended display buffer */
    unsigned char   topline;
    unsigned char   botline;
    char           *buffer;
}               paginginfo;

static int wherex;			/* X-coordinate, 1-based */
static int wherey;			/* Y-coordinate, 1-based */
static int margintop = 1;		/* Top of scrolling region */
static int marginbot = 24;		/* Bottom of scrolling region */

static int      active, quitnow, hangnow, inshift, outshift, tcs, langsv;

static char	answerback[81] = "OS/2 C-Kermit\n";
static char     usertext[(132) + 1];
static char     exittext[(14) + 1];
static char     helptext[(14) + 1];
static char     filetext[(14) + 1];
static char     hostname[(20) + 1];

static unsigned char graphicset[32] = {
    0x20, 0x04, 0xB0, 0x1A, 0x17, 0x1B, 0x19, 0xF8,
    0xF1, 0x15, 0x12, 0xD9, 0xBF, 0xDA, 0xC0, 0xC5,
    0xC4, 0xC4, 0xC4, 0xC4, 0xC4, 0xC3, 0xB4, 0xC1,
    0xC2, 0xB3, 0xF3, 0xF2, 0xE3, 0x9D, 0x9C, 0xFA
};

static char     htab[133] = DEFTABS	/* Default tab settings */

static VIOCURSORINFO crsr_command;
static VIOCURSORINFO crsr_info;

static int      row = 0;
static int      column = 0;
static int      achar;

static unsigned char g0 = 'B';
static unsigned char g1 = 'B';
static unsigned char *g0g1 = &g0;

static bool     printon        = FALSE;	/* Printer is on */
static bool     turnonprinter  = FALSE;	/* Time to turn on printer */
static bool     turnoffprinter = FALSE;	/* Time to turn it off */

static bool     wrapit;
static bool     literal   = FALSE;
static bool     screenon  = TRUE;
static bool     cursoron  = TRUE;	/* For speed, turn off when busy */
static bool     relcursor = FALSE;
static bool     keylock   = FALSE;
static bool     vt52graphics = FALSE;

static unsigned char saveg0, saveg1, *saveg0g1;
static bool     saverelcursor, saved=FALSE;

static bool     dwl[60];
static bool     dwls = FALSE;	/* For optimisation */
static bool     deccolm = FALSE;
static bool     decscnm = FALSE;
static bool     linemode = FALSE;
static bool     insertmode = FALSE;
/*
  Terminal parameters that can also be set externally by SET commands.
  Formerly they were declared and initialized here, and had different
  names, as shown in the comments.  Now they are declared and
  initialized in ckuus7.c.  - fdc
*/
extern int tt_arrow;			/* cursorkey  = TRUE; */
extern int tt_keypad;			/* keypadnum  = FALSE; */
extern int tt_wrap;			/* autowrap   = TRUE; */
extern int tt_type;			/* ansi       = TRUE; */
extern int tt_cursor;			/* cursormode = 0; */

/* Escape-sequence processing buffer */

static bool     escaping = FALSE;
static int      escnext = 1;
static int      esclast = 0;
static unsigned char escbuffer[129];

static unsigned char sgrcols[8] = {0, 4, 2, 6, 1, 5, 3, 7};

/* Function prototypes */

static CHAR (*sxo)(CHAR); /* Local translation functions */
static CHAR (*rxo)(CHAR); /* for output (sending) terminal chars */
static CHAR (*sxi)(CHAR); /* and for input (receiving) terminal chars. */
static CHAR (*rxi)(CHAR);

int ttoci(char c);
int line25(void);
int lgotoxy(int x, int y);
int scroll(int updown, int top, int bottom);
int clearscreen(int all, int attr);
int setmargins(int top, int bot);
int sendstr(char *s);
int wrtch(char ch);
int bleep(void);
int movetoscreen(char *source, int x, int y, int len);
int savescreen(ascreen *scrn);
int restorescreen(ascreen *scrn);
int clrtoeoln(void);
int killcursor(void);
int newcursor(void);
int cwrite(unsigned char ch);
int toplinetocyclicbuffer(void);
int setcursormode(void);
int restorecursormode(void);

extern int concooked(void);
extern int conraw(void);
extern int xxesc(char **);

/* thread stuff */
#ifdef __32BIT__
HEV threadsem;
ULONG semcount;
#define THRDSTKSIZ	8192
#else
static long int threadsem;	/* Semaphore to show thread is running */
#define THRDSTKSIZ	2048
static char            stack[THRDSTKSIZ];	/* Stack for second thread */
#endif
static TID             threadid;

/* skip escape sequences */

/* for comments, see ckucon.c, where this code is copied from */

#define ES_NORMAL 0			/* Normal, not in escape sequence */
#define ES_GOTESC 1			/* Current character is ESC */
#define ES_ESCSEQ 2			/* Inside an escape sequence */
#define ES_GOTCSI 3			/* Inside a control sequence */
#define ES_STRING 4			/* Inside DCS,OSC,PM, or APC string */
#define ES_TERMIN 5			/* 1st char of string terminator */

static int
  iskipesc = 0,				/* Skip over ANSI escape sequences */
  oskipesc = 0,
  inesc = ES_NORMAL,			/* State of sequence recognizer */
  outesc = ES_NORMAL;

int
chkaes(int esc, char c) {
    if (c == CAN || c == SUB)		/* CAN and SUB cancel any sequence */
      esc = ES_NORMAL;
    else				/* Otherwise */
      switch (esc) {			/* enter state switcher */

	case ES_NORMAL:			/* NORMAL state */
	  if (c == ESC)			/* Got an ESC */
	    esc = ES_GOTESC;		/* Change state to GOTESC */
	  break;			/* Otherwise stay in NORMAL state */

	case ES_GOTESC:			/* GOTESC state */
	  if (c == '[')			/* Left bracket after ESC is CSI */
	    esc = ES_GOTCSI;		/* Change to GOTCSI state */
	  else if (c > 057 && c < 0177)	/* Final character '0' thru '~' */
	    esc = ES_NORMAL;		/* Back to normal */
	  else if (c == 'P' || (c > 0134 && c < 0140)) /* P, [, ^, or _ */
	    esc = ES_STRING;		/* Switch to STRING-absorption state */
	  else if (c != ESC)		/* ESC in an escape sequence... */
	    esc = ES_ESCSEQ;		/* starts a new escape sequence */
	  break;			/* Intermediate or ignored ctrl char */

	case ES_ESCSEQ:			/* ESCSEQ -- in an escape sequence */
	  if (c > 057 && c < 0177)	/* Final character '0' thru '~' */
	    esc = ES_NORMAL;		/* Return to NORMAL state. */
	  else if (c == ESC)		/* ESC ... */
	    esc = ES_GOTESC;		/* starts a new escape sequence */
	  break;			/* Intermediate or ignored ctrl char */

	case ES_GOTCSI:			/* GOTCSI -- In a control sequence */
	  if (c > 077 && c < 0177)	/* Final character '@' thru '~' */
	    esc = ES_NORMAL;		/* Return to NORMAL. */
	  else if (c == ESC)		/* ESC ... */
	    esc = ES_GOTESC;		/* starts over. */
	  break;			/* Intermediate or ignored ctrl char */

	case ES_STRING:			/* Inside a string */
	  if (c == ESC)			/* ESC may be 1st char of terminator */
	    esc = ES_TERMIN;		/* Go see. */
	  break;			/* Absorb all other characters. */

	case ES_TERMIN:			/* May have a string terminator */
	  if (c == '\\')		/* which must be backslash */
	    esc = ES_NORMAL;		/* If so, back to NORMAL */
	  else				/* Otherwise */
	    esc = ES_STRING;		/* Back to string absorption. */
      }
    return(esc);
}

/* end code to skip escape sequences */

/*
 * Send a character to the serial line in immediate mode, checking to avoid
 * overwriting a character already waiting to be sent.
 */
sendchar(unsigned char c) {
    int i=0;
    while (ttoci(dopar(c))<0 && i++<10)
      DosSleep(twochartimes);
    if (i>=10) {
	active = FALSE;
	strcpy(termessage,network ? "Cannot transmit to network link.\n"
	       : "Cannot transmit to serial port.\n");
    }
}

/* ------------------------------------------------------------------ */
/* ipadl25 -                                                          */
/* ------------------------------------------------------------------ */
ipadl25() {
    int i, j, n;
    char *s;
    if (tt_type == TT_VT102)
      strcpy(usertext, " C-Kermit VT-102");
    else if (tt_type == TT_VT52)
      strcpy(usertext, " C-Kermit VT-52");
    else strcpy(usertext, " C-Kermit");
    if ( !network )
      sprintf(filetext, "Serial %ld", speed);
    else
      switch ( nettype ) {
      case NET_DEC:
        switch ( ttnproto ) {
        case NP_LAT:
          strcpy(filetext, "DECnet LAT");
          break;
        case NP_CTERM:
          strcpy(filetext, "DECnet CTERM");
          break;
        default:
          strcpy(filetext, "DECnet");
          break;
        }
        break;
      case NET_TCPB:
        strcpy(filetext, "TCP/IP");
        break;
      case NET_PIPE:
        strcpy(filetext, "Named Pipe");
        break;
      }
    strcpy(helptext, "Help: Alt-?");
    strcpy(exittext, "Return: Alt-X");
    n = strlen(ttname);
    i = j = (n < 14) ? (14 - n) / 2 : 0;
    n = 14 - i;
    while (i-- > 0) hostname[i] = ' ';
    for (i = 0; i < n && ttname[i]; i++)
      hostname[i+j] = islower(ttname[i]) ? toupper(ttname[i]) : ttname[i];
    hostname[i+j] = '\0';
    line25();
}

/* ------------------------------------------------------------------ */
/* CursorUp -                                                         */
/* ------------------------------------------------------------------ */
static void
cursorup() {
    if ((relcursor ? margintop : 1) != wherey)
	lgotoxy(wherex, wherey - 1);
}

/* ------------------------------------------------------------------ */
/* CursorDown -                                                       */
/* ------------------------------------------------------------------ */
static void
cursordown() {
    if ((relcursor ? marginbot : ysize) != wherey)
	lgotoxy(wherex, wherey + 1);
}

/* ------------------------------------------------------------------ */
/* CursorRight -                                                      */
/* ------------------------------------------------------------------ */
static void
cursorright() {
    if (wherex < (dwl[wherey - 1] ? xsize - 1 : xsize))
	lgotoxy(wherex + 1, wherey);
}

/* ------------------------------------------------------------------ */
/* CursorLeft -                                                       */
/* ------------------------------------------------------------------ */
static void
cursorleft() {
    if (wherex > 1)
	lgotoxy(wherex - 1, wherey);
}

/* ------------------------------------------------------------------ */
/* ReverseScreen						      */
/* ------------------------------------------------------------------ */
static void
reversescreen() {
    unsigned char   back;
    unsigned char   fore;
    int             i, r, c;
    USHORT          n;
    unsigned char   cell[132 * 2];

    n = sizeof(cell);
    for (r = 0; r < ysize; r++) {	/* flip row */
	VioReadCellStr(cell, &n, r, 0, 0);
	for (c = 1; c < n; c += 2) {	/* do each cell in row */
	    back = (cell[c] & 0x70) >> 4;
	    fore = (cell[c] & 0x07);
	    if (cell[c] == colorunderline)
		cell[c] ^= 0x70;
	    else
		cell[c] = (cell[c] & 0x88) | fore << 4 | back;
	}
	VioWrtCellStr(cell, n, r, 0, 0);
    }
}

/* ----------------------------------------------------------------- */
/* ClrScreen -                                                       */
/* ----------------------------------------------------------------- */
static void
clrscreen() {
    int             i, j;
    int             nlines;
    USHORT          n;
    char            cells[132 * 2];

    /* copy lines on screen to extended display buffer */
    n = xsize * 2;
    for (i = ysize - 1; i >= 0; i--) {
	VioReadCellStr((char *) cells, &n, i, 0, 0);
	for (j = 0; j < xsize; j++) {
	    if (cells[j * 2] != ' ')
		break;
	}
	if (j < xsize)
	    break;
    }
    nlines = i;			/* no. of nonblank lines-1 */
    for (i = 0; i <= nlines; ++i) {
	paginginfo.botline = (paginginfo.botline + 1) % LBUFSIZE;
	if (paginginfo.numlines < LBUFSIZE)
	    paginginfo.numlines = paginginfo.numlines + 1;
	else
	    paginginfo.topline = (paginginfo.topline + 1) % LBUFSIZE;
	VioReadCellStr((paginginfo.buffer + xsize * 2 * paginginfo.botline),
		       &n, i, 0, 0);
    }
    for (i = 0; i < ysize; i++)
	dwl[i] = FALSE;
    dwls = FALSE;
    clearscreen(0, attribute);
}

static void
readmchar_escape() {
    /* Stores character in achar directly */
    if (escnext <= esclast) {
	achar = escbuffer[escnext];
	escnext = escnext + 1;
    } else
	achar = 0;
}

static int
pnumber(int *achar) {
    int num = 0;

    while (isdigit(*achar)) {	/* get number */
	num = (num * 10) + (*achar) - 48;
	readmchar_escape();
    }
    return (num);
}

static void
clreoscr_escape() {
    char cell[2];
    int i;

    if (wherex == 1 && wherey == 1) {
	clrscreen();
	return;
    }
    cell[0] = ' ';
    cell[1] = attribute;
    i = (xsize * ysize) - (((wherey - 1) * xsize) + (wherex - 1));
    VioWrtNCell(cell, i, wherey - 1, wherex - 1, 0);
    for (i = wherey - 1; i < ysize; i++)
	dwl[i] = FALSE;
    dwls = FALSE;
    for (i = 0; i < ysize; i++)
	if (dwl[i]) {
	    dwls = TRUE;
	    break;
	}
}

static void
clrboscr_escape() {
    char cell[2];
    int i;

    cell[0] = ' ';
    cell[1] = attribute;
    i = ((wherey - 1) * xsize) + wherex;
    VioWrtNCell(cell, i, 0, 0, 0);
    for (i = 0; i < wherey; i++)
	dwl[i] = FALSE;
    dwls = FALSE;
    for (i = 0; i < ysize; i++)
	if (dwl[i]) {
	    dwls = TRUE;
	    break;
	}
}

static void
clrbol_escape() {
    char cell[2];

    cell[0] = ' ';
    cell[1] = attribute;
    VioWrtNCell(cell, wherex, wherey - 1, 0, 0);
}

static void
clrline_escape() {
    char cell[2];

    cell[0] = ' ';
    cell[1] = attribute;
    VioWrtNCell(cell, xsize, wherey - 1, 0, 0);
}

static void
decdwl_escape(bool dwlflag) {
    unsigned char   linenumber;
    unsigned char   newx;
    char            cells[132 * 2];
    int             i;
    USHORT          n;
    /* Decdwl */
    linenumber = wherey - 1;
    if (dwlflag != dwl[linenumber]) {
	/* change size */
	n = xsize * 2;
	VioReadCellStr((char *) cells, &n, linenumber, 0, 0);
	if (dwlflag) {		/* make this line double size */
	    for (i = xsize / 2 - 1; i >= 0; --i) {	/* expand */
		cells[4 * i] = cells[2 *i];
		cells[4 * i + 2] = ' ';
	    }
	    newx = (wherex - 1) * 2 + 1;
	    dwls = TRUE;
	} else {		/* make this line single size */
	    for (i = 0; i <= xsize / 2 - 1; ++i)
		cells[2 * i] = cells[4 * i];
	    for (i = xsize / 2; i < xsize; ++i)
		cells[2 * i] = ' ';
	    newx = (wherex - 1) / 2 + 1;
	    dwls = FALSE;
	    for (i = 0; i < ysize; i++)
		if (dwl[i]) {
		    dwls = TRUE;
		    break;
		}
	}
	VioWrtCellStr((char *) cells, n, linenumber, 0, 0);
	dwl[linenumber] = dwlflag;
	if (newx >= xsize)
	  newx = xsize - 1;
	lgotoxy(newx, wherey);
    }
}

static void				/* Reset the terminal emulator */
doreset() {
    int i;
    defaultattribute = colornormal;
    attribute = defaultattribute;
    attrib.blinking = FALSE;
    attrib.bold = FALSE;
    attrib.invisible = FALSE;
    attrib.underlined = FALSE;
    attrib.reversed = FALSE;
    g0 = g1 = 'A';
    g0g1 = &g0;
    printon = FALSE;
    screenon = TRUE;
    vt52graphics = FALSE;
    saved = FALSE;
    linemode = FALSE;
    insertmode = FALSE;
    tt_arrow = TRUE;
    tt_keypad = FALSE;
    tt_wrap = TRUE;
    tt_type = TT_VT102;
    keylock = FALSE;
    deccolm = decscnm = FALSE;
    for (i = 0; i < ysize; i++)
      dwl[i] = FALSE;
    dwls = FALSE;
    for (i = 1; i < xsize; i++)
      htab[i] = (i % 8) == 1 ? 'T' : '0'; /* was "== 0" */
    relcursor = FALSE;
    setmargins(1, ysize);
    clrscreen();
}


static void
vtescape() {
    unsigned char   j;
    unsigned char   k;
    unsigned char   l;
    unsigned char   blankcell[2];
    int             i;
    int             pn[11];
    bool            private;
    char            tempstr[20];
    int             fore, back;
    escaping = FALSE;
    escnext = 1;
    readmchar_escape();
    if (screenon || (achar == '[')) {
	/* screen escape sequences  */
	switch (achar) {		/* First Level */
	case '[':
	    {				/* Left square bracket */
		readmchar_escape();
		switch (achar) {	/* Second level */
		case 'A':
		    cursorup();
		    wrapit = FALSE;
		    break;
		case 'B':
		    cursordown();
		    wrapit = FALSE;
		    break;
		case 'C':
		    cursorright();
		    if (dwl[wherey - 1])
		      cursorright();
		    break;
		case 'D':
		    cursorleft();
		    if (dwl[wherey - 1])
		      cursorleft();
		    break;
		case 'J':	/* Erase End of Display */
		    clreoscr_escape();
		    break;
		case 'K':
		    clrtoeoln();
		    break;
		case '?':
		    private = TRUE;
		    readmchar_escape();
		    goto LB2001;
		case 'f':
		case 'H':	/* Cursor Home */
		    lgotoxy(1, relcursor ? margintop : 1);
		    break;
		case 'g':	/* Tab Clear at this position */
		    htab[wherex] = '0';
		    break;
		case '}':
		case 'm':	/* Normal Video - Exit all attribute modes */
		    attribute = defaultattribute;
		    attrib.blinking = FALSE;
		    attrib.bold = FALSE;
		    attrib.invisible = FALSE;
		    attrib.underlined = FALSE;
		    attrib.reversed = FALSE;
		    break;
		case 'r':	/* Reset Margin */
		    setmargins(1, ysize);
		    lgotoxy(1, 1);
		    break;
		case 'c':
		case 'h':
		case 'l':
		case 'n':
		case 'x':
		    pn[1] = 0;
		    private = FALSE;
		    k = 1;
		    goto LB2003;
		case ';':
		    pn[1] = 0;
		    private = FALSE;
		    k = 1;
		    goto LB2002;
		case 'L':
		case 'M':
		case '@':
		case 'P':
		    pn[1] = 1;
		    private = FALSE;
		    k = 1;
		    goto LB2002;
		default:	/* Pn - got a number */
		    private = FALSE;
	    LB2001:
		    {		/* Esc [ Pn...Pn x   functions */
			pn[1] = pnumber(&achar);
			k = 1;
		LB2002:
			while (achar == ';') {	/* get Pn[k] */
			    readmchar_escape();
			    k++;
			    if (achar == '?') {
				readmchar_escape();
			    }
			    pn[k] = pnumber(&achar);
			}
			pn[k + 1] = 1;
		LB2003:
			switch (achar) { /* Third level */
			case 'A':
			    do {
				cursorup();
				wrapit = FALSE;
				pn[1] = pn[1] - 1;
			    }
			    while (!(pn[1] <= 0));
			    break;
			case 'B':
			    do {
				cursordown();
				wrapit = FALSE;
				pn[1] = pn[1] - 1;
			    }
			    while (!(pn[1] <= 0));
			    break;
			case 'C':
			    do {
				cursorright();
				if (dwl[wherey - 1])
				    cursorright();
				pn[1] = pn[1] - 1;
			    }
			    while (pn[1] > 0);
			    break;
			case 'D':
			    do {
				cursorleft();
				if (dwl[wherey - 1])
				    cursorleft();
				pn[1] = pn[1] - 1;
			    } while (pn[1] > 0);
			    break;
			case 'f':
			case 'H':
			    /* Direct cursor address */
			    if (pn[1] == 0)
				pn[1] = 1;
			    if (relcursor)
				pn[1] += margintop - 1;
			    if (pn[1] > ysize)
			        pn[1] = ysize - 1;
			    if (pn[2] == 0)
				pn[2] = 1;
			    if (dwl[pn[1] - 1]) {
				pn[2] = 2 * pn[2] - 1;
				if (pn[2] > xsize)
				    pn[2] = xsize - 1;
			    } else if (pn[2] > xsize)
				pn[2] = xsize;
			    wrapit = FALSE;
			    lgotoxy(pn[2], pn[1]);
			    break;
			case 'c':	/* Device Attributes */
			    if (pn[1] == 0)
				sendstr("[?6c");
			    break;
			case 'g':
			    if (pn[1] == 3) {
				/* clear all tabs */
				for (j = 1; j <=xsize; ++j)
				    htab[j] = '0';
			    } else if (pn[1] == 0)
				/* clear tab at current position */
				htab[wherex] = '0';
			    break;
			case 'h':	/* Set Mode */
			    for (j = 1; j <= k; ++j)
				if (private)
				    switch (pn[j]) {	/* Field specs */
				    case 1:	/* DECCKM  */
					tt_arrow = TRUE;
					break;
				    case 2:	/* DECANM : ANSI/VT52 */
					tt_type = TT_VT102;
					vt52graphics = FALSE;
					break;
				    case 3:	/* DECCOLM : Col = 132 */
					deccolm = TRUE;
					clrscreen();
					break;
				    case 4:	/* DECSCLM */
					break;
				    case 5:	/* DECSCNM */
					if (decscnm)
					    break;	/* Already set */
					decscnm = TRUE;
					reversescreen();
					defaultattribute = colornormal;
					attribute = defaultattribute;
					if (attrib.underlined)
					    attribute = colorunderline;
					if (attrib.reversed) {
					    attribute = colorreverse;
					    if (attrib.underlined)
						attribute = colorunderline &
						  0x0F | colorreverse & 0xF0;
					}
					if (attrib.blinking)
					    attribute |= 0x80;
					if (attrib.bold)
					    attribute ^= 8;
					if (attrib.invisible) {
					    i = (attribute & 0xF8);
					    attribute = i | ((i >> 4) & 7);
					}
					break;
				    case 6:	/* DECOM : Relative origin */
					relcursor = TRUE;
					lgotoxy(1, margintop);
					break;
				    case 7:	/* DECAWM */
					tt_wrap = TRUE;
					break;
				    case 8:	/* DECARM */
					break;
				    case 9:	/* DECINLM */
					break;
				    default:
					break;
				} else
				    switch (pn[j]) {
				    case 2:	/* Keyboard locked */
					keylock = TRUE;
					break;
				    case 4:	/* Ansi insert mode */
					insertmode = TRUE;
					break;
				    case 20:	/* Ansi linefeed mode */
					linemode = TRUE;
					break;
				    default:
					break;
				    }
			    break;
			case 'l':	/* Reset Mode */
			    for (j = 1; j <= k; ++j)
				if (private)
				    switch ((pn[j])) {	/* Field specs */
				    case 1:	/* DECCKM  */
					tt_arrow = FALSE;
					break;
				    case 2:	/* DECANM : ANSI/VT52 */
					tt_type = TT_VT52;
					vt52graphics = FALSE;
					break;
				    case 3:	/* DECCOLM : 80 col */
					deccolm = FALSE;
					clrscreen();
					break;
				    case 4:	/* DECSCLM */
					break;
				    case 5:	/* DECSCNM */
					if (!decscnm)
					    break;
					decscnm = !decscnm;
					reversescreen();
					defaultattribute = colornormal;
					attribute = defaultattribute;
					if (attrib.underlined)
					    attribute = colorunderline;
					if (attrib.reversed) {
					    attribute = colorreverse;
					    if (attrib.underlined)
						attribute = colorunderline &
						  0x0F | colorreverse & 0xF0;
					}
					if (attrib.blinking)
					    attribute |= 0x80;
					if (attrib.bold)
					    attribute ^= 8;
					if (attrib.invisible) {
					    i = (attribute & 0xF8);
					    attribute = i | ((i >> 4) & 7);
					}
					break;
				    case 6:	/* DECOM : Relative origin */
					relcursor = FALSE;
					lgotoxy(1, 1);
					break;
				    case 7:	/* DECAWM */
					tt_wrap = FALSE;
					break;
				    case 8:	/* DECARM */
					break;
				    case 9:	/* DECINLM */
					break;
				    default:
					break;
				} else
				    switch (pn[j]) {
				    case 2:	/* Keyboard unlocked */
					keylock = FALSE;
					break;
				    case 4:	/* Ansi insert mode */
					insertmode = FALSE;
					break;
				    case 20:	/* Ansi linefeed mode */
					linemode = FALSE;
					break;
				    default:
					break;
				    }
			    break;
			case 'i':	/* Printer Screen  on / off */
#ifndef M_I286
/*
  Not only does this not work, but it causes crashes ("stack overflow"), in
  the 16-bit version, at least for me -- but maybe that's because I don't have
  a printer.  For now, we'll leave it buggy in the 32-bit version and turn it
  off completely in the 16-bit version.  NEEDS DEBUGGING BY SOMEBODY WHO HAS A
  PRINTER.
*/
			    if (pn[1] == 0)
			      /* Need code here to print whole screen */
			      break;
			    else if (pn[1] == 1)
			      /* Need code here to print current line */
			      /* But only if private == TRUE */
			      break;
/* 
  For pn = 4 or 5, we should differentiate here between transparent print
  (private == FALSE) and autoprint (private == TRUE).  As presently coded,
  we always do transparent print.
*/
			    else if (pn[1] == 4)
			      turnoffprinter = TRUE;
			    else if (pn[1] == 5)
			      turnonprinter = TRUE;

/*  6 and 7 are not in the VT102 manual.  Maybe in the Scottish version?  */

			    else if (pn[1] == 6)
			      screenon = FALSE;
			    else if (pn[1] == 7)
			      screenon = TRUE;
#endif /* M_I286 */
			    break;
			case 'q':		/* Load LEDs */
			    break;		/* (nothing) */
			case 'n':
			    if (pn[1] == 5) 	/* Terminal Status Report */
			      sendstr("[0n"); 	/* Terminal is OK */
			    else if (pn[1] == 6) { /* Cursor position report */
#ifdef M_I286
/*
  16-bit version must use a hand-coded in-line version of sprintf to avoid
  the function call that would crash the program with a stack overflow.
  Handles numbers 0 - 999.  No range checking.  - fdc
*/
				int i = 0, j;
				tempstr[i++] = '[';
				if ((j = wherey / 100) > 0)
				  tempstr[i++] = (char) (j + 48);
				if ((j = (wherey % 100) / 10) > 0 ||
				    wherey > 99)
				  tempstr[i++] = (char) (j + 48);
				if ((j = wherey % 10) > 0 ||
				    wherey > 9)
				  tempstr[i++] = (char) (j + 48);
				tempstr[i++] = ';';
				if ((j = wherex / 100) > 0)
				  tempstr[i++] = (char) (j + 48);
				if ((j = (wherex % 100) / 10) > 0 ||
				    wherex > 99)
				  tempstr[i++] = (char) (j + 48);
				if ((j = wherex % 10) > 0 ||
				    wherex > 9)
				  tempstr[i++] = (char) (j + 48);
				tempstr[i++] = 'R';
				tempstr[i] = '\0';
				sendstr(tempstr);
#else
/* 32-bit version can call sprintf() here.  This is the original code. */
				sendstr("[");
				sprintf(tempstr,"%1d",(int) wherey); /* row */
				sendchar(tempstr[0]);
				if (tempstr[1])
				    sendchar(tempstr[1]);
				sendchar(';');
				sprintf(tempstr,"%1d",(int) wherex); /* col */
				sendchar(tempstr[0]);
				if (tempstr[1])
				    sendchar(tempstr[1]);
				sendchar('R');
#endif /* M_286 */
			    }
			    break;
			case 'x':	/* Request terminal Parameters */
			    if (pn[1] > 1)
				break;
			    tempstr[0] = '[';
			    tempstr[1] = (pn[1] == 0) ? '2' : '3';
			    tempstr[2] = ';';
			    switch (parity) {			    
			    case 'e':
				tempstr[3] = '5';
				tempstr[5] = '2';
				break;
			    case 'o':
				tempstr[3] = '4';
				tempstr[5] = '2';
				break;
			    case 0:
				tempstr[3] = '1';
				tempstr[5] = '1';
				break;
			    default:
				tempstr[3] = '1';
				tempstr[5] = '2';
				break;
			    }
			    tempstr[4] = ';';
			    switch (speed) {
			    case 50:
				i = 0;
				break;
			    case 75:
				i = 8;
				break;
			    case 110:
				i = 16;
				break;
			    case 133:
				i = 14;
				break;
			    case 150:
				i = 32;
				break;
			    case 200:
				i = 40;
				break;
			    case 300:
				i = 48;
				break;
			    case 600:
				i = 56;
				break;
			    case 1200:
				i = 64;
				break;
			    case 1800:
				i = 72;
				break;
			    case 2000:
				i = 80;
				break;
			    case 2400:
				i = 88;
				break;
			    case 3600:
				i = 96;
				break;
			    case 4800:
				i = 104;
				break;
			    case 9600:
				i = 112;
				break;
			    case 19200:
				i = 120;
				break;
			    default:
				i = 128;
				break;
			    }
#ifdef M_I286
/*
  Hand-coded sprintf() again.  - fdc.
*/
			    {
				int x, j;
				x = i;
				i = 6;
				tempstr[i++] = ';';
				if ((j = x / 100) > 0)
				  tempstr[i++] = (char) (j + 48);
				if ((j = (wherey % 100) / 10) > 0 ||
				    wherey > 99)
				  tempstr[i++] = (char) (j + 48);
				if ((j = wherey % 10) > 0 ||
				    wherey > 9)
				  tempstr[i++] = (char) (j + 48);
				tempstr[i++] = ';';
				if ((j = x / 100) > 0)
				  tempstr[i++] = (char) (j + 48);
				if ((j = (wherey % 100) / 10) > 0 ||
				    wherey > 99)
				  tempstr[i++] = (char) (j + 48);
				if ((j = wherey % 10) > 0 ||
				    wherey > 9)
				  tempstr[i++] = (char) (j + 48);
				tempstr[i++] = ';';
				tempstr[i++] = '1';
				tempstr[i++] = ';';
				tempstr[i++] = '0';
				tempstr[i++] = 'x';
				tempstr[i++] = '\0';
			    }
#else
			    sprintf(&tempstr[6], ";%d;%d;1;0x", i, i);
#endif /* M_I286 */
			    sendstr(tempstr);
			    break;
			case 'm':	/* Select graphic rendition */
			case '}':
			    for (j = 1; j <= k; ++j)
				switch ((pn[j])) {	/* Field specs */
				case 0:	/* normal */
				    attribute = defaultattribute;
				    attrib.blinking = FALSE;
				    attrib.bold = FALSE;
				    attrib.invisible = FALSE;
				    attrib.underlined = FALSE;
				    attrib.reversed = FALSE;
				    break;
				case 1:	/* bold */
				    attrib.bold = TRUE;
				    attribute ^= 8;
				    break;
				case 4:	/* underline */
				    attrib.underlined = TRUE;
				    attribute = colorunderline;
				    break;
				case 5:	/* blink */
				    attrib.blinking = TRUE;
				    attribute |= 0x80;
				    break;
				case 7:	/* reverse video */
				    if (attrib.reversed)
					break;
				    attrib.reversed = TRUE;
				    attribute = colorreverse;
				    if (attrib.underlined)
				        attribute = colorunderline &
					  0x0F |
					    colorreverse & 0xF0;
				    if (attrib.invisible)
				        attribute = attribute &
					  0xF0 |
					    (attribute >> 4) & 0x07;
				    break;
				case 8:	/* invisible */
				    if (attrib.invisible)
					break;
				    attrib.invisible = TRUE;
				    attribute = attribute &
				      0xF0 |
					(attribute >> 4) & 0x07;
				    break;
				case 30:
				case 31:
				case 32:
				case 33:
				case 34:
				case 35:
				case 36:
				case 37:
				    /* select foreground */
				    i = (attribute & 0xF8);
				    attribute = (i | sgrcols[pn[j] - 30]);
				    break;
				case 40:
				case 41:
				case 42:
				case 43:
				case 44:
				case 45:
				case 46:
				case 47:
				    /* select back ground */
				    i = (attribute & 0x8F);
				    l = sgrcols[pn[j] - 40];
				    attribute = (i | ((l << 4)));
				    break;
				default:
				    break;
				}
			    break;
			case 'r':	/* set margin */
			    if ((k < 2) || (pn[2] == 0))
				pn[2] = ysize;
			    if (pn[1] == 0)
				pn[1] = 1;
			    if ((pn[1] > 0) &&
				(pn[1] < pn[2]) &&
				(pn[2] <= ysize)) {
				setmargins(pn[1], pn[2]);
				lgotoxy(1, relcursor ? margintop : 1);
			    }
			    break;
			case 'J':
			    switch ((pn[1])) {
			    case 0:	/* clear to end of screen */
				clreoscr_escape();
				break;
			    case 1:	/* clear to beginning */
				clrboscr_escape();
				break;
			    case 2:	/* clear all of screen */
				clrscreen();
				break;
			    default:
				break;
			    }
			    break;
			case 'K':
			    switch ((pn[1])) {
			    case 0:	/* clear to end of line */
				clrtoeoln();
				break;
			    case 1:	/* clear to beginning */
				clrbol_escape();
				break;
			    case 2:	/* clear line */
				clrline_escape();
				break;
			    default:
				break;
			    }
			    break;
			case 'L':	/* Insert lines */
			    for (i = 1; i <= pn[1]; ++i)
				scroll(DOWNWARD, wherey - 1, marginbot - 1);
			    break;
			case 'M':	/* Delete lines */
			    for (i = 1; i <= pn[1]; ++i)
				scroll(UPWARD, wherey - 1, marginbot - 1);
			    break;
			case '@':	/* Insert characters */
			    blankcell[0] = ' ';
			    blankcell[1] = attribute;
			    pn[1] *= dwl[wherey - 1] ? 2 : 1;
			    if (pn[1] > xsize + 1 - wherex)
				pn[1] = xsize + 1 - wherex;
			    VioScrollRt(wherey - 1,
					wherex - 1,
					wherey - 1,
					xsize - 1,
					pn[1],
					blankcell,
					0
					);
			    break;
			case 'P':	/* DeleteChar */
			    blankcell[0] = ' ';
			    blankcell[1] = attribute;
			    pn[1] *= dwl[wherey - 1] ? 2 : 1;
			    if (pn[1] > xsize + 1 - wherex)
				pn[1] = xsize + 1 - wherex;
			    VioScrollLf(wherey - 1,
					wherex - 1,
					wherey - 1,
					xsize - 1,
					pn[1],
					blankcell,
					0
					);
			    break;
			default:
			    break;
			}
		    }
		    break;
		}
	    }			/* Left square bracket */
	    break;
	case '7':		/* Save cursor position and attributes */
	    saved = TRUE;		/* Remember they are saved */
	    row = wherey;		/* Current row (absolute) */
	    column = wherex;		/* Current column (absolute) */
	    savedattribute = attribute;	/* Current attributes */
	    savedattrib = attrib;	/* Character attributes structure */
	    saverelcursor = relcursor;	/* Cursor addressing mode */
	    saveg0 = g0;		/* Character sets */
	    saveg1 = g1;
	    saveg0g1 = g0g1;
	    break;
	case '8':		/* Restore Cursor Position and attributes */
	    if (saved == FALSE) {	/* Nothing saved, home the cursor */
		lgotoxy(1, relcursor ? margintop : 1);
	    	break;
	    }
#ifdef COMMENT
/*
  Wrong!  Restoring the saved parameters does not unsave them.  -fdc
*/
	    saved = FALSE;
#endif /* COMMENT */
	    lgotoxy(column, row);	/* Goto saved position */
	    attribute = savedattribute;	/* Restore saved attributes */
	    attrib = savedattrib;
	    relcursor = saverelcursor;	/* Restore cursor addressing mode */
	    g0 = saveg0;		/* Restore character sets */
	    g1 = saveg1;
	    g0g1 = saveg0g1;
	    break;
	case 'A':
	    if (tt_type == TT_VT52)	/* VT52 control */
		cursorup();
	    break;
	case 'B':
	    if (tt_type == TT_VT52)	/* VT52 control */
		cursordown();
	    break;
	case 'C':
	    if (tt_type == TT_VT52)	/* VT52 control */
		cursorright();
	    break;
	case 'D':
	    if (tt_type == TT_VT52)	/* VT52 control */
	      cursorleft();
	    else {
		/* Index */
		if (wherey == marginbot)
		  scroll(UPWARD, margintop - 1, marginbot - 1);
		else
		  cursordown();
	    }
	    break;
	case 'E':			/* Next Line */
	    wrtch(13);
	    wrtch(10);
	    break;
	case 'F':
	    if (tt_type == TT_VT52)	/* VT52 control */
		vt52graphics = TRUE;
	    break;
	case 'G':
	    if (tt_type == TT_VT52)	/* VT52 control */
		vt52graphics = FALSE;
	    break;
	case 'H':
	    if (tt_type == TT_VT102) {
		/* Set Tab Stop */
		htab[wherex] = 'T';
	    }
	    /* Set Tab Stop */
	    else
		lgotoxy(1, 1);
	    /* VT52 control */
	    break;
	case 'I':
	    if (tt_type == TT_VT52) {
		/* VT52 control */
		if ((margintop < wherey))
		    cursorup();
		else
		    scroll(DOWNWARD, margintop - 1, marginbot - 1);
	    }
	    break;
	case 'J':
	    if (tt_type == TT_VT52)
		/* VT52 control */
		clreoscr_escape();
	    break;
	case 'K':
	    if (tt_type == TT_VT52)
		/* VT52 control */
		clrtoeoln();
	    break;
	case 'M':
	    /* Reverse Index */
	    if (margintop == wherey)	/* "==", not ">="!  - fdc */
	      scroll(DOWNWARD, margintop - 1, marginbot - 1);
	    else
	      cursorup();
	    break;
	case 'Y':
	    if (tt_type == TT_VT52) {	/* VT52 control */
		/* direct cursor address */
		readmchar_escape();
		row = achar - 31;
		readmchar_escape();
		column = achar - 31;
		lgotoxy(column, row);
	    }
	    /* direct cursor address */
	    break;
	case 'Z':
	    if (tt_type == TT_VT102) {
		/* Device Attributes */
		/* Send  Esc[?6c */ /* (was "ESC [ ? 6 ; 2 c"  - fdc) */
		sendstr("[?6c");
	    }
	    /* Device Attributes */
	    else
		/* VT52 control */
		sendstr("/Z");
	    break;
	case 'c':
	    /* Reset */
	    doreset();
	    break;
	case '#':
	    /* Esc # sequence */
	    readmchar_escape();
	    switch (achar) {
	    case '3':
		decdwl_escape(TRUE);
		break;
	    case '4':
		decdwl_escape(TRUE);
		break;
	    case '5':
		decdwl_escape(FALSE);
		break;
	    case '6':
		decdwl_escape(TRUE);
		break;
	    case '8':
		{
		    char            cell[2];
		    cell[0] = 'E';
		    cell[1] = 7;
		    /* Self Test */
		    VioWrtNCell(cell, 1920, 0, 0, 0);
		    setmargins(1, ysize);
		    lgotoxy(1, 1);
		}
		break;
		/* Self Test */
	    default:
		break;
	    }
	    break;
	    /* Esc # sequence */
	case '=':
	    tt_keypad = FALSE;
	    break;
	case '>':
	    tt_keypad = TRUE;
	    break;
	case '<':
	    /* VT52 control */
	    tt_type = TT_VT102;
	    break;
	case '(':
	    readmchar_escape();
	    g0 = achar;
	    break;
	case ')':
	    readmchar_escape();
	    g1 = achar;
	    break;
	default:
	    if (achar == 12) {
		lgotoxy(1, 1);
		clrscreen();
	    }
	    break;
	}
	/* First Level Case  */
    } /* Screen escape sequences  */

/* Host said to turn off the printer. */

  if (turnoffprinter) {
      turnoffprinter = FALSE;
      if (lst && !lstclosed)
	fclose(lst);
      lstclosed = TRUE;
      printon = FALSE;
  }

/* If printer is on, print this escape sequence. */

    if (printon) {
	fprintf(lst, "%c", 27);
	if (esclast > 0) {
	    /* print esc sequence */
	    for (i = 1; i <= esclast; ++i)
	      fprintf(lst, "%c", escbuffer[i]);
	}
    }
/*
  If we just got a "printer on" directive, turn on the printer now.
  This way, the "printer on" directive itself is not printed.
*/
    if (turnonprinter) {		/* Last command was "printer on" */
	turnonprinter = FALSE;
	if (lstclosed || !lst)		/* Open printer device */
	  lst = fopen("prn", "w");
	if (lst) {			/* Open OK? */
	    lstclosed = FALSE;		/* So not closed */
	    printon = TRUE;		/* and printer is on. */
	}
    }
}

/* ================================================================== */
/* VT100 emulate a DEC VT100 terminal writing a character             */
/* ================================================================== */
static void
vt100(unsigned char vtch) {
    int             i, j;
    char           *s, str[2];
    /* vt100 */
    if (screenon) {
	if (vtch < 32) {	/* Control Character */
	    achar = vtch;	/* Let the rest of this module see the value */
	    switch (achar) {
	    case LF:
	        if (linemode) wherex=1;
		wrtch((char) achar);
		break;
	    case CR:
		wrtch((char) achar);
		break;
		/* ESC */
	    case 27:
		vtescape();
		break;
	    case 14:		/* SO */
		g0g1 = &g1;
		break;
		/* SI */
	    case 15:
		g0g1 = &g0;
		break;
		/* BS */
	    case 8:
		wrtch((char) achar);
		break;
	    case 12:		/* FF */
	    case 11:		/* VT */
		/* take as lf */
		achar = 10;
		wrtch(10);
		break;
	    case 7:		/* BEL */
		bleep();
		break;
	    case 5:		/* ENQ */
		s = answerback;
		while (*s)
		    sendchar(*s++);
		break;
	    case 9:		/* tab character */
		j = dwl[wherey - 1] ? 2 : 1;
		i = wherex;
		if (j == 2 && htab[(i - 1) / j + 1] == 'T') {
		    i++;
		    cursorright();
		}
		if (i < xsize)
		    do {
			i++;
			cursorright();
		    } while ((htab[(i - 1) / j + 1] != 'T') &&
			     (i <= xsize - j));
		break;
	    default:		/* ignore it */
		break;
	    }
	    /* end of Control Character */
	} else {
	    if (vtch != DEL) {	/* Normal char */
		if (tt_type == TT_VT102) {
		    if (*g0g1 == 'A') {	/* UK ascii set */
			/* if (vtch == 35)
			    vtch = 156; */
		    } else if ((*g0g1 == '0') &&
			       (95 <= vtch) && (vtch <= 126)) {
			literal = TRUE;
			vtch = graphicset[vtch - 95];
		    }
		} else {
		    if (vt52graphics && (95 <= vtch) && (vtch <= 126)) {
			literal = TRUE;
			vtch = graphicset[vtch - 95];
		    }
		}
		if (wherex != (dwl[wherey - 1] ? xsize - 1 : xsize)) {
		    wrtch(vtch);
		    if (dwl[wherey - 1])
			wrtch(' ');
		    wrapit = FALSE;
		} else {
		    if (wrapit) {	/* Next line  */
			if (marginbot <= wherey) {	/* Scroll up */
			    scroll(UPWARD, margintop - 1, marginbot - 1);
			    lgotoxy(1, wherey);
			} else
			    lgotoxy(1, wherey + 1);
			wrtch(vtch);
			if (dwl[wherey - 1])
			    wrtch(' ');
			wrapit = FALSE;
		    } else {	/* put char on last column */
			i = dwl[wherey - 1] ? 2 : 1;
			str[0] = vtch;
			str[1] = ' ';
			VioWrtCharStrAtt(&vtch, i,
					 wherey - 1,
					 xsize - i,
					 &attribute,
					 0);
			literal = FALSE;
			if ((tt_wrap && !deccolm))
			    wrapit = TRUE;
		    }
		}
	    }			/* Normal char */
	}
    }
    if (printon && (vtch != 27))
      fprintf(lst, "%c", vtch);
}

/* save current status of screen */
savescreen(ascreen *scrn) {
    USHORT n = xsize * (ysize + 1) * 2;
    scrn->ox = wherex;
    scrn->oy = wherey;
    scrn->att = attribute;
    VioReadCellStr((char *) (scrn->scrncpy), &n, 0, 0, 0);
}

/* restore state of screen */
restorescreen(ascreen *scrn) {
    movetoscreen(scrn->scrncpy, 1, 1, xsize * (ysize + 1) * 2);
    attribute = scrn->att;
    wherey = scrn->oy;
    wherex = scrn->ox;
    lgotoxy(wherex, wherey);
}

#ifdef M_I286
/* Avoid 16-bit stack overflows */
#define logchar(c) zchout(ZSFILE,c)
#else
void
logchar(char c) {
    if (zchout(ZSFILE,c) < 0) {
	conoll("");
	conoll("ERROR WRITING SESSION LOG, LOG CLOSED!");
	seslog = 0;
    }
}
#endif /* M_I286 */

sendcharduplex(unsigned char key) {
    unsigned char csave;

    key &= cmdmsk;		/* Do any requested masking */
    csave = key;

    if (outesc == ES_NORMAL) { /* If not inside escape seq.. */
	/* Translate character sets */
	if (sxo) key = (*sxo)(key); /* Local to intermediate. */
	if (rxo) key = (*rxo)(key); /* Intermediate to remote. */
    }
    if (oskipesc) outesc = chkaes(outesc, key);
    /* Check escape sequence status */

    if (sosi) {		     	     /* Shift-In/Out selected? */
	if (cmask == 0177) {         /* In 7-bit environment? */
	    if (key & 0200) {        /* 8-bit character? */
		if (outshift == 0) { /* If not shifted, */
		    sendchar(SO);    /* shift. */
		    outshift = 1;
		}
	    } else {
		if (outshift == 1) { /* 7-bit character */
		    sendchar(SI);    /* If shifted, */
		    outshift = 0;    /* unshift. */
		}
	    }
	}
	if (key == SO) outshift = 1;    /* User typed SO */
	if (key == SI) outshift = 0;    /* User typed SI */
    }

    key &= cmask;		/* Apply Kermit-to-host mask now. */
    if (tnlm && key == '\015') {   /* Handle TERMINAL NEWLINE */
        sendchar(dopar('\015'));   /* Send the CR */
        if (duplex) {              /* If local echoing... */
	   cwrite('\015');         /*   echo to screen */
           if (seslog)             /*   and if logging */
	     logchar(csave);       /*   log it */
        }
        csave = key = '\012';   /* Now insert a linefeed */
    }
    sendchar(key);
    if (duplex) {
	cwrite(csave);
	if (seslog) logchar(csave);
    }
}

sendstrduplex(unsigned char *s, int escape) {
    if (escape)
        sendcharduplex(27);
    for ( ; *s; s++ )
        sendcharduplex(*s);
}

void
vt100key(int key) {
    void doesc(int);
    int helpconnect(int);
    int             i;
    int             il;
    char            str[3];
    int             prt;
    int             st;
    unsigned char   nolblines;
    unsigned char   linesleft;
    unsigned char   nextline;
    unsigned char   nlines;

    if ( macrotab[key] ) {
      sendstrduplex(macrotab[key], 0);
      return;
    }

    key = keymap[key];

    if (key < 0x100) {
	sendcharduplex((char) key);
	if (key == CR && linemode)
	    sendcharduplex(LF);
        return;
    }

    switch (key & 0xFF) {
    case 72:
        /* up */
        if (tt_type == TT_VT102) {
            if (tt_arrow)
                sendstrduplex("OA", 1);
            else
                sendstrduplex("[A", 1);
        } else
            sendstrduplex("A", 1);
        break;
    case 75:
        /* left */
        if (tt_type == TT_VT102) {
            if (tt_arrow)
                sendstrduplex("OD", 1);
            else
                sendstrduplex("[D", 1);
        } else
            sendstrduplex("D", 1);
        break;
    case 77:
        /* right */
        if (tt_type == TT_VT102) {
            if (tt_arrow)
                sendstrduplex("OC", 1);
            else
                sendstrduplex("[C", 1);
        } else
            sendstrduplex("C", 1);
        break;
    case 80:
        /* down */
        if (tt_type == TT_VT102) {
            if (tt_arrow)
                sendstrduplex("OB", 1);
            else
                sendstrduplex("[B", 1);
        } else
            sendstrduplex("B", 1);
        break;
    case 15:                /* Backtab */
        /* backspace */
        sendcharduplex(8);
        break;
    case 83:                /* DEL */
        /* delete */
        sendcharduplex(127);
        break;
    case 59:                /* F1 */
        /* PF1 */
        if (tt_type == TT_VT102)
            sendstrduplex("OP", 1);
        else
            sendstrduplex("P", 1);
        break;
    case 60:                /* F2 */
        /* PF2 */
        if (tt_type == TT_VT102)
            sendstrduplex("OQ", 1);
        else
            sendstrduplex("Q", 1);
        break;
    case 61:
        /* PF3 */           /* F3 */
        if (tt_type == TT_VT102)
            sendstrduplex("OR", 1);
        else
            sendstrduplex("R", 1);
        break;
    case 62:                /* F4 */
        /* PF4 */
        if (tt_type == TT_VT102)
            sendstrduplex("OS", 1);
        else
            sendstrduplex("S", 1);
        break;
    case 120:               /* Alt-1 ... Alt-9 */
    case 121:
    case 122:
    case 123:
    case 124:
    case 125:
    case 126:
    case 127:
    case 128:
    case 129:		   /* This one is Alt-0 */
        /* numeric 1-9,0 */
	if (tt_keypad) {		/* Keypad digits in numeric mode*/
	    if (key == 129) key = 48;	/* Watch out for zero */
	    else key -= 71;
	    sendcharduplex((unsigned char)key);	/* send digit */
        } else {
            key = 'q' + (key & 0xFF) - 120;
            if (key == 'z')
                key = 'p';
            /* alt 0 */
            if (tt_type == TT_VT102)
                strcpy(str, "O ");
            else
                strcpy(str, "? ");
            str[1] = key;
            sendstrduplex(str, 1);
        }
        break;
    case 63:				/* Keypad minus = F5 or ... */
    case 64:				/* F6 */
	if (tt_keypad)			/* Keypad in numeric mode */
	    sendcharduplex('-');
        else if (tt_type == TT_VT102)	/* Application mode, VT102 */
            sendstrduplex("Om", 1);
        else				/* Application mode, VT52 */
            sendstrduplex("?m", 1);
        break;
    case 65:				/* Keypad comma = F7 or ... */
    case 66:				/* F8 */
	if (tt_keypad)
	    sendcharduplex(',');
        else if (tt_type == TT_VT102)
            sendstrduplex("Ol", 1);
        else
            sendstrduplex("?l", 1);
        break;
    case 67:				/* Keypad period = F9 */
	if (tt_keypad)
	    sendcharduplex('.');
        else if (tt_type == TT_VT102)
            sendstrduplex("On", 1);
        else
            sendstrduplex("?n", 1);
        break;
    case 68:				/* Keypad Enter = F10 */
	if (tt_keypad) {
	    sendcharduplex('\015');	/* Numeric mode, send CR */
	    if (tnlm)
	      sendcharduplex('\012');	/* Newline mode, send LF too */
        } else if (tt_type == TT_VT102)
	  sendstrduplex("OM", 1);
        else
	  sendstrduplex("?M", 1);
        break;
    case 255:                /* Scroll-Lock */
        {
            strcpy(usertext, " Scroll-Lock ");
            *exittext = '\0';
            *helptext = '\0';
            line25();
            while (keymap[congks(0)] != 0x1FF) ;
            ipadl25();
        }
        break;
    case 81:                /* Page-Down */
    case 118:               /* Control-Page-Down */
        bleep();
        /* since not in extended display mode */
        break;
    case 71:                /* Home           enter extended display mode */
    case 73:                /* Page-Up  */
    case 132:               /* Control-Page-Up */
        if (paginginfo.numlines == 0)
            bleep();
        else {
            savescreen(&savedscreen);
            killcursor();
            linesleft = paginginfo.numlines;
            nextline = paginginfo.botline;
            do {
                if ((key & 0xFF) == 71 || (key & 0xFF) == 73 || 
		    (key & 0xFF) == 132) {  /* Page Up or Home */
                    if (linesleft == 0)
                        bleep();
                    else {
                        /* scroll up a page */
                        nlines = (key & 0xFF) == 73 ? ysize
			       : (key & 0xFF) == 71 ? linesleft : 1;
                        if (nlines > linesleft)
                            nlines = linesleft;
			if (nlines > ysize) {
			    nextline = paginginfo.topline + ysize - 1;
			    for (il = ysize; il >= 1; --il) {
                                movetoscreen(paginginfo.buffer + 
					     xsize * 2 * nextline, 
					     1, il, xsize * 2);
				nextline--;
                            }
			}
			else
			    for (il = 1; il <= nlines; ++il) {
                                scroll(DOWNWARD, 0, ysize - 1);
                                movetoscreen(paginginfo.buffer + 
					     xsize * 2 * nextline, 
					     1, 1, xsize * 2);
                                if (nextline == 0)
                                    nextline = LBUFSIZE - 1;
                                else
                                    nextline--;
                            }

                        linesleft = linesleft - nlines;
                    }
                } else if ((key & 0xFF) == 81 || (key & 0xFF) == 118) {
                    nlines = (key & 0xFF) == 81 ? ysize : 1;    /* Page Down */
                    do {
                        nextline = nextline + 1;
                        if (nextline >= LBUFSIZE)
                            nextline = 0;
                        linesleft = linesleft + 1;
                        /* lines of ext display above top of the screen */
                        nolblines = paginginfo.numlines - linesleft;
                        /* no. of ext disp buffer lines on screen */
                        scroll(UPWARD, 0, ysize - 1);
                        if (nolblines >= ysize) {
                            /* move from buffer */
                            i = nextline;
                            i = (i + ysize);
                            if (i >= LBUFSIZE)
                                i = i - LBUFSIZE;
                            movetoscreen(paginginfo.buffer + xsize * 2 * i,
					 1, ysize, xsize * 2);
                        } else {
                            /* move from the screen copy */
    movetoscreen(&(savedscreen.scrncpy[(ysize - 1 - nolblines) * xsize * 2]),
	     1, ysize, xsize * 2);
                        }
                        nlines = nlines - 1;
                    }
                    while (!(((nlines == 0) ||
			      (linesleft == paginginfo.numlines)))) ;
                } else
                    linesleft = paginginfo.numlines;
                if (linesleft != paginginfo.numlines) {
                    strcpy(helptext, "PageUp/Down");
                    strcpy(exittext, "Home/End");
                    strcpy(usertext, " Scrollback:");
                    line25();
                    if ( (key = keymap[congks(0)]) < 0x100 )
                        key = 32;
                }
            }
            while (linesleft != paginginfo.numlines);
            restorescreen(&savedscreen);
            newcursor();
            ipadl25();
        }
        break;
    case 16:				/* Alt-Q = Quit */
      doesc('Q'); break;
    case 45:				/* Alt-X = Return to prompt */
      doesc('C'); break;
    case 48:				/* Alt-B = Send BREAK */
      doesc('B'); break;
    case 38:				/* Alt-L = Send Long BREAK */
      doesc('L'); break;
    case 35:				/* Alt-H = Hangup */
      doesc('H'); break;
    case 131:				/* Alt-= is Reset */
      doreset();
#ifdef COMMENT
      clearscreen(0, attribute);
      savescreen(&vt100screen);
#endif /* COMMENT */
      break;
    case 53:				/* Alt-? or Alt-/ = Help */
      strcpy(usertext, " Press any key to restore the screen");
      exittext[0] = '\0';
      helptext[0] = '\0';
      line25();
      helpconnect(1);
      ipadl25();
      break;
    default:				/* Others, ignore */
        break;
    }
    /* of case */
}

/* ------------------------------------------------------------------ */
/* cwrite                                                             */
/* ------------------------------------------------------------------ */
cwrite(unsigned char ch)
{
    {
	/* check and process escape sequence */
	/* escape */
	if (ch == 27) {
	    escaping = TRUE;
	    esclast = 0;
	} else {
	    if (!(escaping)) {
		/* can send it to vt100 to be processed */
		vt100(ch);
	    } else {
		/* in the middle of an escape sequence */
		if (((ch == 24) || (ch == 26)))
		    /* cancelled */
		    escaping = FALSE;
		else {
		    if (ch < 32) {
			if (ch == 8) {	/* Backspace */
			    if (esclast >= 1)
				esclast--;
			}
		    } else {
			/* add to control string */
			if (esclast < 128) {
			    /* add to buffer */
			    esclast = esclast + 1;
			    escbuffer[esclast] = ch;
			}
			/* now check to see if sequence complete */
			{
			    if (tt_type == TT_VT102) {
				if (escbuffer[1] != '[') {
				    char            c = escbuffer[1];
				    if ((c != '#' &&
					 c != '(' &&
					 c != ')' &&
					 c != 'O' &&
					 c != '?') ||
					esclast >= 2) {
					if ((escbuffer[1] != 'Y'))
					    vtescape();
					else {
					    if ((esclast == 3))
						vtescape();
					}
				    }
				} else {
				    /* check for terminating character */
				    if ((((64 <= ch) &&
					  (ch <= 126)) &&
					 (esclast > 1)))
					vtescape();
				}
			    } else {
				/* vt52 mode */
				if ((escbuffer[1] != 'Y'))
				    vtescape();
				else {
				    if (esclast == 3)
					vtescape();
				}
			    }
			}
		    }
		}
	    }
	}
    }
}

/*---------------------------------------------------------------------------*/
/* scrninit                                                                  */
/*---------------------------------------------------------------------------*/
scrninit() {
    if (!scrninitialised) {
	scrninitialised = 1;
	defaultattribute = colornormal;
	attribute = defaultattribute;
	/* Initialise paging info */
        if ( paginginfo.buffer == NULL )
	  paginginfo.buffer = malloc(LBUFSIZE * xsize * 2);
	assert(paginginfo.buffer != NULL);
	paginginfo.numlines = 0;
	paginginfo.topline = 0;
	paginginfo.botline = LBUFSIZE - 1;
	clearscreen(0, attribute);
	savescreen(&vt100screen);
    }
}

/*---------------------------------------------------------------------------*/
/* bleep                                                                     */
/*---------------------------------------------------------------------------*/
bleep() {
    DosBeep(880, 50);
}

/*---------------------------------------------------------------------------*/
/* wrtch                                                                     */
/*---------------------------------------------------------------------------*/
wrtch(char ch) {
    char cell[2];
    if (ch >= ' ' || literal) {	/* Normal character */
	if ((tt_type == TT_VT102) && insertmode) {
	    cell[0] = ch;
	    cell[1] = attribute;
	    VioScrollRt(wherey - 1, wherex - 1, wherey - 1,
			xsize - 1, 1, cell, 0);
	} else
	    VioWrtCharStrAtt(&ch, 1, wherey - 1, wherex - 1, &attribute, 0);
	literal = FALSE;
	if (++wherex > xsize) {
	    wherex = 1;
	    wrtch(LF);
	}
    } else {			/* Control character */
	switch (ch) {
	case LF:
	    if (wherey == marginbot) {
		if (margintop == 1)
		    toplinetocyclicbuffer();
		scroll(UPWARD, margintop - 1, marginbot - 1);
	    } else {
		wherey++;
		if (wherey == ysize + 1)
		    wherey--;
	    }
	    break;
	case CR:
	    wherex = 1;
	    break;
	case BS:
	    if (wherex > 1)
		wherex--;
	    break;
	case 12:
	    if (wherex < xsize)
		wherex++;
	    break;
	case BEL:
	    DosBeep(440, 100);
	    break;
	default:{		/* Ignore */
	    }
	}
    }
    if (cursoron)
	VioSetCurPos(wherey - 1, wherex - 1, 0);
}

/*---------------------------------------------------------------------------*/
/* clearscreen                                                               */
/*---------------------------------------------------------------------------*/
clearscreen(int all, int attr) {
    char cell[2];
    cell[0] = ' ';
    cell[1] = attr;
    VioWrtNCell(cell, xsize * ysize + (all ? xsize : 0), 0, 0, 0);
    lgotoxy(1, 1);
}

/*---------------------------------------------------------------------------*/
/* lgotoxy                                                                   */
/*---------------------------------------------------------------------------*/
lgotoxy(int x, int y) {
    wherex = x;
    wherey = y;
    if (cursoron)
	VioSetCurPos(wherey - 1, wherex - 1, 0);
}

/*---------------------------------------------------------------------------*/
/* scroll                                                                    */
/*---------------------------------------------------------------------------*/
scroll(int updown, int top, int bottom) {
    char blankcell[2];
    int i;

    blankcell[0] = ' ';
    blankcell[1] = attribute;
    switch (updown) {
    case UPWARD:
	VioScrollUp(top, 0, bottom, xsize - 1, 1, blankcell, 0);
        VioShowBuf(0, xsize * 2, 0);
        /* There is a bug in the OS/2 2.0 8514/A PM driver that causes
         * wrong screen update after a scroll operation. We just force
         * a correct screen update of the line in question here. */
	if (dwls) {
	    for (i = top; i < bottom; i++)
		dwl[i] = dwl[i + 1];
	    dwl[bottom] = FALSE;
	}
	break;
    case DOWNWARD:
	VioScrollDn(top, 0, bottom, xsize - 1, 1, blankcell, 0);
        VioShowBuf(xsize * 2, xsize * 2, 0);
	if (dwls) {
	    for (i = bottom; i > top; i--)
		dwl[i] = dwl[i - 1];
	    dwl[top] = FALSE;
	}
	break;
    default: /* ignore */ ;
    }
    if (dwls) {
	dwls = FALSE;
	for (i = 0; i < ysize; i++)
	    if (dwl[i]) {
		dwls = TRUE;
		break;
	    }
    }
}

/*---------------------------------------------------------------------------*/
/* movetoscreen                                                              */
/*---------------------------------------------------------------------------*/
movetoscreen(char *source, int x, int y, int len) {
    VioWrtCellStr(source, len, y - 1, x - 1, 0);
}

/*---------------------------------------------------------------------------*/
/* toplinetocyclicbuffer                                                     */
/*---------------------------------------------------------------------------*/
toplinetocyclicbuffer() {
    USHORT n = xsize * 2;
    if (paginginfo.numlines == LBUFSIZE) {
	if (++paginginfo.topline == LBUFSIZE)
	    paginginfo.topline = 0;
    } else
	paginginfo.numlines++;
    if (++paginginfo.botline == LBUFSIZE)
	paginginfo.botline = 0;
    VioReadCellStr((paginginfo.buffer + xsize * 2 * paginginfo.botline),
		   &n, 0, 0, 0);
}

/*---------------------------------------------------------------------------*/
/* cleartoeol                                                                */
/*---------------------------------------------------------------------------*/
clrtoeoln() {
    char            cell[2];

    cell[0] = ' ';
    cell[1] = attribute;
    VioWrtNCell(cell, xsize + 1 - wherex, wherey - 1, wherex - 1, 0);
}

/*---------------------------------------------------------------------------*/
/* setmargins                                                                */
/*---------------------------------------------------------------------------*/
setmargins(int top, int bot) {
    margintop = top;
    marginbot = bot;
}

/*---------------------------------------------------------------------------*/
/* killcursor                                                                */
/*---------------------------------------------------------------------------*/
killcursor() {
    VIOCURSORINFO   nocursor;
    if (!cursoron)
	return 0;
    VioGetCurType(&crsr_info, 0);	/* Store current cursor type */
    nocursor = crsr_info;	/* MS C allows this */
    nocursor.attr = -1;
    VioSetCurType(&nocursor, 0);/* Hide cursor */
}

/*---------------------------------------------------------------------------*/
/* newcursor                                                                 */
/*---------------------------------------------------------------------------*/
newcursor() {
    VioSetCurType(&crsr_info, 0);
    VioSetCurPos(wherey - 1, wherex - 1, 0);
    cursoron = TRUE;
}

/*---------------------------------------------------------------------------*/
/* line25                                                                    */
/*---------------------------------------------------------------------------*/

strinsert(char *d, char *s) {
    while (*s)
	*d++ = *s++;
}

line25() {
    char s[132];
    int  i;
    char attr;

    for (i = 0; i < xsize; i++)
	s[i] = ' ';
    strinsert(&s[00], usertext);
    strinsert(&s[20], helptext);
    strinsert(&s[33], exittext);
    strinsert(&s[50], hostname);
    strinsert(&s[65], filetext);
    attr = colorstatus;
    VioWrtCharStrAtt(s, xsize, ysize, 0, &attr, 0);
}

sendstr(char * s) {
    sendchar(27);
    while (*s)
	sendchar(*s++);
}

/*
 * RDSERWRTSCR  --  Read the comms line and write to the screen.
 * This function is executed by a separate thread.
 */
#ifdef __32BIT__
long FAR
rdserwrtscr(long param)
#else
void FAR
rdserwrtscr()
#endif
{
    int c;

    DosSetPrty(PRTYS_THREAD, PRTYC_FOREGROUNDSERVER, 0, 0);
#ifdef __32BIT__
    DosPostEventSem(threadsem);
#else
    DosSemClear(&threadsem);	/* Let him know we've started */
#endif

    while (active) {
        c = ttinc(-50);
	if (!active)
	  break;
	if (c >= 0) {
	    DosEnterCritSec();

	    if (cursoron)
		if (ttchk() > 20) {
		    killcursor();
		    cursoron = FALSE;
		}

            c &= cmask;

	    if (sosi) {		/* Handle SI/SO */
		if (c == SO) {	/* Shift Out */
		    inshift = 1;
		    goto cont;
		} else if (c == SI) { /* Shift In */
		    inshift = 0;
		    goto cont;
		}
		if (inshift) c |= 0200;
	    }

	    /* Translate character sets */
	    if (inesc == ES_NORMAL) {
	      /* Translate character sets */
	      if (sxi) c = (*sxi)((CHAR)c);
	      if (rxi) c = (*rxi)((CHAR)c);
	    }
	    if (iskipesc) inesc = chkaes(inesc, (CHAR)c);
	    /* Adjust escape sequence status */

	    c &= cmdmsk;	/* Apply command mask. */
	    if (c == CR && tt_crd) { /* SET TERM CR-DISPLAY CRLF ? */
		cwrite((char) c);	/* Yes, output CR */
		if (seslog) logchar((char) c);
		c = LF;		     /* and insert a linefeed */
	    }
	    cwrite((char) c);
	    if (seslog) logchar((char) c);

cont:	    DosExitCritSec();
	} else if (c == -2) {
            active = FALSE;
	    strcpy(termessage, "Connection closed.\n");
            /* link broken */
	} else if (!cursoron) {
	    DosEnterCritSec();
	    newcursor();
	    DosExitCritSec();
	}
    }

#ifndef __32BIT__
    DosEnterCritSec();		/* Stop thread 1 discarding our stack before
				 * we've gone */
    DosSemClear(&threadsem);	/* Tell him we're going to die */
#endif
    DosExit(EXIT_THREAD, 0);
}

/* HELPCONNECT  --  Give help message for connect.  */

static int helpcol, helprow;
static int helpwidth;

void helpstart(int w, int h)
{
    unsigned char   cell[2];

    cell[1] = colorhelp;
    helpwidth = w;
    helpcol = (xsize - w) / 2;
    helprow = (ysize - h) / 2 - 1;
    cell[0] = 201;		/* Top left corner */
    VioWrtNCell(cell, 1, helprow, helpcol, 0);
    cell[0] = 205;		/* Horizontal */
    VioWrtNCell(cell, helpwidth, helprow, helpcol + 1, 0);
    cell[0] = 187;		/* Top right corner */
    VioWrtNCell(cell, 1, helprow, helpcol + helpwidth + 1, 0);
}

void
helpline(char *s) {
    unsigned char   cell[2];
    int             i;

    i = strlen(s);
    helprow++;
    cell[1] = colorhelp;
    cell[0] = 186;		/* Vertical */
    VioWrtNCell(cell, 1, helprow, helpcol, 0);
    VioWrtCharStrAtt(s, i, helprow, helpcol + 1, &cell[1], 0);
    cell[0] = ' ';
    VioWrtNCell(cell, helpwidth - i, helprow, helpcol + 1 + i, 0);
    cell[0] = 186;		/* Vertical */
    VioWrtNCell(cell, 1, helprow, helpcol + helpwidth + 1, 0);
}

void
helpend() {
    unsigned char   cell[2];

    helprow++;
    cell[1] = colorhelp;
    cell[0] = 200;		/* Bottom left corner */
    VioWrtNCell(cell, 1, helprow, helpcol, 0);
    cell[0] = 205;		/* Horizontal */
    VioWrtNCell(cell, helpwidth, helprow, helpcol + 1, 0);
    cell[0] = 188;		/* Bottom right corner */
    VioWrtNCell(cell, 1, helprow, helpcol + helpwidth + 1, 0);
}

int
helpconnect(int x) {
    int c, n;
    char line[80];
    static char    *hlpmsg[] = {
  "",
  " Command:   (SPACE to cancel)",
  "",
  "    C    to return to the C-Kermit prompt",
  "    H    to hangup and return to the prompt",
  "    Q    to hangup and quit C-Kermit",
  "    !    to enter an OS/2 command processor",
  "",
  "    0    (zero) to send a null",
  "   ^%c    to send the escape character",
  "    B    to send a BREAK",
  "    L    to send a LONG BREAK",
  "",
  "    \\    backslash escape (end with ENTER):",
  "         \\nnn   decimal code",
  "         \\Onnn  octal code",
  "         \\Xhh   hexadecimal code",
  ""};
#define HELPSIZE (sizeof(hlpmsg)/sizeof(char *))

    static char *altmsg[] = {
  "",
  " Alt-X to return to the C-Kermit prompt",
  " Alt-H to hangup and return to the prompt",
  " Alt-Q to hangup and quit Kermit",
  " Alt-B to send a BREAK",
  " Alt-L to send a Long BREAK",
  " Alt-= to reset terminal emulator",
  "",
  " or the CONNECT-mode escape character Ctrl-%c",
  " followed by ? for additional commands",
  ""};
#define ALTSIZE (sizeof(altmsg)/sizeof(char *))

    n = x ? ALTSIZE : HELPSIZE;

    savescreen(&savedscreen);
    killcursor();
    helpstart(47, n);
    for (c = 0; c < n; c++)
      if (strchr(x ? altmsg[c] : hlpmsg[c], '%')) {
	sprintf(line, x ? altmsg[c] : hlpmsg[c], ctl(escape));
	helpline(line);
      }
      else
	helpline(x ? altmsg[c] : hlpmsg[c]);
    helpend();
    c = keymap[congks(0)];
    newcursor();
    restorescreen(&savedscreen);
    return (c);
}

/* CHSTR  --  Make a printable string out of a character  */

char*
chstr(int c) {
    static char     s[8];
    char           *cp = s;

    if (c < SP) {
	sprintf(cp, "CTRL-%c", ctl(c));
    } else
	sprintf(cp, "'%c'\n", c);
    cp = s;
    return (cp);
}

/* DOESC  --  Process an escape character argument  */

void
esc25(int h) {
    strcpy(usertext, " Command:");
    strcpy(exittext, h ? "" : "Return: c");
    strcpy(helptext, h ? "" : "Help: ?");
    line25();
}

void
doesc(int c) {
    CHAR d;
    VIOMODEINFO mi;

    while (1) {
	if (c == escape) {	/* Send escape character */
	    sendchar((char)c);
	    return;
	} else
	 /* Or else look it up below. */ if (isupper(c))
	    c = tolower(c);

	switch (c) {

	case 'c':		/* return to command mode */
	case '\03':
	    active = 0;
	    return;

	case 'h':		/* Hangup and return to command mode */
	case '\010':
	    hangnow = 1;
	    active = 0;
	    strcpy(termessage, "Hangup.\n");
	    return;

	case 'q':               /* Hangup and quit */
	case '\021':
	    active = 0;
	    hangnow = 1;
	    quitnow = 1;
	    strcpy(termessage, "Hangup and quit.\n");
	    return;

	case '!':
            savescreen(&savedscreen);
            mi.cb = sizeof(mi);
            VioGetMode(&mi, 0);
            concooked();
            clearscreen(1, colorcmd);
            restorecursormode();
            puts("Enter EXIT to return to C-Kermit.");
	    zshcmd("");
	    conraw();
            connoi();
            VioSetMode(&mi, 0);
            setcursormode();
            restorescreen(&savedscreen);
	    return;

	case 'b':		/* Send a BREAK signal */
	case '\02':
	    ttsndb();
	    return;

	case 'l':		/* Send a LONG BREAK signal */
	case '\014':
	    ttsndlb();
	    return;

	case '0':		/* Send a null */
	    c = '\0';
	    sendchar((char)c);
	    return;

        case '\\':
            {
                char kbuf[32];
		char *kbp = kbuf, *text = usertext + 2;

		*kbp++ = c;

                strcpy(usertext, " \\");
                strcpy(exittext,"Exit: ENTER");
                helptext[0] = 0;
                line25();

		while (((c = (coninc(0) & cmdmsk)) != '\r') && (c != '\n')) {
                  if ( c == '\b' ) {
                    if ( kbp > kbuf + 1 ) {
		      *--kbp = 0;
                      *--text = 0;
                    }
                  } else if ( kbp - kbuf < sizeof(kbuf) - 2 ) {
		    *kbp++ = c;
                    *text++ = c;
                    *text = 0;
                  }
                  line25();
                }

		*kbp = 0; kbp = kbuf;
		c = xxesc(&kbp);	/* Interpret it */

		if (c >= 0) {
	            sendcharduplex((char)c);
		    return;
		} else {		/* Invalid backslash code. */
		    conoc(BEL);
		    return;
		}
	    }
            return;

	case SP:		/* Space, ignore */
	    return;

	default:		/* Other */
	    conoc(BEL);
	    return;		/* Invalid esc arg, beep */
	}
    }
}

/* CHECKSCREENMODE  --  Make sure we are in a usable mode */
checkscreenmode() {
    VIOMODEINFO     m;
    VIOINTENSITY vi;

    vi.cb = sizeof(vi);
    vi.type = 2;
    vi.fs = 1;
    VioSetState(&vi, 0);
    
    m.cb = sizeof(m);
    assert(VioGetMode(&m, 0) == 0);

    if ( xsize != min(132, m.col) || ysize != min(60, m.row) - 1 ) {
      scrninitialised = 0;
      if ( paginginfo.buffer )
        free(paginginfo.buffer);
      paginginfo.buffer = NULL;
      if ( vt100screen.scrncpy )
        free(vt100screen.scrncpy);
      vt100screen.scrncpy = NULL;
      if ( commandscreen.scrncpy )
        free(commandscreen.scrncpy);
      commandscreen.scrncpy = NULL;
      if ( savedscreen.scrncpy )
        free(savedscreen.scrncpy);
      savedscreen.scrncpy = NULL;
    }

    xsize = min(132, m.col);
    ysize = min(60, m.row) - 1;
    marginbot = ysize;

    if ( !vt100screen.scrncpy )
      vt100screen.scrncpy = malloc(xsize * (ysize + 1) * 2);
    assert(vt100screen.scrncpy != NULL);
    if ( !commandscreen.scrncpy )
      commandscreen.scrncpy = malloc(xsize * (ysize + 1) * 2);
    assert(commandscreen.scrncpy != NULL);
    if ( !savedscreen.scrncpy )
      savedscreen.scrncpy = malloc(xsize * (ysize + 1) * 2);
    assert(savedscreen.scrncpy != NULL);
}

setcursormode() {
    VIOMODEINFO vmi;
    VIOCURSORINFO vci;
    int cell, bottom, top;

    vmi.cb = sizeof(vmi);
    VioGetMode(&vmi, 0);
    cell   = vmi.vres / vmi.row;

    VioGetCurType(&vci, 0);
    crsr_command = vci;

    vci.cEnd   = cell - 1;
    vci.yStart = (tt_cursor == 0) ? cell - 2 :
                 cell - 1 - (cell - 2) * tt_cursor / 2;

    VioSetCurType(&vci, 0);
}

restorecursormode() {
    VioSetCurType(&crsr_command, 0);
}

/* CONECT  --  Perform terminal connection  */
int
conect() {
    USHORT          len, x, y;
    int             c, cm;	/* c is a character, but must be signed
				 * integer to pass thru -1, which is the
				 * modem disconnection signal, and is
				 * different from the character 0377 */
    char            errmsg[50], *erp, ac, bc, ss[132];

    if (!network && speed < 0) {
	printf("Sorry, you must set speed first.\n");
	return (0);
    }
    twochartimes = network ? 1 : 22000L / speed;

    if ((escape < 0) || (escape > 0177)) {
	printf("Your escape character is not ASCII - %d\n", escape);
	return (0);
    }
    if (ttopen(ttname, &local, network ? -nettype : mdmtyp, 0) < 0) {
	erp = errmsg;
	sprintf(erp, "Sorry, can't open %s", ttname);
	perror(errmsg);
	return (0);
    }

    /* Condition console terminal and communication line */
    if (ttvt(speed, flow) < 0) {
	printf("Sorry, Can't condition communication line\n");
	return (0);
    }

    if (!network && carrier != CAR_OFF && (ttgmdm() & BM_DCD) == 0) {
        printf("No carrier detected.\n");
	return (0);
    }

    outshift = inshift = 0;		/* Initial shift state. */

    tcs = TC_1LATIN;
    langsv = language;
    language = L_USASCII;

    if (tcsr == tcsl) {			/* Remote and local sets the same? */
	sxo = rxo = NULL;		/* If so, no translation. */
	sxi = rxi = NULL;
    } else {				/* Otherwise, set up */
	sxo = xls[tcs][tcsl];		/* translation function */
	rxo = xlr[tcs][tcsr];		/* pointers for output functions */
	sxi = xls[tcs][tcsr];		/* and for input functions. */
	rxi = xlr[tcs][tcsl];
    }

    iskipesc = oskipesc = (tcs != TC_TRANSP) &&	/* Not transparent */
      (fcsinfo[tcsl].size == 128 || fcsinfo[tcsr].size == 128) && /* 7 bits */
	(fcsinfo[tcsl].code != FC_USASCII);
    inesc = outesc = ES_NORMAL;		/* Initial state of recognizer */

    checkscreenmode();
    setcursormode();
    VioGetCurPos(&y, &x, 0);
    wherex = x + 1;
    wherey = y + 1;
    savescreen(&commandscreen);
    colorcmd = commandscreen.scrncpy[(x + y * xsize) * 2 + 1];
    scrninit();
    restorescreen(&vt100screen);
    conraw();
    connoi();
    ipadl25();

    /* Create a thread to read the comms line and write to the screen */

    active = 1;			/* So thread 2 doesn't end at once */
    quitnow = 0;
    hangnow = 0;
    termessage[0] = 0;
#ifdef __32BIT__
    DosOpenEventSem(NULL, &threadsem);
    DosResetEventSem(threadsem, &semcount);
    if (DosCreateThread(&threadid, (PFNTHREAD)rdserwrtscr, 0, 0, THRDSTKSIZ)) {
	printf("Sorry, can't create thread\n");
	return (0);
    }
    DosWaitEventSem(threadsem, -1L);
#else
    DosSemSet(&threadsem);	/* Thread 2 will clear this when it starts */
    if (DosCreateThread(rdserwrtscr, &threadid,
			(PBYTE)(stack + THRDSTKSIZ))) {
	printf("Sorry, can't create thread\n");
	return (0);
    }
    DosSemWait(&threadsem, -1L);/* Wait for thread to start */
    DosSemSet(&threadsem);	/* Thread 2 will clear this on termination */
#endif

    while (active) {		/* Read the keyboard and write to comms line */
	c = congks(2);
	if (!active)
	  break;
	if (c == -1) {
	  active = network || (carrier == CAR_OFF) || (ttgmdm() & BM_DCD) != 0;
	  if (!active)
	    strcpy(termessage, "Carrier lost.\n");
	  continue;
	}
	cm = keymap[c];
	DosEnterCritSec();	/* Protect the run time library */
	if (cm == escape) {	/* Look for escape char */
	    esc25(0);
	    c = keymap[congks(0)];	/* Got esc, get its arg */
	    if (c == '?') {
	        esc25(1);
		c = helpconnect(0);
	    }
	    doesc(c);		/* And process it */
	    ipadl25();
	} else {		/* Ordinary character */
	    if (!keylock)
		vt100key(c);
	}
	DosExitCritSec();	/* Let other guy use run time library */
    }				/* while (active) */

#ifdef __32BIT__
    DosWaitThread(&threadid, 0);
    DosCloseEventSem(threadsem);
#else
    DosSemWait(&threadsem, -1L);/* Wait for other thread to terminate */
#endif

    language = langsv;		/* Restore language */

    concooked();
    savescreen(&vt100screen);
    restorescreen(&commandscreen);
    restorecursormode();
    if (termessage[0]!='\0') printf(termessage);
    if (hangnow) {
#ifndef NODIAL
      if (mdmhup() < 1)
#endif /* NODIAL */
	tthang();
    }
    if (quitnow) doexit(GOOD_EXIT,0);
    return (1);
}
