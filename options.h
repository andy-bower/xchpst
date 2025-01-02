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
#include <sys/capability.h>
#include <sys/resource.h>

#include "xchpst.h"
#include "usrgrp.h"

enum compat_level {
  COMPAT_CHPST     = 0001,
  COMPAT_XCHPST    = 0002,
  COMPAT_SOFTLIMIT = 0004,
  COMPAT_ENVDIR    = 0010,
  COMPAT_PGRPHACK  = 0020,
  COMPAT_SETUIDGID = 0040,
  COMPAT_ENVUIDGID = 0100,
  COMPAT_SETLOCK   = 0200,
};

enum verbosity {
  LOG_LEVEL_NONE = 0,
  LOG_LEVEL_VERBOSE = 1,
  LOG_LEVEL_DEBUG = 2,
};

static const/*expr*/ enum compat_level C_X = COMPAT_XCHPST;
static const/*expr*/ enum compat_level C_R = COMPAT_XCHPST | COMPAT_CHPST;
static const/*expr*/ enum compat_level C_0 = COMPAT_CHPST;

static const/*expr*/ enum compat_level C_S = COMPAT_SOFTLIMIT;
static const/*expr*/ enum compat_level C_RS = C_R | C_S;
static const/*expr*/ enum compat_level C_XS = C_X | C_S;

static const/*expr*/ enum compat_level C_L = COMPAT_SETLOCK;

static const/*expr*/ enum compat_level C_ALL = 0377;

enum opt {
  OPT_SETUIDGID = 0x1000,
  OPT_ENVUIDGID,
  OPT_ARGV0,
  OPT_ENVDIR,
  OPT_CHROOT,
  OPT_CHDIR,
  OPT_NICE,
  OPT_LOCK_WAIT,
  OPT_LOCK,
  OPT_LOCKOPT_WAIT,
  OPT_LOCKOPT_TRY,
  OPT_LOCKOPT_NOISY,
  OPT_LOCKOPT_QUIET,
  OPT_LIMIT_MEM,
  OPT_RLIMIT_DATA,
  OPT_RLIMIT_STACK,
  OPT_RLIMIT_MEMLOCK,
  OPT_RLIMIT_AS,
  OPT_RLIMIT_NOFILE,
  OPT_RLIMIT_NPROC,
  OPT_RLIMIT_FSIZE,
  OPT_RLIMIT_CORE,
  OPT_RLIMIT_RSS,
  OPT_RLIMIT_CPU,
  OPT_CLOSE_STDIN,
  OPT_CLOSE_STDOUT = OPT_CLOSE_STDIN + STDOUT_FILENO - STDIN_FILENO,
  OPT_CLOSE_STDERR = OPT_CLOSE_STDIN + STDERR_FILENO - STDIN_FILENO,
  OPT_VERBOSE,
  OPT_VERSION,
  OPT_PGRPHACK,
  OPT_LEGACY,
  OPT_HELP,
  OPT_EXIT,
  OPT_MOUNT_NS,
  OPT_NET_NS,
  OPT_USER_NS,
  OPT_PID_NS,
  OPT_NET_ADOPT,
  OPT_PRIVATE_RUN,
  OPT_PRIVATE_TMP,
  OPT_RO_SYS,
  OPT_CAPBS_KEEP,
  OPT_CAPBS_DROP,
  OPT_CAPS_KEEP,
  OPT_CAPS_DROP,
  OPT_FORK_JOIN,
  OPT_NEW_ROOT,
  OPT_NO_NEW_PRIVS,
};
static_assert(STDIN_FILENO == 0);

static const/*expr*/ int MAX_POSITIONAL_OPTS = 1;
struct app {
  enum compat_level compat_level;
  const char *name;
  bool long_opts;
  int takes_positional_opts;
  enum opt positional_opts[1 /* MAX_POSITIONAL_OPTS */];
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

typedef uint64_t cap_bits_t;

enum cap_op {
  CAP_OP_NONE = 0,
  CAP_OP_KEEP,
  CAP_OP_DROP,
};

struct options {
  const struct app *app;
  bool error;
  bool version;
  bool help;
  bool exit;
  int retcode;
  int verbosity;

  /* The interesting (x)chpsty stuff here */
  char *argv0;
  int new_ns;
  int niceness;
  bool renice;
  bool private_run;
  bool private_tmp;
  bool ro_sys;
  bool setuidgid;
  bool envuidgid;
  bool lock_wait;
  bool lock_nowait_override;
  bool lock_quiet;
  bool new_session;
  bool fork_join;
  bool new_root;
  bool no_new_privs;
  const char *lock_file;
  const char *env_dir;
  const char *chroot;
  const char *chdir;
  const char *net_adopt;
  struct users_groups users_groups;
  struct users_groups env_users_groups;
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
  unsigned close_fds;
  enum cap_op cap_bounds_op;
  cap_bits_t cap_bounds;
  cap_bits_t caps_op;
  cap_bits_t caps;
};

extern struct options opt;

static inline bool is_verbose(void) {
  return opt.verbosity >= LOG_LEVEL_VERBOSE;
}

static inline bool is_debug(void) {
  return opt.verbosity >= LOG_LEVEL_DEBUG;
}

bool options_init(void);
void options_print(FILE *out);
void options_print_positional(FILE *out);
void options_explain_positional(FILE *out);
int options_parse(int argc, char *argv[]);
void options_free(void);

#endif
