# CKOKER.MAK, Version 3.00
#
# -- Makefile to build C-Kermit 5A for OS/2 --
#
# The result is a runnable program called CKOKER.EXE in the current
# directory.  After satisfactory testing, you can rename CKOKER.EXE to
# CKERMIT.EXE and install it in the desired directory.
#
# The 32-bit versions run only on OS/2 2.00 or higher.
#
#---------- Compiler targets:

unknown:
	@echo Please specify target: "msc ibmc gcc"

# Microsoft C 6.00 (16-bit):
#
# To build: "nmake -f ckoker.mak msc"
#
# When (if) the LINK step doesn't work: (a) make sure the MSC directories are
# ahead of the OS/2 directories in your PATH; (b) if LINK complains about not
# finding LLIBCEP.LIB (because NMAKE inserted "/NOD:LLIBCE.LIB LLIBCEP.LIB"
# into the LINK options), just tell it to use LLIBCE.LIB.
# 
# Warning: If the compiler complains about "Unrecoverable heap overflow", add
# the -B3C3L option after -W2, within the quotes.  This requires C3L.EXE on
# disk.  (This problem has been observed with MSC 6.00A, but not 6.00.)
msc:
	$(MAKE) -f ckoker.mak all \
	CC="cl -nologo -W2" OUT="-o" O=".obj" OPT="-Ot -Gs" DEBUG="" \
	CFLAGS="-AL -Au -FPc -J -Zap -G2 -Gt8 -UMSDOS -DNETCONN -DDECNET" \
	LDFLAGS="-F 9A00 -Lp -link /noe /packc /packd /exepack"

# and with debug information
mscd:
	$(MAKE) -f ckoker.mak all \
	CC="cl -nologo -W2" OUT="-o" O=".obj" OPT="-Od" DEBUG="-Zi" \
	CFLAGS="-AL -Au -FPc -J -Zap -G2 -Gt8 -UMSDOS -DNETCONN -DDECNET" \
	LDFLAGS="-F 9A00 -Lp -link /noe /packc /packd /exep"

# IBM C Set/2 (32-bit) with static linking -- no DLL's required.
# Which is good, because otherwise users would need to have the IBM
# OS/2 development system C libraries on their PCs.
#
ibmc:
	$(MAKE) -f ckoker.mak all \
	CC="icc -q -O -Gd- -Gm" OUT="-Fe" O=".obj" OPT="-O -Gs" DEBUG="" \
	CFLAGS="-Sm -Sp1 -D__STDC__ -DNETCONN -DDECNET" LDFLAGS="-B/ST:0x10000"
	msgbind ckoker.msb

# and with debug information.
ibmcd:
	$(MAKE) -f ckoker.mak all \
	CC="icc -q -Gm" OUT="-Fe" O=".obj" OPT="" DEBUG="-D__DEBUG -Ti" \
	CFLAGS="-Sm -Sp1 -D__STDC__ -DNETCONN -DDECNET" LDFLAGS="-B/ST:0x10000"

# emx 0.8e + gcc 2.2.2 (32-bit)
#
gcc:
	$(MAKE) -f ckoker.mak all \
	CC="gcc -Zomf -Zmt" OUT="-o" O=".obj" OPT="-O" DEBUG="" \
	CFLAGS="" LDFLAGS="-los2 -l16bit"

# and with debug information.
gccd:
	$(MAKE) -f ckoker.mak all \
	CC="gcc" OUT="-o" O=".o" OPT="" DEBUG="-g" \
	CFLAGS="" LDFLAGS="-los2 -l16bit"

#---------- Macros:

DEFINES = -DOS2 -DDYNAMIC

#---------- Inference rules:

.c$(O):
	$(CC) $(CFLAGS) $(OPT) $(DEBUG) $(DEFINES) -c $*.c

#---------- Targets:

OBJS =	ckcmai$(O) ckcfns$(O) ckcfn2$(O) ckcfn3$(O) ckcpro$(O) \
        ckuxla$(O) ckucmd$(O) ckuusr$(O) ckuus2$(O) ckuus3$(O) \
        ckuus4$(O) ckuus5$(O) ckuus6$(O) ckuus7$(O) ckuusx$(O) \
	ckuusy$(O) ckudia$(O) ckuscr$(O) ckufio$(O) ckocon$(O) \
        ckotio$(O) ckonet$(O)
DEF =   ckoker.def

all: ckoker.exe

ckoker.exe: $(OBJS) $(DEF)
	$(CC) $(CFLAGS) $(DEBUG) $(OBJS) $(DEF) $(OUT) $@ $(LDFLAGS)

ckwart.exe: ckwart$(O) $(DEF)
	$(CC) $(CFLAGS) ckwart$(O) $(DEF) $(OUT) $@ $(LDFLAGS)

#---------- Dependencies:

ckcmai$(O):	ckcmai.c ckcker.h ckcdeb.h ckcasc.h ckcsym.h ckcnet.h
ckcfns$(O):	ckcfns.c ckcker.h ckcdeb.h ckcasc.h ckcsym.h ckcxla.h ckuxla.h
ckcfn2$(O):	ckcfn2.c ckcker.h ckcdeb.h ckcasc.h ckcsym.h ckcxla.h ckuxla.h
ckcfn3$(O):	ckcfn3.c ckcker.h ckcdeb.h ckcasc.h ckcsym.h ckcxla.h ckuxla.h
ckcpro$(O):	ckcpro.c ckcker.h ckcdeb.h ckcasc.h
ckuxla$(O):	ckuxla.c ckcker.h ckcdeb.h ckcxla.h ckuxla.h
ckucmd$(O):	ckucmd.c ckcker.h ckcdeb.h ckcasc.h ckucmd.h
ckuusr$(O):	ckuusr.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h ckucmd.h \
		  ckcxla.h ckuxla.h ckcnet.h
ckuus2$(O):	ckuus2.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h ckucmd.h \
		  ckcxla.h ckuxla.h
ckuus3$(O):	ckuus3.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h ckucmd.h \
ckcxla.h ckuxla.h ckcnet.h
ckuus4$(O):	ckuus4.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h ckucmd.h \
		  ckcxla.h ckuxla.h ckcnet.h ckuver.h
ckuus5$(O):	ckuus5.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h ckucmd.h
ckuus6$(O):	ckuus6.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h ckucmd.h
ckuus7$(O):	ckuus7.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h ckucmd.h \
		  ckcxla.h ckuxla.h ckcnet.h
ckuusx$(O):	ckuusx.c ckcker.h ckcdeb.h ckcasc.h ckuusr.h
ckuusy$(O):	ckuusy.c ckcker.h ckcdeb.h ckcasc.h
ckudia$(O):	ckudia.c ckcker.h ckcdeb.h ckcasc.h ckucmd.h
ckuscr$(O):	ckuscr.c ckcker.h ckcdeb.h ckcasc.h
ckufio$(O):	ckufio.c ckcker.h ckcdeb.h ckuver.h ckodir.h ckoker.h
ckocon$(O):	ckocon.c ckcker.h ckcdeb.h ckcasc.h ckoker.h
ckotio$(O):	ckotio.c ckcker.h ckcdeb.h ckuver.h ckodir.h ckoker.h
ckonet$(O):	ckonet.c ckcker.h ckcdeb.h ckcnet.h ckoker.h

ckcpro.c:	ckcpro.w ckwart.exe
		ckwart ckcpro.w ckcpro.c
