/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "options.h"

struct options opt;

const struct option_info options_info[] = {
  { C_R, OPT_SETUIDGID,   'u',  NULL,       required_argument,
    "set uid, gid and supplementary groups", "[:]USER[:GROUP]*", },
  { C_R, OPT_ENVUIDGID,   'U',  NULL,       required_argument,
    "set UID and GID vars", "[:]USER[:GROUP]", },
  { C_R, OPT_ARGV0,       'b',  NULL,       required_argument,
    "launch program with ARGV0 as the argv[0]", "ARGV0" },
  { C_R, OPT_ENVDIR,      'e',  NULL,       required_argument,
    "populate environment from directory", "DIR" },
  { C_R, OPT_CHROOT,      '/',  NULL,       required_argument, "change root directory", "DIR" },
  { C_R, OPT_CHDIR,       'C',  NULL,       required_argument, "change directory", "DIR" },
  { C_R, OPT_NICE,        'n',  NULL,       required_argument, "adjust niceness", "INC" },
  { C_R, OPT_LOCK_WAIT,   'l',  NULL,       required_argument, "wait for lock", "FILE" },
  { C_R, OPT_LOCK,        'L',  NULL,       required_argument, "obtain lock; fail fast", "FILE" },
  { C_L, OPT_LOCKOPT_WAIT,'N',  NULL,       no_argument,       "wait for lock (default)" },
  { C_L, OPT_LOCKOPT_TRY, 'n',  NULL,       no_argument,       "don't wait for lock" },
  { C_L, OPT_LOCKOPT_NOISY,'X', NULL,       no_argument,       "fail noisily (default)" },
  { C_L, OPT_LOCKOPT_QUIET,'x', NULL,       no_argument,       "fail silently" },
  { C_RS,OPT_LIMIT_MEM,   'm',  NULL,       required_argument,
    "set soft DATA, STACK, MEMLOCK and AS limits", "BYTES" },
  { C_RS,OPT_RLIMIT_DATA, 'd',  NULL,       required_argument, "set soft RLIMIT_DATA", "BYTES" },
  { C_XS,OPT_RLIMIT_STACK,'s',  NULL,       required_argument, "set soft RLIMIT_STACK", "BYTES" },
  { C_S, OPT_RLIMIT_MEMLOCK,'l', NULL,      required_argument, "set soft RLIMIT_MEMLOCK", "BYTES" },
  { C_X, OPT_RLIMIT_MEMLOCK,'\0',"memlock", required_argument, "set soft RLIMIT_MEMLOCK", "BYTES" },
  { C_XS,OPT_RLIMIT_AS,   'a',  NULL,       required_argument, "set soft RLIMIT_AS", "BYTES" },
  { C_RS,OPT_RLIMIT_NOFILE,'o', NULL,       required_argument, "set soft RLIMIT_NOFILE", "FILES" },
  { C_RS,OPT_RLIMIT_NPROC,'p',  NULL,       required_argument, "set soft RLIMIT_NPROC", "PROCS" },
  { C_RS,OPT_RLIMIT_FSIZE,'f',  NULL,       required_argument, "set soft RLIMIT_FSIZE", "BYTES" },
  { C_RS,OPT_RLIMIT_CORE, 'c',  NULL,       required_argument, "set soft RLIMIT_CORE", "BYTES" },
  { C_XS,OPT_RLIMIT_RSS,  'r',  NULL,       required_argument, "set soft RLIMIT_RSS", "BYTES" },
  { C_RS,OPT_RLIMIT_CPU,  't',  NULL,       required_argument, "set soft RLIMIT_CPU", "SECONDS" },
  { C_ALL,OPT_VERBOSE,    'v',  "verbose",  no_argument,       "be verbose" },
  { C_R, OPT_VERSION,     'V',  "version",  no_argument,       "show " NAME_STR " version" },
  { C_R, OPT_PGRPHACK,    'P',  NULL,       no_argument,       "run in new process group" },
  { C_R, OPT_CLOSE_STDIN, '0',  NULL,       no_argument,       "close stdin" },
  { C_R, OPT_CLOSE_STDOUT,'1',  NULL,       no_argument,       "close stdout" },
  { C_R, OPT_CLOSE_STDERR,'2',  NULL,       no_argument,       "close stderr" },
  { C_X, OPT_LEGACY,      '@',  NULL,       no_argument,
    "restricts following options to chpst(8) ones" },
  { C_X, OPT_HELP,        'h',  "help",     no_argument,       "show help" },
  { C_X, OPT_EXIT,        '\0', "exit",     optional_argument,
    "exit (with optional RETCODE)", "RETCODE" },
  { C_X, OPT_MOUNT_NS,    '\0', "mount-ns", no_argument,       "create mount namespace" },
  { C_X, OPT_NET_NS,      '\0', "net-ns",   no_argument,       "create net namespace" },
  { C_X, OPT_USER_NS,     '\0', "user-ns",  no_argument,       "create user namespace" },
  { C_X, OPT_PID_NS,      '\0', "pid-ns",   no_argument,       "create pid namespace" },
  { C_X, OPT_NET_ADOPT,   '\0', "adopt-net",required_argument,
    "adopt net namespace", "NS-PATH" },
  { C_X, OPT_PRIVATE_RUN, '\0', "private-run",no_argument,     "create private run dir" },
  { C_X, OPT_PRIVATE_TMP, '\0', "private-tmp",no_argument,     "create private tmp dir" },
  { C_X, OPT_PROTECT_HOME,'\0', "protect-home",no_argument,    "protect home directories" },
  { C_X, OPT_RO_SYS,      '\0', "ro-sys",     no_argument,     "create read only system" },
  { C_X, OPT_CAPBS_KEEP,  '\0', "cap-bs-keep",required_argument,
    "restrict capabilities bounding set", "CAP[,...]" },
  { C_X, OPT_CAPBS_DROP,  '\0', "cap-bs-drop",required_argument,
    "drop from capabilities bounding set", "CAP[,...]" },
  { C_X, OPT_CAPS_KEEP,   '\0', "caps-keep", required_argument,
    "keep (only) these capabilities", "CAP[,...]" },
  { C_X, OPT_CAPS_DROP,   '\0', "caps-drop", required_argument,
    "drop these capabilities", "CAP[,...]" },
  { C_X, OPT_FORK_JOIN,   '\0', "fork-join", no_argument,      "fork and wait for process" },
  { C_X, OPT_NEW_ROOT,    '\0', "new-root",  no_argument,      "create a new root fs" },
  { C_X, OPT_NO_NEW_PRIVS,'\0', "no-new-privs", no_argument,   "no new privileges" },
  { C_X, OPT_SCHEDULER,   '\0', "scheduler", required_argument,"set scheduler policy", "POLICY" },
  { C_X, OPT_CPUS,        '\0', "cpus",      required_argument,"set CPU affinity", "AFFINITY" },
};
#define max_options ((ssize_t) ((sizeof options_info / sizeof *options_info)))

static struct option options[max_options + 1];
static char optstr[max_options * 2];

void options_print(FILE *out) {
  const struct option_info *optdef;
  int i = 0;

  for (optdef = options_info; optdef - options_info < max_options; optdef++) {
    if (optdef->compat_level & opt.app->compat_level) {
      if (i++ == 0)
        fprintf(out, "\n OPTIONS\n");
      fprintf(out, "  %c%c%s%s%s%c%-*s %s\n",
             optdef->short_name ? '-' : ' ',
             optdef->short_name ? optdef->short_name : ' ',
             opt.app->long_opts && optdef->long_name ? (optdef->short_name ? "," : " ") : "",
             opt.app->long_opts && optdef->long_name ? " --" : "",
             opt.app->long_opts && optdef->long_name ? optdef->long_name : "",
             optdef->has_arg == optional_argument ? '=' : ' ',
             opt.app->long_opts && optdef->long_name ? 20 - (int) strlen(optdef->long_name) : 24,
             optdef->arg ? optdef->arg : " ",
             optdef->help ? optdef->help : "");
    }
  }
}

void options_print_positional(FILE *out) {
  int i;

  for (i = 0; i < opt.app->takes_positional_opts; i++) {
    const struct option_info *optdef;
    enum opt option = opt.app->positional_opts[i];

    /* Look up option definition */
    for (optdef = options_info;
         optdef - options_info < max_options && option != optdef->option;
         optdef++);
    if (optdef->has_arg != no_argument)
      fprintf(out, " %s", optdef->arg);
  }
}

void options_explain_positional(FILE *out) {
  int i;

  if (opt.app->takes_positional_opts)
    fprintf(out, "\n");

  for (i = 0; i < opt.app->takes_positional_opts; i++) {
    const struct option_info *optdef;
    enum opt option = opt.app->positional_opts[i];

    /* Look up option definition */
    for (optdef = options_info;
         optdef - options_info < max_options && option != optdef->option;
         optdef++);
    fprintf(out, " %-10s %s\n",
            optdef->has_arg != no_argument ? optdef->arg : "",
            optdef->help);
  }
}

bool options_init(void) {
  const struct option_info *optdef;
  size_t optstr_sz = 1;
  size_t optstr_len;
  int i = 0;

  for (optdef = options_info; optdef - options_info < max_options; optdef++) {
    if (optdef->compat_level & opt.app->compat_level) {

      /* Populate long option list */
      if (optdef->long_name) {
        struct option option = {
          .name = optdef->long_name,
          .has_arg = optdef->has_arg,
          .flag = 0,
          .val = optdef->option
        };
        options[i++] = option;
      }

      /* Size up short option string */
      if (optdef->short_name) {
        switch (optdef->has_arg) {
        case optional_argument:
          optstr_sz++;
          [[fallthrough]];
        case required_argument:
          optstr_sz++;
          [[fallthrough]];
        default:
          optstr_sz++;
        }
      }
    }
  }

  /* Populate short option string */
  if (optstr_sz + 1 > sizeof optstr) {
    fprintf(stderr, "short option string too long\n");
    return false;
  }
  optstr_len = 0;
  optstr[optstr_len++] = '+'; /* Stop accepting options after prog name */
  for (optdef = options_info; optdef - options_info < max_options; optdef++) {
    if (optdef->compat_level & opt.app->compat_level && optdef->short_name) {
      optstr[optstr_len++] = optdef->short_name;
      switch (optdef->has_arg) {
      case optional_argument:
        optstr[optstr_len++] = ':';
        [[fallthrough]];
      case required_argument:
        optstr[optstr_len++] = ':';
      default:
      }
    }
  }
  assert(optstr_len == optstr_sz);
  optstr[optstr_len++] = '\0';
  return true;
}

bool parse_limit(rlim_t *lim, const char *arg) {
  int toks = 0;
  long long val;

  if (!strcmp(arg, "unlimited")) {
    *lim = RLIM_INFINITY;
    toks = 1;
  } else {
    toks = sscanf(arg, "%lld", &val);
    if (toks == 1) {
      if (val == -1LL) {
        *lim = RLIM_INFINITY;
      } else if (val >= 0LL) {
        *lim = val;
      } else {
        fprintf(stderr, "invalid limit: %lld\n", val);
        toks = 0;
      }
    }
  }
  return toks == 1 ? true : false;
}

bool parse_caps(cap_bits_t *caps, char *names) {
  cap_bits_t set = 0;
  cap_value_t val;
  cap_value_t max_cap = cap_max_bits();
  bool good = true;
  char *tok;
  int rc;

  if (max_cap < 0) {
    /* Record that we don't have capabilities but don't fail option */
    runtime.ok.caps = false;
    return true;
  }

  assert(8 * sizeof set >= (unsigned) max_cap);

  while ((tok = strsep(&names, ","))) {
    rc = cap_from_name(tok, &val);
    if (rc != 0) {
      fprintf(stderr, "cannot interpret capability \"%s\"\n", tok);
      good = false;
    }
    set |= (1 << val);
  }
  *caps = set;
  return good;
}

bool parse_cpu_range(const char *str, int range[3], int *max) {
  int field = 0;
  int val = 0;
  char c = -1;

  while (field < 3 && c) {
    if (isdigit(c = *str++)) {
      val = val * 10 + c - '0';
    } else if ((c == '-' && field == 0) ||
               (c == ':' && field == 1) ||
               c == '\0') {
      range[field++] = val;
      val = 0;
    } else {
      return false;
    }
  }

  /* Default stride is 1 */
  if (field < 3)
    range[2] = 1;

  /* Singleton */
  if (field < 2)
    range[1] = range[0];

  /* No CPUs listed */
  if (field < 1)
    return false;

  if (max != NULL && range[1] > *max)
    *max = range[1];

  return true;
}

void parse_cpus(char *spec) {
  char *rest = spec;
  char *tok;
  char *end = spec;
  int range[3];
  int cpu;
  int max = 0;

  while((tok = strsep(&rest, ","))) {
    if (!parse_cpu_range(tok, range, &max))
      goto fail;
    end = tok;
  }
  opt.cpu_affinity.size = CPU_ALLOC_SIZE(max++);
  opt.cpu_affinity.mask = CPU_ALLOC(max);
  if (opt.cpu_affinity.mask == NULL) {
    perror("CPU_ALLOC");
    goto fail;
  }
  CPU_ZERO_S(opt.cpu_affinity.size, opt.cpu_affinity.mask);

  for (tok = spec; tok <= end; tok += strlen(tok) + 1) {
    parse_cpu_range(tok, range, NULL);
    for (cpu = range[0]; cpu <= range[1]; cpu += range[2])
      CPU_SET_S(cpu, opt.cpu_affinity.size, opt.cpu_affinity.mask);
  }
  return;

fail:
  opt.error = true;
  fprintf(stderr, "error in CPU list (at %s)\n", tok);
}

int sched_policy_from_name(const char *name) {
  if (!strcmp(name, "batch"))
    return SCHED_BATCH;
  else if (!strcmp(name, "idle"))
    return SCHED_IDLE;
  else if (strcmp(name, "other"))
    fprintf(stderr, "ignoring unknown scheduler policy: %s\n", name);

  return SCHED_OTHER;
}

static void handle_option(enum compat_level *compat,
                          const struct option_info *optdef) {
  switch (optdef->option) {
  case OPT_LEGACY:
    *compat = COMPAT_CHPST;
    break;
  case OPT_VERSION:
    opt.version = true;
    break;
  case OPT_HELP:
    opt.help = true;
    break;
  case OPT_EXIT:
    opt.exit = true;
    opt.retcode = optarg ? atoi(optarg) : CHPST_ERROR_EXIT;
    break;
  case OPT_VERBOSE:
    opt.verbosity++;
    break;
  case OPT_ARGV0:
    opt.argv0 = optarg;
    break;
  case OPT_ENVDIR:
    opt.env_dir = optarg;
    break;
  case OPT_CHROOT:
    opt.chroot = optarg;
    break;
  case OPT_CHDIR:
    opt.chdir = optarg;
    break;
  case OPT_NICE:
    opt.renice = true;
    opt.niceness = atoi(optarg);
    break;
  case OPT_LOCK_WAIT:
    opt.lock_wait = true;
    [[fallthrough]];
  case OPT_LOCK:
    opt.lock_file = optarg;
    break;
  case OPT_LOCKOPT_WAIT:
    opt.lock_wait = true;
    break;
  case OPT_LOCKOPT_TRY:
    opt.lock_wait = false;
    opt.lock_nowait_override = true;
    break;
  case OPT_LOCKOPT_NOISY:
    opt.lock_quiet = false;
    break;
  case OPT_LOCKOPT_QUIET:
    opt.lock_quiet = true;
    break;
  case OPT_MOUNT_NS:
    opt.new_ns |= CLONE_NEWNS;
    break;
  case OPT_NET_NS:
    opt.new_ns |= CLONE_NEWNET;
    break;
  case OPT_PID_NS:
    opt.new_ns |= CLONE_NEWPID;
    break;
  case OPT_USER_NS:
    opt.new_ns |= CLONE_NEWUSER;
    break;
  case OPT_NET_ADOPT:
    opt.net_adopt = optarg;
    break;
  case OPT_PRIVATE_RUN:
    opt.private_run = true;
    break;
  case OPT_PRIVATE_TMP:
    opt.private_tmp = true;
    break;
  case OPT_PROTECT_HOME:
    opt.protect_home = true;
    break;
  case OPT_RO_SYS:
    opt.ro_sys = true;
    break;
  case OPT_CAPBS_KEEP:
    if (!parse_caps(&opt.cap_bounds, optarg))
      opt.error = true;
    opt.cap_bounds_op = CAP_OP_KEEP;
    break;
  case OPT_CAPBS_DROP:
    if (!parse_caps(&opt.cap_bounds, optarg))
      opt.error = true;
    opt.cap_bounds_op = CAP_OP_DROP;
    break;
  case OPT_CAPS_KEEP:
    if (!parse_caps(&opt.caps, optarg))
      opt.error = true;
    opt.caps_op = CAP_OP_KEEP;
    break;
  case OPT_CAPS_DROP:
    if (!parse_caps(&opt.caps, optarg))
      opt.error = true;
    opt.caps_op = CAP_OP_DROP;
    break;
  case OPT_FORK_JOIN:
    opt.fork_join = true;
    break;
  case OPT_NEW_ROOT:
    opt.new_root = true;
    break;
  case OPT_NO_NEW_PRIVS:
    opt.no_new_privs = true;
    break;
  case OPT_SCHEDULER:
    opt.scheduler = true;
    opt.sched_policy = sched_policy_from_name(optarg);
    break;
  case OPT_CPUS:
    parse_cpus(optarg);
    break;
  case OPT_SETUIDGID:
    if (usrgrp_parse(&opt.users_groups, optarg))
      opt.error = true;
    else if (usrgrp_resolve(&opt.users_groups))
      opt.error = true;
    if (opt.verbosity > 1)
      usrgrp_print(stderr, "setuidgid", &opt.users_groups);
    opt.setuidgid = true;
    break;
  case OPT_ENVUIDGID:
    if (usrgrp_parse(&opt.env_users_groups, optarg))
      opt.error = true;
    else if (usrgrp_resolve(&opt.env_users_groups))
      opt.error = true;
    if (opt.verbosity > 1)
      usrgrp_print(stderr, "envuidgid", &opt.env_users_groups);
    opt.envuidgid = true;
    break;
  case OPT_LIMIT_MEM:
    if (!(opt.rlimit_memlock.soft_specified = parse_limit(&opt.rlimit_memlock.limits.rlim_cur, optarg))) {
      opt.error = true;
    } else {
      opt.rlimit_data = opt.rlimit_memlock;
      opt.rlimit_stack = opt.rlimit_memlock;
      opt.rlimit_as = opt.rlimit_as;
    }
    break;
   case OPT_RLIMIT_DATA:
    if (!(opt.rlimit_data.soft_specified = parse_limit(&opt.rlimit_data.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_MEMLOCK:
    if (!(opt.rlimit_memlock.soft_specified = parse_limit(&opt.rlimit_memlock.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_AS:
    if (!(opt.rlimit_as.soft_specified = parse_limit(&opt.rlimit_as.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_STACK:
    if (!(opt.rlimit_stack.soft_specified = parse_limit(&opt.rlimit_stack.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_NOFILE:
    if (!(opt.rlimit_nofile.soft_specified = parse_limit(&opt.rlimit_nofile.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_RSS:
    if (!(opt.rlimit_rss.soft_specified = parse_limit(&opt.rlimit_rss.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_NPROC:
    if (!(opt.rlimit_nproc.soft_specified = parse_limit(&opt.rlimit_nproc.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_FSIZE:
   if (!(opt.rlimit_fsize.soft_specified = parse_limit(&opt.rlimit_fsize.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_CPU:
   if (!(opt.rlimit_cpu.soft_specified = parse_limit(&opt.rlimit_cpu.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_RLIMIT_CORE:
   if (!(opt.rlimit_core.soft_specified = parse_limit(&opt.rlimit_core.limits.rlim_cur, optarg)))
      opt.error = true;
    break;
  case OPT_PGRPHACK:
      opt.new_session = true;
    break;
  case OPT_CLOSE_STDIN:
  case OPT_CLOSE_STDOUT:
  case OPT_CLOSE_STDERR:
    opt.close_fds |= 1 << (optdef->option - OPT_CLOSE_STDIN + STDIN_FILENO);
    break;
  /* Avoid default case to get useful compiler warnings instead.
  default:
    fprintf(stderr, "-%c%s not yet implemented\n",
                    optdef->long_name ? '-' : optdef->short_name,
                    optdef->long_name ? optdef->long_name : "");
    opt.error = true;
   */
  }
}

int options_parse(int argc, char *argv[]) {
  const struct option_info *optdef;
  enum compat_level compat = opt.app->compat_level;
  int c;

  while ((c = opt.app->long_opts ?
                getopt_long(argc, argv, optstr, options, NULL) :
                getopt(argc, argv, optstr)) != -1) {
    bool is_short = isascii(c);
    const struct option_info *incompatible_option = NULL;

    /* Look up option definition */
    for (optdef = options_info;
         optdef - options_info < max_options;
         optdef++) {

      if (c == (is_short ? optdef->short_name : (int) optdef->option)) {
        if ((optdef->compat_level & compat) == 0) {
          /* Tentatively found an incompatible option but there may be
           * another one that is compatible later in the list. */
          incompatible_option = optdef;
        } else {
          incompatible_option = NULL;
          break;
        }
      }
    }

    /* Handle option */
    if (optdef - options_info == max_options) {
      if ((optdef = incompatible_option)) {
        char short_name[2] = { optdef->short_name, 0};
        fprintf(stderr, "illegal option (%s) at this compat level\n",
                optdef->long_name ? optdef->long_name : short_name);
      }
      opt.error = true;
    } else {
      handle_option(&compat, optdef);
    }
  }

  assert(opt.app->takes_positional_opts <= MAX_POSITIONAL_OPTS);
  for (int i = 0; i < opt.app->takes_positional_opts; i++) {
    enum opt option = opt.app->positional_opts[i];
    if (optind == argc) {
      opt.error = true;
      break;
    }

    /* Look up option definition */
    for (optdef = options_info;
         optdef - options_info < max_options && option != optdef->option;
         optdef++);
    assert(optdef);

    if (optdef->has_arg != no_argument)
      optarg = argv[optind++];
    handle_option(&compat, optdef);
  }
  return optind;
}

void options_free(void) {
  if (opt.cpu_affinity.size)
    CPU_FREE(opt.cpu_affinity.mask);
  usrgrp_free(&opt.users_groups);
  usrgrp_free(&opt.env_users_groups);
}
