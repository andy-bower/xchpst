/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

/* xchpst: eXtended Change Process State
 * A tool that is backwards compatible with chpst(8) from runit(8),
 * offering additional options to harden process with namespace isolation
 * and more. */

#include <libgen.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/dir.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>

#include "xchpst.h"
#include "rootfs.h"
#include "mount.h"

/* Missing in glibc */
static int pivot_root(const char *new_root, const char *put_old) {
  return syscall(SYS_pivot_root, new_root, put_old);
}

static const char *special_mount_names[] = {
  [SPECIAL_PROC] = "proc",
};

static const char *exclude_root_dirs[] = {
  ".",
  "..",
  "lost+found",
  NULL,
};

struct mount_info *special_mounts[SPECIAL_MAX];
static struct mount_info *mts = NULL;
static ssize_t num_mounts = 0;

static bool bind_root_dirs(const char *new_root) {
  enum special_mount special;
  const struct dirent *de;
  struct stat statbuf;
  const char *entity;
  const char **filter;
  DIR *dir = NULL;
  char *path = NULL;
  struct mount_info *mt;
  bool success = false;
  int dir1 = -1;
  int dir2 = -1;

  mts = NULL;
  memset(special_mounts, '\0', sizeof special_mounts);

  if ((dir1 = open(entity = "/", O_RDONLY | O_DIRECTORY)) == -1) {
    perror("opening root directory");
    return false;
  }
  if ((dir2 = openat(dir1, "./", O_RDONLY | O_DIRECTORY)) == -1)
    goto fail;
  if (fstat(dir1, &statbuf) == -1) {
    perror("stat");
    goto fail;
  }
  if ((dir = fdopendir(dir1)) == NULL) {
    perror("opening root directory");
    close(dir1);
    return false;
  }

  mts = calloc(statbuf.st_nlink, sizeof *mts);
  if (mts == NULL) {
    perror("calloc");
    goto fail;
  }
  mt = mts;
  for (errno = 0; (de = readdir(dir));) {
    if (asprintf(&mt->from, "/%s", de->d_name) == -1 ||
        asprintf(&mt->to, "%s/%s", new_root, de->d_name) == -1)
      mt->to = NULL;

    if (de->d_type == DT_LNK) {
      int rc;
      path = malloc(PATH_MAX);
      if (path == NULL)
        goto fail;
      rc = readlinkat(dir2, de->d_name, path, PATH_MAX - 1);
      if (rc == -1 || rc >= PATH_MAX - 1)
        goto fail;
      path[rc]='\0';
      if (symlink(path, mt->to) == -1 || is_verbose())
        fprintf(stderr, "  symlink(%s,%s)=%s\n", path, mt->to, strerror(errno));
      free(path);
      path = NULL;
    }

    if (de->d_type != DT_DIR)
      continue;

    for (filter = exclude_root_dirs; *filter && strcmp(de->d_name, *filter); filter++);
    if (*filter)
      continue;

    for (special = 0; special < SPECIAL_MAX && strcmp(de->d_name, special_mount_names[special]); special++);

    if (is_debug()) {
      fprintf(stderr, "binding %s into new rootfs\n", de->d_name);
    }
    mkdir(mt->to, 0700);
    if (special < SPECIAL_MAX) {
      if (is_debug())
        fprintf(stderr, "  found special mount %s\n", de->d_name);
      special_mounts[special] = mt;
    }
    if (special != SPECIAL_PROC || (opt.new_ns & CLONE_NEWPID) == 0) {
      mt->rc = mount(mt->from, mt->to, NULL, MS_BIND | MS_REC, NULL);
      if (is_debug() || mt->rc != 0)
        fprintf(stderr, "  mount(%s,%s)=%s\n", mt->from, mt->to, strerror(mt->rc == 0 ? 0 : errno));
      if (mt->rc == 0)
        mt->mounted = true;
    }

    mt++;
  }
  num_mounts = mt - mts;

  success = true;
fail:
  if (!success)
    fprintf(stderr, "failed to bind directories into new root, %s\n", strerror(errno));

  if (mts && !success) {
    unmount_temp_rootfs();
    free_rootfs_data();
  }
  free(path);
  closedir(dir);
  if (dir2 != -1)
    close(dir2);
  return success;
}

void unmount_temp_rootfs(void) {
  struct mount_info *mt;

  for (mt = mts; mt - mts < num_mounts; mt++) {
    if (mt->to && mt->mounted)
      umount2(mt->to, MNT_DETACH);
  }
}

void free_rootfs_data(void) {
  struct mount_info *mt;

  if (!mts)
    return;

  for (mt = mts; mt - mts < num_mounts; mt++) {
    free(mt->from);
    free(mt->to);
  }
  free(mts);
  mts = NULL;
}

bool create_new_root(const char *executable,
                     char **save_new_root,
                     char **save_old_root) {
  char *old_root = NULL;
  char *new_root = NULL;
  struct timeval t = { 0 };
  bool success = false;
  int rc;

  get_run_dir();
  gettimeofday(&t, NULL);
  /* basename(3) promises that with _GNU_SOURCE defined, its argument is
     unmodified. */
  rc = asprintf(&new_root, "%s/rootfs-%lld-%d-%s",
                "/run/xchpst",
                (long long) t.tv_sec, getpid(), basename((char *)executable));
  if (rc == -1) {
    perror("formatting new root");
    goto finish;
  }
  *save_new_root = new_root;
  private_mount(new_root);
  bind_root_dirs(new_root);
  if ((asprintf(&old_root, "%s/%s", new_root, ".old_root")) == -1)
    goto finish;
  *save_old_root = old_root;
  if ((mkdir(old_root, 0700))) {
    perror("mkdir(new_root/.old_root)");
    goto finish;
  }
  success = true;
finish:
  if (new_root && !success) {
    unmount_temp_rootfs();
    free_rootfs_data();
  }
  return success;
}

bool pivot_to_new_root(char *new_root, char *old_root) {
  bool success = false;
  int rc;

  if (chdir(new_root) == -1)
    perror("chdir to new root");
  else
    success = true;

  rc = pivot_root(new_root, old_root);
  if (rc == -1) {
    fprintf(stderr, "could not pivot %s to new root %s, %s\n",
            old_root, new_root,
            strerror(errno));
    goto finish;
  } else if (is_verbose()) {
    fprintf(stderr, "pivoted new root from %s to %s\n", old_root, new_root);
  }

  if (chdir("/") == -1)
    perror("chdir to new root");
  else if (chroot(".") == -1)
    perror("chroot to pivotted root");
  else
    success = true;

  {
    /* FIXME: To remove the new root from the parent fs we will need to fork
     * before changing mount namespace. Leaking an empty directory in /run
     * will be a good trade-off for now. */
    char *path;
    rc = asprintf(&path, "/.old_root%s", new_root);
    if (rc == -1 || umount2(path, MNT_DETACH))
      {}//perror("unmount new root from parent fs");
    if (rc != -1 && rmdir(path) == -1)
      {}//perror("deleting new root mount point from parent fs");
    if (rc == 0)
      free(path);
  }

  if (umount2("/.old_root", MNT_DETACH) == -1)
    perror("umounting old root");

  if (rmdir("/.old_root") == -1)
    perror("removing old root mount point");

finish:
  return success;
}
