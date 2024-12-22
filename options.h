/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>

#include "xchpst.h"
#include "usrgrp.h"

enum compat_level {
  COMPAT_CHPST     = 01,
  COMPAT_XCHPST    = 02,
  COMPAT_SOFTLIMIT = 04,
};

enum verbosity {
  LOG_LEVEL_NONE = 0,
  LOG_LEVEL_VERBOSE = 1,
};

constexpr enum compat_level C_X = COMPAT_XCHPST;
constexpr enum compat_level C_R = COMPAT_XCHPST | COMPAT_CHPST;
constexpr enum compat_level C_0 = COMPAT_CHPST;

constexpr enum compat_level C_S = COMPAT_SOFTLIMIT;
constexpr enum compat_level C_RS = C_R | C_S;

struct app {
  enum compat_level compat_level;
  const char *name;
  bool long_opts;
};

enum opt:int {
  OPT_VERSION = 0x1000,
  OPT_VERBOSE,
  OPT_HELP,
  OPT_SETUIDGID,
  OPT_ENVUIDGID,
  OPT_ARGV0,
  OPT_MOUNT_NS,
  OPT_NET_NS,
  OPT_PID_NS,
  OPT_USER_NS,
  OPT_NET_ADOPT,
  OPT_PRIVATE_RUN,
  OPT_PRIVATE_TMP,
  OPT_RO_SYS,
  OPT_LEGACY,
  OPT_LIMIT_MEM,
  OPT_RLIMIT_DATA,
  OPT_RLIMIT_AS,
  OPT_RLIMIT_STACK,
  OPT_RLIMIT_MEMLOCK,
  OPT_RLIMIT_RSS,
  OPT_RLIMIT_NOFILE,
  OPT_RLIMIT_NPROC,
  OPT_RLIMIT_FSIZE,
  OPT_RLIMIT_CORE,
  OPT_RLIMIT_CPU,
};

#define NAME_STR STRINGIFY(PROG_NAME)

struct option_info {
  enum compat_level compat_level;
  enum opt option;
  char short_name;
  char *long_name;
  int has_arg;
  const char *help;
  const char *arg;
};

struct limit {
  struct rlimit limits;
  bool soft_specified:1;
  bool hard_specified:1;
};

struct options {
  const struct app *app;
  bool error;
  bool version;
  bool help;
  int verbosity;

  /* The interesting (x)chpsty stuff here */
  char *argv0;
  int new_ns;
  bool private_run;
  bool private_tmp;
  bool ro_sys;
  bool setuidgid;
  const char *net_adopt;
  struct users_groups users_groups;
  struct limit rlimit_data;
  struct limit rlimit_stack;
  struct limit rlimit_as;
  struct limit rlimit_memlock;
  struct limit rlimit_rss;
  struct limit rlimit_nofile;
  struct limit rlimit_nproc;
  struct limit rlimit_fsize;
  struct limit rlimit_core;
  struct limit rlimit_cpu;
};

extern struct options opt;

static inline bool is_verbose(void) {
  return opt.verbosity >= LOG_LEVEL_VERBOSE;
}

void options_init(void);
void options_print(FILE *out);
int options_parse(int argc, char *argv[]);
void options_free(void);

#endif
