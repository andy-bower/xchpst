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
#include "mount.h"

int special_mount(char *path, char *fs, char *desc, char *options) {
  const char *op = "mkdirat";
  int rc;

  rc = mkdirat(AT_FDCWD, path, 0777);
  if (rc == 0 || errno == EEXIST) {
    umount2(path, MNT_DETACH);
    op = "mount";
    rc = mount(nullptr, path, fs,
               MS_NODEV | MS_NOEXEC | MS_NOSUID, options);
  } else
    if (rc == -1)
      fprintf(stderr, "in creating %s mount: %s: %s, %s\n",
              desc, path, op, strerror(errno));
  return rc;
}

int private_mount(char *path) {
  return special_mount(path, "tmpfs", "private", "mode=0755");
}

int remount_ro(const char *path) {
  struct stat statbuf;
  int rc;

  if ((rc = stat(path, &statbuf)) == -1 && errno == ENOENT)
    return ENOENT;

  /* Try remount first, in case we don't need a bind mount. */
  rc = mount(path, path, nullptr,
             MS_REMOUNT | MS_BIND | MS_REC | MS_RDONLY, nullptr);
  if (rc == -1) {
    /* we hope errno == EINVAL but no need to check as will find out later */
    rc = mount(path, path, nullptr,
               MS_REC | MS_BIND | MS_SLAVE, nullptr);
    if (rc == -1)
      fprintf(stderr, "recursive bind mounting %s: %s", path, strerror(errno));

    rc = mount(path, path, nullptr,
               MS_REMOUNT | MS_REC | MS_BIND | MS_RDONLY, nullptr);
    if (rc == -1)
      fprintf(stderr, "remounting %s read-only: %s", path, strerror(errno));
  } else if (opt.verbosity > 0) {
    fprintf(stderr, "could go straight to remount for %s\n", path);
  }
  return rc ? -1 : 0;
}

int remount_sys_ro(void) {
  int rc;

  return (((rc = remount_ro("/usr")) && rc != ENOENT) ||
         ((rc = remount_ro("/boot/efi")) && rc != ENOENT) ||
         ((rc = remount_ro("/boot")) && rc != ENOENT)) ? -1 : 0;
}
