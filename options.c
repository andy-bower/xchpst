/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

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
#include <sys/types.h>

#include "options.h"

struct options opt;

const struct option_info options_info[] = {
  { C_R, OPT_VERSION,     'V',  "version",     no_argument,       "show " NAME_STR " version" },
  { C_R, OPT_VERBOSE,     'v',  "verbose",     no_argument,       "be verbose" },
  { C_X, OPT_HELP,        'h',  "help",        no_argument,       "show help" },
  { C_R, OPT_SETUIDGID,   'u',  nullptr,       required_argument,
    "set uid, gid and supplementary groups", "[:]USER[:GROUP]*", },
  { C_R, OPT_ARGV0,       'b',  nullptr,       required_argument,
    "launch program with ARGV0 as the argv[0]", "ARGV0" },
  { C_X, OPT_MOUNT_NS,    '\0', "mount-ns",    no_argument,       "create mount namespace" },
  { C_X, OPT_NET_NS,      '\0', "net-ns",      no_argument,       "create net namespace" },
  { C_X, OPT_USER_NS,     '\0', "user-ns",     no_argument,       "create user namespace" },
  { 0  , OPT_PID_NS,      '\0', "pid-ns",      no_argument,       "create pid namespace" },
  { C_X, OPT_NET_ADOPT,   '\0', "adopt-net",   required_argument,
    "adopt net namespace", "NS-PATH" },
  { C_X, OPT_PRIVATE_RUN, '\0', "private-run", no_argument,       "create private run dir" },
  { C_X, OPT_PRIVATE_TMP, '\0', "private-tmp", no_argument,       "create private tmp dir" },
  { C_X, OPT_RO_SYS,      '\0', "ro-sys",      no_argument,       "create read only system" },
  { C_X, OPT_LEGACY,      '@',  nullptr,       no_argument,       "only legacy compat options follow" },
};
constexpr size_t max_options = sizeof options_info / (sizeof *options_info);

static struct option options[max_options + 1];
static char *optstr;

void options_print(FILE *out) {
  const struct option_info *optdef;

  fprintf(out, "\n OPTIONS\n");
  for (optdef = options_info; optdef - options_info < max_options; optdef++) {
    if (optdef->compat_level & opt.app->compat_level) {
      printf("  %c%c%s%s%s %-*s %s\n",
             optdef->short_name ? '-' : ' ',
             optdef->short_name ? optdef->short_name : ' ',
             opt.app->long_opts && optdef->long_name ? (optdef->short_name ? "," : " ") : "",
             opt.app->long_opts && optdef->long_name ? " --" : "",
             opt.app->long_opts && optdef->long_name ? optdef->long_name : "",
             opt.app->long_opts && optdef->long_name ? 20 - (int) strlen(optdef->long_name) : 24,
             optdef->arg ? optdef->arg : " ",
             optdef->help ? optdef->help : "");
    }
  }
}

void options_init(void) {
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
  if (!(optstr = malloc(optstr_sz + 1))) {
    perror("malloc: creating option string");
    return;
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
}

int options_parse(int argc, char *argv[]) {
  const struct option_info *optdef;
  enum compat_level compat = opt.app->compat_level;
  int c;

  while ((c = opt.app->long_opts ?
                getopt_long(argc, argv, optstr, options, nullptr) :
                getopt(argc, argv, optstr)) != -1) {
    bool is_short = isascii(c);

    /* Look up option definition */
    for (optdef = options_info;
         optdef - options_info < max_options && c != (is_short ? optdef->short_name : optdef->option);
         optdef++);

    /* Handle option */
    if (optdef - options_info == max_options) {
        opt.error = true;
    } else if ((optdef->compat_level & compat) == 0) {
        char short_name[2] = { optdef->short_name, 0};
        fprintf(stderr, "illegal option (%s) at this compat level\n",
                optdef->long_name ? optdef->long_name : short_name);
        opt.error = true;
    } else {
     switch (optdef->option) {
      case OPT_LEGACY:
        compat = COMPAT_CHPST;
        break;
      case OPT_VERSION:
        opt.version = true;
        break;
      case OPT_HELP:
        opt.help = true;
        break;
      case OPT_VERBOSE:
        opt.verbosity++;
        break;
      case OPT_ARGV0:
        opt.argv0 = optarg;
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
      case OPT_RO_SYS:
        opt.ro_sys = true;
        break;
      case OPT_SETUIDGID:
        if (usrgrp_parse(&opt.users_groups, optarg))
          opt.error = true;
        else if (usrgrp_resolve(&opt.users_groups))
          opt.error = true;
        if (opt.verbosity > 1)
          usrgrp_print(stderr, &opt.users_groups);
        opt.setuidgid = true;
        break;
      case OPT_ENVUIDGID:
        fprintf(stderr, "-%c%s not yet implemented\n",
                        optdef->long_name ? '-' : optdef->short_name,
                        optdef->long_name ? optdef->long_name : "");
        opt.error = true;
      }
    }
  }
  return optind;
}

void options_free(void) {
  if (optstr)
    free(optstr);
  usrgrp_free(&opt.users_groups);
}
