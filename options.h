/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024,2025 Andrew Bower <andrew@bower.uk> */

#ifndef _OPTIONS_H
#define _OPTIONS_H

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/capability.h>
#include <sys/resource.h>
#include <linux/ioprio.h>

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

enum opt /* C23: :int */ {
  OPT_BASE = 0x1000,
  OPT_SETUIDGID = OPT_BASE,
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
  OPT_RLIMIT_MSGQUEUE,
  OPT_RLIMIT_NICE,
  OPT_RLIMIT_RTPRIO,
  OPT_RLIMIT_RTTIME,
  OPT_RLIMIT_SIGPENDING,
  OPT_CLOSE_STDIN,
  OPT_CLOSE_STDOUT = OPT_CLOSE_STDIN + STDOUT_FILENO - STDIN_FILENO,
  OPT_CLOSE_STDERR = OPT_CLOSE_STDIN + STDERR_FILENO - STDIN_FILENO,
  OPT_VERBOSE,
  OPT_VERSION,
  OPT_PGRPHACK,
  OPT_LEGACY,
  OPT_HELP,
  OPT_FILE,
  OPT_MOUNT_NS,
  OPT_NET_NS,
  OPT_USER_NS,
  OPT_PID_NS,
  OPT_UTS_NS,
  OPT_NET_ADOPT,
  OPT_PRIVATE_RUN,
  OPT_PRIVATE_TMP,
  OPT_PROTECT_HOME,
  OPT_RO_SYS,
  OPT_CAPBS_KEEP,
  OPT_CAPBS_DROP,
  OPT_CAPS_KEEP,
  OPT_CAPS_DROP,
  OPT_FORK_JOIN,
  OPT_NEW_ROOT,
  OPT_NO_NEW_PRIVS,
  OPT_SCHEDULER,
  OPT_CPUS,
  OPT_IO_NICE,
  OPT_UMASK,
  OPT_APP,
  OPT_RUN_DIR,
  OPT_STATE_DIR,
  OPT_CACHE_DIR,
  OPT_LOG_DIR,
  OPT_LOGIN,
  OPT_OOM,

  /* Keep at end */
  OPT_EXIT,
  _OPT_LAST = OPT_EXIT
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

struct options_file {
  struct options_file *next;
  char content[];
};

struct options {
  /* Linked list of options files */
  struct options_file *opt_files;

  /* Bitfield of specified options */
  uint32_t specified[(_OPT_LAST - OPT_BASE + 32) / 32];

  /* Which type of application we are launched as */
  const struct app *app;

  /* Meta stuff */
  bool error;
  bool version;
  bool help;
  bool exit;
  int retcode;
  int verbosity;

  /* The interesting (x)chpsty stuff here */
  char *argv0;
  char *app_name;
  int new_ns;
  int niceness;
  bool lock_wait;
  bool lock_nowait_override;
  bool lock_quiet;
  int sched_policy;
  int ionice_prio;
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
  struct limit rlimit_msgqueue;
  struct limit rlimit_nice;
  struct limit rlimit_rtprio;
  struct limit rlimit_rttime;
  struct limit rlimit_sigpending;
  unsigned close_fds;
  enum cap_op cap_bounds_op;
  cap_bits_t cap_bounds;
  cap_bits_t caps_op;
  cap_bits_t caps;
  unsigned int umask;
  long oom_adjust;

  struct {
    cpu_set_t *mask;
    int size;
  } cpu_affinity;
};

extern struct options opt;

static inline bool is_verbose(void) {
  return opt.verbosity >= LOG_LEVEL_VERBOSE;
}

static inline bool is_debug(void) {
  return opt.verbosity >= LOG_LEVEL_DEBUG;
}

static inline bool set(enum opt option) {
  int index = option - OPT_BASE;
  return opt.specified[index / 32] & (1 << (index & 31));
}

static inline void enable(enum opt option) {
  int index = option - OPT_BASE;
  opt.specified[index / 32] |= (1 << (index & 31));
}

bool options_init(void);
void options_print(FILE *out);
void options_print_positional(FILE *out);
void options_explain_positional(FILE *out);
int options_parse(int argc, char *argv[]);
void options_free(void);

#endif
