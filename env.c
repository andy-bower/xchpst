/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

/* xchpst: eXtended Change Process State
 * A tool that is backwards compatible with chpst(8) from runit(8),
 * offering additional options to harden process with namespace isolation
 * and more. */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "xchpst.h"
#include "caps.h"
#include "options.h"

bool read_env_dir(const char *dir_name) {
  int dir1 = -1;
  int dir2 = -1;
  int file = -1;
  DIR *dir = NULL;
  const char *entity;
  const struct dirent *de;
  bool success = false;
  struct stat statbuf;
  char *data = NULL;
  ssize_t data_sz = 0;
  ssize_t buffered;
  ssize_t end;
  ssize_t ptr;
  int rc;

  if ((dir1 = open(entity = dir_name, O_RDONLY | O_DIRECTORY)) == -1)
    goto fail;
  if ((dir2 = openat(dir1, "./", O_RDONLY | O_DIRECTORY)) == -1)
    goto fail;
  if ((dir = fdopendir(dir1)) == NULL)
    goto fail;

  for (errno = 0; (de = readdir(dir));) {
    if (de->d_type == DT_DIR)
      continue;
    if ((file = openat(dir2, entity = de->d_name, O_RDONLY)) == -1)
      goto fail;
    if ((rc = fstat(file, &statbuf)) == -1)
      goto fail;
    if (statbuf.st_size > data_sz) {
      data_sz = statbuf.st_size;
      free(data);
      data = malloc(data_sz + 1);
      if (data == NULL)
        goto fail;
    }

    for (ptr = 0, buffered = 0, end = statbuf.st_size; ptr < end; ptr++) {
      /* Slurp chunks of data */
      if (ptr == buffered) {
        if ((rc = read(file, data + ptr, end - ptr)) == -1)
          goto fail;
        else
          buffered += rc;
      }
      /* Terminate at first LF; turn NUL within value into LF */
      if (data[ptr] == '\n')
        end = ptr;
      else if (data[ptr] == '\0')
        data[ptr] = '\n';
    }
    /* Remove trailing whitespace */
    for (ptr = end - 1; ptr >= 0; ptr--)
      if (data[ptr] == ' ' || data[ptr] == '\t')
        data[ptr] = '\0';
      else
        break;

    close(file);
    file = -1;

    if (statbuf.st_size != 0) {
      data[end] = '\0';
      if (is_verbose())
        fprintf(stderr, "setting %s=%s\n", de->d_name, data);
      setenv(de->d_name, data, 1);
    } else {
      if (is_verbose())
        fprintf(stderr, "unsetting %s\n", de->d_name);
      unsetenv(de->d_name);
    }
  }
  success = errno == 0 ? true : false;

fail:
  if (!success)
    fprintf(stderr, "error reading environment \"%s\", %s\n", entity, strerror(errno));
  free(data);
  if (file != -1)
    close(file);
  if (dir != NULL)
    closedir(dir);
  else if (dir1 != -1)
    close(dir1);
  if (dir2)
    close(dir2);
  return success;
}
