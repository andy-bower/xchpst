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
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbit.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/prctl.h>
#include <sys/file.h>
#include <sys/dir.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/pidfd.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "xchpst.h"
#include "caps.h"
#include "join.h"
#include "env.h"
#include "options.h"
#include "rootfs.h"
#include "mount.h"

static const char *version_str = STRINGIFY(PROG_VERSION);

const struct app apps[] = {
  { COMPAT_CHPST,     "chpst",     .long_opts = false },
  { COMPAT_XCHPST,    "xchpst",    .long_opts = true },
  { COMPAT_SOFTLIMIT, "softlimit", .long_opts = false },
  { COMPAT_ENVDIR,    "envdir",    false, 1, { OPT_ENVDIR } },
  { COMPAT_PGRPHACK,  "pgrphack",  false, 1, { OPT_PGRPHACK } },
  { COMPAT_SETUIDGID, "setuidgid", false, 1, { OPT_SETUIDGID } },
  { COMPAT_ENVUIDGID, "envuidgid", false, 1, { OPT_ENVUIDGID } },
  { COMPAT_SETLOCK,   "setlock",   false, 1, { OPT_LOCK_WAIT } },
};
constexpr size_t max_apps = sizeof apps / (sizeof *apps);

static struct {
  char uid[16];
  char gid[16];
} extra_env;

static void version(FILE *out) {
  fprintf(out,
          "xchpst-%s (c) Copyright 2024, Andrew Bower <andrew@bower.uk>\n",
          version_str);
}

static void usage(FILE *out) {
  version(out);
  fprintf(out, "\nusage: %s OPTIONS [--]",
          program_invocation_short_name);
  options_print_positional(out);
  fprintf(out, " PROG...    launch PROG with changed process state\n");
  options_explain_positional(out);
  options_print(out);
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
  sigset_t newmask;
  sigset_t oldmask;
  char **sub_argv;
  char *executable;
  char *new_root = nullptr;
  char *old_root = nullptr;
  int sub_argc;
  pid_t child;
  int optind;
  int rc = 0;
  int ret = CHPST_ERROR_CHANGING_STATE;
  int lock_fd = -1;
  bool in_new_root = false;
  uid_t uid;
  gid_t gid;
  int fd;
  int i;

  /* As which application were we invoked? */
  for (opt.app = apps;
       opt.app - apps < max_apps && strcmp(program_invocation_short_name, opt.app->name);
       opt.app++);

  if (!options_init())
    return CHPST_ERROR_OPTIONS;
  optind = options_parse(argc, argv);

  if (is_verbose())
    fprintf(stderr, "invoked as %s(%s)\n", opt.app->name, program_invocation_short_name);

  if (!opt.fork_join &&
      (opt.new_ns & CLONE_NEWPID)) {
    if (is_verbose())
      fprintf(stderr, "also going to do fork-join since new PID namespace requested\n");
    opt.fork_join = true;
  }

  if (!(opt.new_ns & CLONE_NEWNS) &&
      (opt.new_ns || opt.private_run || opt.private_tmp || opt.ro_sys || opt.new_root)) {
    if (is_verbose())
      fprintf(stderr, "also creating mount namespace implicitly due to other options\n");
    opt.new_ns |= CLONE_NEWNS;
  }

  if (opt.exit) {
    ret = opt.retcode;
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

  if (optind == argc)
    opt.error = true;

  if (opt.error) {
    usage(stderr);
    ret = CHPST_ERROR_OPTIONS;
    goto finish0;
  }

  /* Do xchpsty-type things now! */
  sub_argc = argc - optind;
  sub_argv = malloc((sub_argc + 1) * sizeof *sub_argv);
  memcpy(sub_argv, argv + optind, sub_argc * sizeof *sub_argv);
  sub_argv[sub_argc] = nullptr;

  executable = argv[optind];

  if (opt.lock_file) {
    if (opt.lock_nowait_override)
      opt.lock_wait = false;
    lock_fd = open(opt.lock_file, O_WRONLY | O_NDELAY | O_APPEND | O_CREAT, 0600);
    if (lock_fd != -1 && flock(lock_fd, LOCK_EX | (opt.lock_wait ? 0 : LOCK_NB)) == -1) {
      close(lock_fd);
      lock_fd = -1;
    }
    if (lock_fd == -1) {
      if (opt.lock_quiet)
        ret = CHPST_ERROR_EXIT;
      else
        fprintf(stderr, "error obtaining lock, %s\n", strerror(errno));
      goto finish;
    }
  }

  if (opt.argv0)
    sub_argv[0] = opt.argv0;

  if (opt.new_session) {
    rc = setsid();
    if (rc == -1) {
      perror("setsid");
      goto finish;
    } else if (is_verbose()) {
      fprintf(stderr, "new session id: %d\n", rc);
    }
  }

  if (opt.env_dir && !read_env_dir(opt.env_dir))
    goto finish;

  if (opt.envuidgid) {
    struct sys_entry *entry;
    if ((entry = &opt.env_users_groups.user)->resolved) {
      rc = snprintf(extra_env.uid, sizeof extra_env.uid, "UID=%d", entry->uid);
      if (rc == -1 || rc == sizeof extra_env.uid) {
        perror("creating UID env");
        goto finish;
      }
      putenv(extra_env.uid);
    }
    if ((entry = &opt.env_users_groups.group)->resolved) {
      rc = snprintf(extra_env.gid, sizeof extra_env.gid, "GID=%d", entry->gid);
      if (rc == -1 || rc == sizeof extra_env.uid) {
        perror("creating UID env");
        goto finish;
      }
      putenv(extra_env.gid);
    }
  }

  if (opt.chroot) {
    rc = chdir(opt.chroot);
    if (rc == -1) {
      perror("chdir for chroot");
      goto finish;
    }
    rc = chroot(".");
    if (rc == -1) {
      perror("chroot");
      goto finish;
    }
    if (is_verbose())
      fprintf(stderr, "entered chroot: %s\n", opt.chroot);
  }

  if (opt.chdir) {
    rc = chdir(opt.chdir);
    if (rc == -1) {
      perror("chdir");
      goto finish;
    }
    if (is_verbose())
      fprintf(stderr, "change directory: %s\n", opt.chdir);
  }

  if (opt.renice) {
    int newnice;

    errno = 0;
    newnice = nice(opt.niceness);
    if (errno) {
      fprintf(stderr, "could not change niceness, %s\n", strerror(errno));
      goto finish;
    }
    if (is_verbose()) {
      fprintf(stderr, "now at niceness %d\n", newnice);
    }
  }

  if (opt.cap_bounds_op != CAP_OP_NONE)
    if (!set_capabilities_bounding_set())
      goto finish;

  if (opt.setuidgid && opt.users_groups.user.resolved) {
    /* You do this backwards: supplemental groups first */
    gid_t *groups = malloc(sizeof(gid_t) * opt.users_groups.num_supplemental);
    gid_t gid;
    uid_t uid;

    if (opt.caps_op != CAP_OP_NONE) {
      /* Postpone the loss of capabilities until after the user switch
       * and drop them when ready. */
      rc = prctl(PR_SET_KEEPCAPS, 1);
    }

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
  }
  if (opt.new_ns & CLONE_NEWUSER) {
    rc = prctl(PR_SET_DUMPABLE, 1);
    if (uid != 0 || gid != 0) {
      setgroups(0, nullptr);
      write_once("/proc/self/setgroups", "%s", "deny\n");
      write_once("/proc/self/gid_map", "%u %u %u\n", 0, gid, 1);
      write_once("/proc/self/uid_map", "%u %u %u\n", 0, uid, 1);
    }
    if (setresgid(0, 0, 0) != 0 ||
        setresuid(0, 0, 0) != 0)
      fprintf(stderr, "warning: error becoming root in user namespace, %s\n", strerror(errno));
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

  if (opt.new_root) {
    if (!create_new_root(executable, &new_root, &old_root))
      goto finish;
  }

  set_resource_limits();

  if (opt.fork_join) {
    /* Save old signal mask for re-use by child and block all signals
     * in the parent so we can get them delivered by signalfd. */
    sigfillset(&newmask);
    sigdelset(&newmask, SIGCHLD);
    sigdelset(&newmask, SIGBUS);
    sigdelset(&newmask, SIGFPE);
    sigdelset(&newmask, SIGILL);
    sigdelset(&newmask, SIGSEGV);
    rc = sigprocmask(SIG_SETMASK, &newmask, &oldmask);
    if (rc == -1) {
      perror("setting up mask for signalfds");
      goto finish;
    }

    child = fork();
    if (child == -1) {
      perror("fork");
      goto finish;
    } else if (child != 0) {
      goto join;
    } else {
      if (sigprocmask(SIG_SETMASK, &oldmask, nullptr) == -1)
        perror("warning: could not restore signal mask in child");
    }
  }

  if (opt.new_ns & CLONE_NEWPID &&
      special_mounts[SPECIAL_PROC]) {
    rc = mount("none", special_mounts[SPECIAL_PROC]->to, "proc", 0, nullptr);
    if (rc == -1)
      perror("mounting proc in new ns");
  }

  if (opt.new_root) {
    if (!pivot_to_new_root(new_root, old_root))
      goto finish;
    else
      in_new_root = true;
  }

  if (!opt.new_root) {
    if (opt.new_ns & CLONE_NEWPID)
      special_mount("/proc", "proc", "procfs", nullptr);
  }

  if (opt.private_run)
    private_mount("/run");

  if (opt.private_tmp) {
    private_mount("/tmp");
    private_mount("/var/tmp");
  }

  if (opt.ro_sys) {
    remount_sys_ro();
  }

  if (opt.caps_op != CAP_OP_NONE)
    if (!drop_capabilities())
      goto finish;

  for (unsigned int close_fds = opt.close_fds; close_fds; close_fds &= ~(1 << fd))
    close(fd = stdc_trailing_zeros(close_fds));

  /* Launch the target */
  rc = execvp(executable, sub_argv);

  /* Handle errors launching */
  assert(rc == -1);
  perror(NAME_STR ": execvp");

join:
  if (opt.fork_join && child != 0)
    join(child, &newmask, &oldmask, &ret);

finish:
  /* Actions here should be
     1) suitable for if exec() fails.
     2) clean up if --fork-join is used.
     3) not be necessary when --fork-join is not used.
   */

  if (new_root && !in_new_root) {
    if (umount2(new_root, MNT_DETACH) == -1)
      fprintf(stderr, "umount2(%s): %s\n", new_root, strerror(errno));
    if (rmdir(new_root) == -1)
      fprintf(stderr, "rmdir(%s): %s\n", new_root, strerror(errno));
    free_rootfs_data();
  }

  if (lock_fd != -1)
    close(lock_fd);

  free(new_root);
  free(old_root);
  free(sub_argv);

finish0:
  options_free();
  return ret;
}
