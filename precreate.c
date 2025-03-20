/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

/* xchpst: eXtended Change Process State
 * A tool that is backwards compatible with chpst(8) from runit(8),
 * offering additional options to harden process with namespace isolation
 * and more. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/dir.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "xchpst.h"
#include "options.h"
#include "precreate.h"

int precreate_dir(const char *area, mode_t mode, uid_t owner, uid_t group) {
  int dirfd = openat(-1, area, O_DIRECTORY | O_CLOEXEC);
  int rc = -1;
  int err = errno;

  if (dirfd == -1) {
    fprintf(stderr, "could not open %s area, %s\n",
            area, strerror(errno));
  } else {
    rc = mkdirat(dirfd, opt.app_name, mode);
    err = rc == 0 ? 0 : errno;
    if (rc == -1 && err != EEXIST) {
      fprintf(stderr, "could not create dir for %s under %s, %s\n",
              opt.app_name, area, strerror(err));
    }
    if ((rc == 0 || err == EEXIST) &&
        (owner != (uid_t) -1 || group != (gid_t) -1)) {
      rc = fchownat(dirfd, opt.app_name, owner, group, 0);
      if (rc == -1) {
        fprintf(stderr, "warning: could not set ownership of %s under %s, %s\n",
          opt.app_name, area, strerror(errno));
        /* This will not be a cause of failure. */
      }
    }
  }
  close(dirfd);
  return err == EEXIST ? 0 : rc;
}
