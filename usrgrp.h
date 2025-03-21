/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#ifndef _USRGRP_H
#define _USRGRP_H

#include <stdbool.h>

enum tok_type {
  TOK_NONE = 0,
  TOK_NAME,
  TOK_ID,
};

struct sys_entry {
  char *tok;
  union {
    struct {
      uid_t uid;
      gid_t user_gid;
    };
    gid_t gid;
  };
  enum tok_type tok_type;
  bool resolved;
};

struct users_groups {
  struct sys_entry user;
  struct sys_entry group;
  struct sys_entry *supplemental;
  ssize_t num_supplemental;

  /* Buffers to be freed */
  void *buf_tok;
  char *username;
  char *home;
  char *shell;
};

static inline bool usrgrp_specified(const struct sys_entry *entry) {
  return entry->tok_type != TOK_NONE;
}

int usrgrp_parse(struct users_groups *ug, const char *arg);
int usrgrp_resolve(struct users_groups *ug);
void usrgrp_resolve_uid(struct users_groups *ug, uid_t nid);
void usrgrp_print(FILE *out, const char *what, struct users_groups *ug);
void usrgrp_free(struct users_groups *ug);

#endif
