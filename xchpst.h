/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#ifndef _XCHPST_H
#define _XCHPST_H

#define STRINGIFY(x) _STRINGIFY(x)
#define _STRINGIFY(x) #x

enum chpst_exit {
  CHPST_ERROR_OPTIONS = 100,
  CHPST_ERROR_CHANGING_STATE = 111,

  /* chpst(8) returns 100 for no-ops like -v; do likewise */
  CHPST_OK = CHPST_ERROR_OPTIONS,

  /* 'xchpst --exit' is a quick way of detecting presence of the tool */
  CHPST_ERROR_EXIT = 0,
};

#include "options.h"

#define NAME_STR STRINGIFY(PROG_NAME)

/* Support levels determined at runtime */
struct runtime {
  struct {
    bool caps;
  } absent;
};

extern struct runtime runtime;
extern char *run_dir;

extern int ensure_dir(int dirfd, const char *path, int *fd, mode_t mode);
extern int get_run_dir(void);

#endif
