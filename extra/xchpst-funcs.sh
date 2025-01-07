# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: (c) Copyright 2024,2025 Andrew Bower <andrew@bower.uk>

# This snippet can be sourced by scripts that call xchpst but should
# fall back to chpst-compatible options in the absence of xchpst.
# Requires the use of the -@ divider or -- separator.

xchpst () {
  if command xchpst --exit 2>/dev/null; then
    command xchpst "$@";
  else
    # Consume extended arguments
    while [ $# -gt 0 -a "$1" != "-@" -a "$1" != "--" ]
    do
      shift
    done

    # Consume -@ but not --
    if [ "$1" = "-@" ]
    then
      shift
    fi

    chpst "$@"
  fi
}
