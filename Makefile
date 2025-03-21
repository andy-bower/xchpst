# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: (c) Copyright 2024,2025 Andrew Bower <andrew@bower.uk>

VERSION ?= 0.6.0
name := xchpst

prefix ?= /usr/local
#CFLAGS ?= -g
CFLAGS ?= -g -O2
CFLAGS += -MMD -MP \
	  -Wall -Wextra -Werror \
	  -std=c2x \
	  -D_GNU_SOURCE \
	  -DPROG_NAME=$(name) \
	  -DPROG_VERSION=$(VERSION) \
	  -DINST_PREFIX=$(prefix)
LDLIBS = -lcap
INSTALL = install
LN = ln -f
DEP = $(wildcard *.d)
prefix ?= /usr

OBJS = xchpst.o options.o usrgrp.o caps.o env.o join.o rootfs.o mount.o \
  precreate.o
ALT_EXES = chpst softlimit envdir pgrphack setuidgid envuidgid setlock

.PHONY: all clean install

all: $(name) $(ALT_EXES)

-include $(DEP)

$(name): $(OBJS)

$(ALT_EXES): $(name)
	$(LN) $< $@

clean:
	$(RM) $(name) $(OBJS) $(DEP) $(ALT_EXES) chpst.o chpst.compat

install:
	$(INSTALL) -m 755 -D -t $(DESTDIR)$(prefix)/bin               $(name)
#	$(INSTALL) -m 755 -D -t $(DESTDIR)$(prefix)/bin               $(ALT_EXES)
	$(INSTALL) -m 644 -D -t $(DESTDIR)$(prefix)/share/man/man8    $(name).8
	$(INSTALL) -m 644 -D -t $(DESTDIR)$(prefix)/share/doc/$(name) extra/xchpst-funcs.sh
