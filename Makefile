# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk>

VERSION ?= 0.1.0~1
name := xchpst

prefix ?= /usr/local
CFLAGS ?= -g -O2
CFLAGS += -MMD -MP \
	  -Wall -Wimplicit-fallthrough -Werror \
	  -std=c23 \
	  -D_GNU_SOURCE \
	  -DPROG_NAME=$(name) \
	  -DPROG_VERSION=$(VERSION) \
	  -DINST_PREFIX=$(prefix)
LDLIBS = -lcap
INSTALL = install
LN = ln -f
DEP = $(wildcard *.d)
prefix ?= /usr

OBJS = xchpst.o options.o usrgrp.o
ALT_EXES = chpst softlimit

.PHONY: all clean install

all: $(name) $(ALT_EXES)

-include $(DEP)

$(name): $(OBJS)

$(ALT_EXES): $(name)
	$(LN) $< $@

clean:
	$(RM) $(name) $(OBJS) $(DEP)

install:
	$(INSTALL) -m 755 -D -t $(DESTDIR)$(prefix)/bin               $(name) $(ALT_EXES)
	$(INSTALL) -m 644 -D -t $(DESTDIR)$(prefix)/share/doc/$(name) xchpst-funcs.sh
#	$(INSTALL) -m 644 -D -t $(DESTDIR)$(prefix)/share/man/man1    $(name).1
