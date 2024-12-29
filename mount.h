/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#ifndef _MOUNT_H
#define _MOUNT_H

extern int special_mount(char *path, char *fs, char *desc, char *options);
extern int private_mount(char *path);
extern int remount_ro(const char *path);
extern int remount_sys_ro(void);

#endif
