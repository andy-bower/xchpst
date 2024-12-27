/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

#ifndef _CAPS_H
#define _CAPS_H

#include <sys/capability.h>

bool set_capabilities_bounding_set(void);
bool drop_capabilities(void);

#endif
