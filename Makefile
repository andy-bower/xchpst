# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk>

VERSION ?= 0.1.0~1
name := xchpst

prefix ?= /usr/local
#CFLAGS ?= -g -O2
CFLAGS ?= -g
CFLAGS += -MMD -MP \
	  -Wall -Wimplicit-fallthrough -Werror \
	  -std=c23 \
	  -D_GNU_SOURCE \
	  -DPROG_NAME=$(name) \
	  -DPROG_VERSION=$(VERSION) \
	  -DINST_PREFIX=$(prefix)
LDLIBS =
INSTALL = install
LN = ln -f
DEP = $(wildcard *.d)
prefix ?= /usr

OBJS = xchpst.o options.o

.PHONY: all clean install

all: $(name) chpst

-include $(DEP)

$(name): $(OBJS)

chpst: $(name)
	$(LN) $< $@

clean:
	$(RM) $(name) $(OBJS) $(DEP)

install:
	$(INSTALL) -m 755 -D -t $(DESTDIR)$(prefix)/bin            $(name)
#	$(INSTALL) -m 644 -D -t $(DESTDIR)$(prefix)/share/man/man1 $(name).1
