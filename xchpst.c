/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

/* xchpst: eXtended Change Process State
 * A tool that is backwards compatible with chpst(8) from runit(8),
 * offering additional options to harden process with namespace isolation
 * and more. */

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <errno.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/prctl.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/prctl.h>

#include "xchpst.h"
#include "options.h"

enum chpst_exit {
  CHPST_ERROR_OPTIONS = 100,
  CHPST_ERROR_CHANGING_STATE = 111,

  /* chpst(8) returns 100 for no-ops like -v; do likewise */
  CHPST_OK = CHPST_ERROR_OPTIONS,
};

static const char *version_str = STRINGIFY(PROG_VERSION);

const struct app apps[] = {
  { COMPAT_CHPST,     "chpst",     .long_opts = false },
  { COMPAT_XCHPST,    "xchpst",    .long_opts = true },
  { COMPAT_SOFTLIMIT, "softlimit", .long_opts = false },
};
constexpr size_t max_apps = sizeof apps / (sizeof *apps);

static void version(FILE *out) {
  fprintf(out,
          "xchpst-%s (c) Copyright 2024, Andrew Bower <andrew@bower.uk>\n",
          version_str);
}

static void usage(FILE *out) {
  version(out);
  fprintf(out,
          "\nusage: %s OPTIONS [--] PROG...    launch PROG with changed process state\n",
          program_invocation_short_name);
  options_print(out);
}

static int special_mount(char *path, char *fs, char *desc, char *options) {
  int rc;

  rc = mkdirat(AT_FDCWD, path, 0777);
  if (rc == 0 || errno == EEXIST) {
    umount2(path, MNT_DETACH);
    rc = mount(nullptr, path, fs,
               MS_NODEV | MS_NOEXEC | MS_NOSUID, options);
  }
  if (rc == -1)
    fprintf(stderr, "in creating %s mount: %s, %s\n", desc, path, strerror(errno));
  return rc;
}

static int private_mount(char *path) {
  return special_mount(path, "tmpfs", "private", "mode=0755");
}

static int remount(const char *path) {
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

static int remount_sys(void) {
  int rc;

  return (((rc = remount("/usr")) && rc != ENOENT) ||
         ((rc = remount("/boot/efi")) && rc != ENOENT) ||
         ((rc = remount("/boot")) && rc != ENOENT)) ? -1 : 0;
}

static int write_once(const char *file, const char *fmt, ...) {
  int fd = open(file, O_WRONLY);
  char *text;
  size_t len;
  int rc = 0;
  va_list args;

  if ((rc = fd == -1 ? 1 : 0))
    goto fail0;

  va_start(args);
  len = vasprintf(&text, fmt, args);
  va_end(args);
  if (text == nullptr || len == -1) {
    rc = -1;
    goto fail;
  }

  rc = write(fd, text, len);
  rc = (rc == len ? 0 : -1);

  free(text);

fail:
  close(fd);
fail0:
  if (rc != 0)
    fprintf(stderr, "writing to %s: %s\n", file, strerror(errno));
  return rc;
}

void set_rlimit(int resource, struct limit *option) {
  struct rlimit prev;

  if (option->soft_specified) {
    if (getrlimit(resource, &prev) == 0) {
      if (option->limits.rlim_cur != RLIM_INFINITY &&
          option->limits.rlim_cur > prev.rlim_max) {
        fprintf(stderr, "warning capping requested %d soft limit from %lld to maximum %lld\n",
                resource, (long long) option->limits.rlim_cur, (long long) prev.rlim_max);
        prev.rlim_cur = prev.rlim_max;
      } else {
        prev.rlim_cur = option->limits.rlim_cur;
      }
      if (setrlimit(resource, &prev) != 0) {
        fprintf(stderr, "warning: failed to set %d soft limit\n", resource);
      }
    } else {
      fprintf(stderr, "warning: resource type %d cannot be controlled on this kernel\n", resource);
    }
  }
}

void set_resource_limits(void) {
  set_rlimit(RLIMIT_DATA, &opt.rlimit_data);
  set_rlimit(RLIMIT_AS, &opt.rlimit_as);
  set_rlimit(RLIMIT_STACK, &opt.rlimit_stack);
  set_rlimit(RLIMIT_MEMLOCK, &opt.rlimit_memlock);
  set_rlimit(RLIMIT_RSS, &opt.rlimit_rss);
  set_rlimit(RLIMIT_NOFILE, &opt.rlimit_nofile);
  set_rlimit(RLIMIT_NPROC, &opt.rlimit_nproc);
  set_rlimit(RLIMIT_FSIZE, &opt.rlimit_fsize);
  set_rlimit(RLIMIT_CORE, &opt.rlimit_core);
  set_rlimit(RLIMIT_CPU, &opt.rlimit_cpu);
}

int main(int argc, char *argv[]) {
  char **sub_argv;
  char *executable;
  int sub_argc;
  int optind;
  int rc = 0;
  int ret = CHPST_ERROR_CHANGING_STATE;
  int lock_fd = -1;
  uid_t uid;
  gid_t gid;
  int fd;
  int i;

  /* As which application were we invoked? */
  for (opt.app = apps;
       opt.app - apps < max_apps && strcmp(program_invocation_short_name, opt.app->name);
       opt.app++);

  options_init();
  optind = options_parse(argc, argv);

  if (is_verbose())
    fprintf(stderr, "invoked as %s(%s)\n", opt.app->name, program_invocation_short_name);

  if (optind == argc)
    opt.error = true;

  if (!(opt.new_ns & CLONE_NEWNS) &&
      (opt.new_ns || opt.private_run || opt.private_tmp || opt.ro_sys)) {
    if (is_verbose())
      fprintf(stderr, "also creating mount namespace implicitly due to other options\n");
    opt.new_ns |= CLONE_NEWNS;
  }

  if (opt.error) {
    usage(stderr);
    ret = CHPST_ERROR_OPTIONS;
    goto finish0;
  }

  if (opt.help)
    usage(stdout);
  else if (opt.version)
    version(stdout);

  if (opt.help || opt.version) {
    ret = CHPST_OK;
    goto finish0;
  }

  /* Do xchpsty-type things now! */
  sub_argc = argc - optind;
  sub_argv = malloc((sub_argc + 1) * sizeof *sub_argv);
  memcpy(sub_argv, argv + optind, sub_argc * sizeof *sub_argv);
  sub_argv[sub_argc] = nullptr;

  executable = argv[optind];

  if (opt.lock_file) {
    lock_fd = open(opt.lock_file, O_WRONLY | O_NDELAY | O_APPEND | O_CREAT, 0600);
    if (lock_fd == -1) {
      perror("opening lock file");
      goto finish;
    }
    if (flock(lock_fd, LOCK_EX | (opt.lock_wait ? 0 : LOCK_NB)) == -1) {
      perror("obtaining lock");
      goto finish;
    }
  }

  if (opt.argv0)
    sub_argv[0] = opt.argv0;

  if (opt.drop_cap_bounds) {
    int i, max = cap_max_bits();
    cap_bits_t b = 1ull << (max - 1);
    for (i = max - 1; b; i--, b >>= 1) {
      if ((opt.cap_bounds & b) == 0 &&
          cap_get_bound(i) == 1) {
        if (is_verbose())
          fprintf(stderr, "dropping capability %s\n", cap_to_name(i));
        rc = cap_drop_bound(i);
        if (rc == -1) {
          perror("cap_drop_bound");
          goto finish;
        }
      } else if (is_debug()) {
        fprintf(stderr, "keeping capability %s\n", cap_to_name(i));
      }
    }
  }

  if (opt.setuidgid && opt.users_groups.user.resolved) {
    /* You do this backwards: supplemental groups first */
    gid_t *groups = malloc(sizeof(gid_t) * opt.users_groups.num_supplemental);
    gid_t gid;
    uid_t uid;

    /* TODO - might need to do this to retain capabilities needed to effect
     * other changes when also dropping user. */
    /* rc = prctl(PR_SET_KEEPCAPS, 1); */

    if (!groups) goto finish;
    for (i = 0; i < opt.users_groups.num_supplemental; i++)
      groups[i] = opt.users_groups.supplemental[i].gid;
    rc = (setgroups(i, groups) == -1 ? errno : 0);
    free(groups);
    if (rc) {
      perror("setgroups");
      goto finish;
    }

    /* Then main group */
    gid = opt.users_groups.group.gid;
    rc = setresgid(gid, gid, gid);
    if (rc) {
      perror("setresgid");
      goto finish;
    }

    /* Then the actual user */
    uid = opt.users_groups.user.uid;
    rc = setresuid(uid, uid, uid);
    if (rc) {
      perror("setresgid");
      goto finish;
    }
  }
  uid = getuid();
  gid = getgid();

  if (opt.new_ns) {
    rc = unshare(opt.new_ns);
    if (rc == -1) {
      perror(NAME_STR ": unshare()");
      goto finish;
    }
    if (opt.verbosity > 0) fprintf(stderr, "created 0xb%b namespaces\n", opt.new_ns);

    if (opt.new_ns & CLONE_NEWNS) {
      rc = mount(nullptr, "/", nullptr,
                 MS_REC | MS_SLAVE, nullptr);
      if (rc == -1)
        fprintf(stderr, "recursive remounting / as MS_SLAVE: %s", strerror(errno));
    }

    if (opt.new_ns & CLONE_NEWNET)
      special_mount("/sys", "sysfs", "sysfs", nullptr);
    if (opt.new_ns & CLONE_NEWPID)
      special_mount("/proc", "proc", "procfs", nullptr);
    if (opt.new_ns & CLONE_NEWUSER) {
      setgroups(0, nullptr);
      write_once("/proc/self/setgroups", "%s", "deny");
      write_once("/proc/self/gid_map", "%u %u %u", 0, gid, 1);
      write_once("/proc/self/uid_map", "%u %u %u", 0, uid, 1);
      setresgid(0, 0, 0);
      setresuid(0, 0, 0);
    }
  }

  if (opt.net_adopt) {
    const char *failed_op = nullptr;
    if ((fd = open(opt.net_adopt, O_RDONLY)) != -1) {
      if ((rc = setns(fd, CLONE_NEWNET)) == 0) {
        if ((rc = umount2(opt.net_adopt, MNT_DETACH)) == 0) {
          if ((rc = unlink(opt.net_adopt)) == -1)
            failed_op = "unlink";
        } else failed_op = "umount2";
      } else {
        failed_op = "setns"; close(fd);
      }
    } else failed_op = "open";
    if (failed_op) {
      perror(failed_op);
      goto finish;
    }
    if (opt.verbosity > 0) fprintf(stderr, "adopted net ns\n");
  }

  if (opt.private_run)
    private_mount("/run");

  if (opt.private_tmp) {
    private_mount("/tmp");
    private_mount("/var/tmp");
  }

  if (opt.ro_sys) {
    remount_sys();
  }

  set_resource_limits();

  /* Launch the target */
  rc = execvp(executable, sub_argv);

  /* Handle errors launching */
  assert(rc == -1);
  perror(NAME_STR ": execvp");

finish:
  if (lock_fd != -1)
    close(lock_fd);

  free(sub_argv);

finish0:
  options_free();
  return ret;
}
