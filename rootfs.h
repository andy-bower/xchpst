/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#ifndef _ROOTFS_H
#define _ROOTFS_H

enum special_mount {
 SPECIAL_PROC = 0,
 SPECIAL_MAX
};

struct mount_info {
  char *from;
  char *to;
  bool mounted;
  int rc;
};

extern struct mount_info *special_mounts[SPECIAL_MAX];

extern bool create_new_root(const char *executable, char **new_root, char **old_root);
bool pivot_to_new_root(char *old_root, char *new_root);
extern void unmount_temp_rootfs(void);
extern void free_rootfs_data(void);

#endif
