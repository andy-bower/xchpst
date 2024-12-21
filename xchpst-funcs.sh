# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk>

# Call xchpst where available, otherwise strip extended options and
# pass every option after the -@ divider to chpst
xchpst () {
  if { command xchpst --version >/dev/null 2>&1; test $? -eq 100; }; then
    command xchpst "$@";
  else
    while test "$1" != "-@" -a "$1" != "--" && shift; do true; done; shift;
    chpst "$@"
  fi
}
