/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "usrgrp.h"

int usrgrp_parse(struct users_groups *ug, const char *arg) {
  enum tok_type t = TOK_NAME;
  enum {
    STATE_USER,
    STATE_GROUP,
    STATE_SUPPLEMENTAL,
  } state = STATE_USER;
  char *copy;
  char *scan;
  ssize_t n_toks;
  int i;

  copy = strdup(arg);
  if (copy == NULL) return -1;

  ug->buf_tok = copy;
  if (*copy == ':') {
    t = TOK_ID;
    copy++;
  }

  for (n_toks = 0, scan = copy; strsep(&scan, ":"); n_toks++);
  if (n_toks > 2) {
    ug->num_supplemental = n_toks - 2;
    ug->supplemental = malloc(ug->num_supplemental * sizeof(struct sys_entry));
    if (ug->supplemental == NULL) {
      free(ug->buf_tok);
      return -1;
    }
  }

  scan = copy;
  for(i = 0; i < n_toks; i++) {
    struct sys_entry entry = (struct sys_entry) {
      .tok = scan,
      .tok_type = t,
    };
    switch (state) {
    case STATE_USER:
      ug->user = entry;
      state = STATE_GROUP;
      break;
    case STATE_GROUP:
      ug->group = entry;
      state = STATE_SUPPLEMENTAL;
      break;
    case STATE_SUPPLEMENTAL:
      ug->supplemental[i - 2] = entry;
    }
    scan += strlen(scan) + 1;
  }

  return 0;
}

void usrgrp_resolve_uid(struct users_groups *ug, uid_t nid) {
  struct sys_entry *entry = &ug->user;
  struct passwd *password;

  entry->tok_type = TOK_ID;
  entry->uid = nid;
  entry->resolved = true;
  password = getpwuid(entry->uid);
  if (password) {
    entry->user_gid = password->pw_gid;
    ug->username = strdup(password->pw_name);
    ug->home = strdup(password->pw_dir);
    ug->shell = strdup(password->pw_shell);
  } else {
    entry->user_gid = (gid_t) -1;
    ug->username = ug->home = ug->shell = NULL;
  }
}

static int resolve_user(struct users_groups *ug) {
  struct sys_entry *entry = &ug->user;
  struct passwd *password;
  int toks = 0;
  int rc = 0;
  long nid;

  switch (entry->tok_type) {
  case TOK_NONE:
    entry->uid = (uid_t) -1;
    entry->user_gid = (gid_t) -1;
    entry->resolved = false;
    break;
  case TOK_NAME:
    errno = 0;
    password = getpwnam(entry->tok);
    if (password) {
      entry->uid = password->pw_uid;
      entry->user_gid = password->pw_gid;
      entry->resolved = true;
      ug->username = strdup(password->pw_name);
      ug->home = strdup(password->pw_dir);
      ug->shell = strdup(password->pw_shell);
    } else {
      rc = ENOENT;
      if (errno != 0) {
        rc = errno;
        fprintf(stderr, "getpwnam(\"%s\"): %s\n", entry->tok, strerror(rc));
      }
      entry->uid = (uid_t) -1;
      entry->user_gid = (gid_t) -1;
      entry->resolved = false;
      ug->username = ug->home = ug->shell = NULL;
    }
    break;
  case TOK_ID:
    toks = sscanf(entry->tok, "%ld", &nid);
    if (toks == 1) {
      usrgrp_resolve_uid(ug, nid);
    } else {
      entry->resolved = false;
    }
  }

  return rc;
}

static int resolve_group(struct sys_entry *entry) {
  struct group *group;
  size_t toks;
  int rc = 0;
  long nid;

  switch (entry->tok_type) {
  case TOK_NONE:
    entry->gid = (gid_t) -1;
    entry->resolved = false;
    break;
  case TOK_NAME:
    errno = 0;
    group = getgrnam(entry->tok);
    if (group) {
      entry->gid = group->gr_gid;
      entry->resolved = true;
    } else {
      rc = ENOENT;
      if (errno != 0) {
        rc = errno;
        fprintf(stderr, "getgrnam(\"%s\": %s\n", entry->tok, strerror(rc));
      }
      entry->gid = (gid_t) -1;
      entry->resolved = false;
    }
    break;
  case TOK_ID:
    toks = sscanf(entry->tok, "%ld", &nid);
    if (toks == 1) {
      entry->gid = nid;
      entry->resolved = true;
    } else {
      entry->resolved = false;
    }
  }

  return rc;
}

static const char *tok_type_name(enum tok_type tok_type) {
  switch (tok_type) {
  case TOK_NONE:
    return "NONE";
  case TOK_NAME:
    return "NAME";
  case TOK_ID:
    return "ID";
  default:
    return "?";
  }
}

static void print_user(FILE *out, struct sys_entry *entry) {
  fprintf(out, "%s:%d:%d:%s:%s\n",
          entry->tok ? entry->tok : "",
          entry->uid, entry->user_gid,
          tok_type_name(entry->tok_type),
          entry->resolved ? "RESOLVED" : "");
}

static void print_group(FILE *out, struct sys_entry *entry) {
  fprintf(out, "%s:%d:%s:%s\n",
          entry->tok ? entry->tok : "",
          entry->gid,
          tok_type_name(entry->tok_type),
          entry->resolved ? "RESOLVED" : "");
}

void usrgrp_print(FILE *out, const char *what, struct users_groups *ug) {
  int i;

  fprintf(out, "%s:\n  user: ", what);
  print_user(out, &ug->user);
  fprintf(out, "  group: ");
  print_group(out, &ug->group);
  for (i = 0; i < ug->num_supplemental; i++) {
    fprintf(out, "  supplemental: ");
    print_group(out, ug->supplemental + i);
  }
}

int usrgrp_resolve(struct users_groups *ug) {
  int errors = 0;
  int i;

  if (resolve_user(ug))
    errors++;
  if (resolve_group(&ug->group))
    errors++;
  for (i = 0; i < ug->num_supplemental; i++) {
    if (resolve_group(&ug->supplemental[i]))
      errors++;
  }

  /* Use user's group if another one wasn't requested. */
  if (ug->group.tok_type == TOK_NONE &&
      ug->user.resolved == true) {
    ug->group.tok_type = TOK_ID;
    ug->group.gid = ug->user.user_gid;
    ug->group.resolved = true;
  }

  return errors;
}

void usrgrp_free(struct users_groups *ug) {
  free(ug->home);
  free(ug->shell);
  free(ug->username);
  if (ug->buf_tok)
    free(ug->buf_tok);
  if (ug->supplemental)
    free(ug->supplemental);
}
