/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#ifndef _JOIN_H
#define _JOIN_H

bool join(pid_t child, sigset_t *mask, sigset_t *oldmask, int *retcode);

#endif
