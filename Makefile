#
# Labjack/Nerdjack Tools
# Copyright (c) 2007-2009
# Jim Paris <jim@jtan.com>, Zach Clifford <zacharyc@mit.edu>
#
# This is free software; you can redistribute it and/or modify it and
# it is provided under the terms of version 2 of the GNU General Public
# License as published by the Free Software Foundation; see COPYING.
#

# For Solaris, use: gmake CC=gcc LDFLAGS="-lsocket -lnsl"
# For Windows, build with "make win"

# Build options

CFLAGS += -Wall -g #-pg
LDFLAGS += #-pg
LDLIBS += -lm

PREFIX = /usr/local
MANPATH = ${PREFIX}/man/man1/
BINPATH = ${PREFIX}/bin

#WINCC = i386-mingw32-gcc
WINCC = i586-mingw32msvc-gcc
WINCFLAGS += $(CFLAGS)
WINLDFLAGS += $(LDFLAGS) -lws2_32 -liphlpapi -s

# Targets

.PHONY: default
default: lin

.PHONY: all
all: lin win

.PHONY: lin
lin: ethstream ethstream.1 ethstream.txt

.PHONY: win
win: ethstream.exe


version.h: VERSION
	echo "/* This file was automatically generated. */" >version.h
	echo "#define VERSION \"`cat VERSION` (`date +%Y-%m-%d`)\"" >>version.h

# Object files for each executable

obj-common = opt.o ue9.o ue9error.o netutil.o debug.o nerdjack.o
obj-ethstream = ethstream.o $(obj-common)

ethstream: $(obj-ethstream)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

ethstream.exe: $(obj-ethstream:.o=.obj) compat-win32.obj

# Manpages

%.1: %
	if ! help2man -N --output=$@ ./$< ; then \
		echo "No manual page available." > $@ ; fi

%.txt: %.1
	nroff -man $< | colcrt | sed s/$$/\\r/ > $@

# Install/uninstall targets for Linux

.PHONY: install
install: ethstream.1 ethstream
	mkdir -p ${BINPATH} ${MANPATH}
	install -m 0755 ethstream ${BINPATH}
	install -m 0644 ethstream.1 ${MANPATH}

.PHONY: uninstall
uninstall:
	rm -f ${BINPATH}/ethstream ${MANPATH}/ethstream.1

# Packaging

PACKAGE=labjack-`cat VERSION`
.PHONY: dist
dist: version.h
	mkdir -p ${PACKAGE}
	cp [A-Z]* *.[ch] ${PACKAGE}
	tar cvzf ${PACKAGE}.tar.gz ${PACKAGE}
	rm -r ${PACKAGE}

# Maintenance

.PHONY: clean distclean
clean distclean:
	rm -f *.o *.obj *.exe ethstream core *.d *.dobj *.1 *.txt

# Dependency tracking:

allsources = $(wildcard *.c)

-include $(allsources:.c=.d)
%.o : %.c
	$(COMPILE.c) -MP -MMD -MT '$*.o' -MF '$*.d' -o $@ $<

-include $(allsources:.c=.dobj)
%.obj : %.c
	$(WINCC) $(WINCFLAGS) -MP -MMD -MT '$*.obj' -MF '$*.dobj' -c -o $@ $<

# Win32 executable

%.exe : %.obj
	$(WINCC) -o $@ $^ $(WINLDFLAGS)
