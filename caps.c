/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#include <linux/prctl.h>
#include <sys/capability.h>

#include "xchpst.h"
#include "options.h"

bool set_capabilities_bounding_set(void) {
  int rc;

  if (opt.cap_bounds_op == CAP_OP_KEEP) {
    int i, max = cap_max_bits();
    cap_bits_t b = 1ull << (max - 1);
    for (i = max - 1; b; i--, b >>= 1) {
      if ((opt.cap_bounds & b) == 0 &&
          cap_get_bound(i) == 1) {
        if (is_verbose())
          fprintf(stderr, "dropping capability %s\n", cap_to_name(i));
        rc = cap_drop_bound(i);
        if (rc == -1) {
          perror("cap_drop_bound");
          return false;
        }
      } else if (is_debug()) {
        fprintf(stderr, "keeping capability %s\n", cap_to_name(i));
      }
    }
  } else if (opt.cap_bounds_op == CAP_OP_DROP) {
    int i, max = cap_max_bits();
    cap_bits_t b = 1ull << (max - 1);
    for (i = max - 1; b; i--, b >>= 1) {
      if (opt.cap_bounds & b) {
        if (is_verbose())
          fprintf(stderr, "dropping capability %s\n", cap_to_name(i));
        rc = cap_drop_bound(i);
        if (rc == -1) {
          perror("cap_drop_bound");
          return false;
        }
      }
    }
  }
  return true;
}

/* Drop effective and permitted capabilities,
 * make capabilities inheritable and
 * add them to the ambient set ahead of execve(). */
bool drop_capabilities(void) {
  cap_value_t max = cap_max_bits();
  cap_bits_t bits = 1ull << (max - 1);
  cap_bits_t make_ambient = 0;
  cap_flag_value_t fv;
  cap_value_t cap;
  cap_t caps;
  bool success = false;

  if (opt.caps_op == CAP_OP_DROP) {
    fv = CAP_CLEAR;
    caps = cap_get_proc();
  } else {
    fv = CAP_SET;
    caps = cap_get_proc(); /* not _init() so we can print diagnostics */
  }
  if (caps == nullptr) {
    perror("could not get init capabilities");
    goto fail0;
  }

  if (is_verbose()) {
    char *capstr = cap_to_text(caps, nullptr);
    fprintf(stderr, "initial capabilities: %s\n", cap_to_text(caps, nullptr));
    cap_free(capstr);
  }
  if (fv == CAP_SET)
    cap_clear(caps);

  for (cap = max - 1; bits; cap--, bits >>= 1) {
    if (opt.caps & bits) {
      if (is_verbose())
        fprintf(stderr, "%s capability %s\n",
                opt.caps_op == CAP_OP_KEEP ? "keeping" : "dropping",
                cap_to_name(cap));
      if (fv == CAP_SET)
        make_ambient |= (1ull << cap);
      if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, fv) == -1 ||
          cap_set_flag(caps, CAP_INHERITABLE, 1, &cap, fv) == -1 ||
          cap_set_flag(caps, CAP_PERMITTED, 1, &cap, fv) == -1) {
        perror("cap_set_flag");
        goto fail;
      }
    } else if (opt.caps_op == CAP_OP_DROP) {
      cap_flag_value_t rdv = CAP_CLEAR;
      cap_get_flag(caps, cap, CAP_PERMITTED, &rdv);
      if (rdv == CAP_SET) {
        make_ambient |= (1ull << cap);
        if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, CAP_SET) == -1 ||
            cap_set_flag(caps, CAP_INHERITABLE, 1, &cap, CAP_SET) == -1 ||
            cap_set_flag(caps, CAP_PERMITTED, 1, &cap, CAP_SET) == -1) {
          perror("cap_set_flag");
          goto fail;
        }
      }
    }
  }

  if (is_verbose()) {
    char *capstr = cap_to_text(caps, nullptr);
    fprintf(stderr, "setting capabilities to: %s\n", cap_to_text(caps, nullptr));
    cap_free(capstr);
  }

  if (cap_set_proc(caps) == -1) {
    perror("setting permitted, effective and inheritable capabilities");
    goto fail;
  }

  if (is_verbose()) {
    cap_free(caps);
    caps = cap_get_proc();
    char *capstr = cap_to_text(caps, nullptr);
    fprintf(stderr, "final capabilities: %s\n", cap_to_text(caps, nullptr));
    cap_free(capstr);
  }

  bits = 1ull << (max - 1);
  for (cap = max - 1; bits; cap--, bits >>= 1) {
    if (make_ambient & bits) {
      if (cap_set_ambient(cap, CAP_SET) == -1) {
        perror("setting ambient capabilities");
        goto fail;
      }
    }
  }

  success = true;
fail:
  cap_free(caps);
fail0:
  return success;
}
