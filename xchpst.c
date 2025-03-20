/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024,2025 Andrew Bower <andrew@bower.uk> */

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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/prctl.h>
#include <linux/ioprio.h>
#include <sys/file.h>
#include <sys/dir.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/pidfd.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#include "xchpst.h"
#include "caps.h"
#include "join.h"
#include "env.h"
#include "options.h"
#include "rootfs.h"
#include "mount.h"
#include "precreate.h"

static const char *version_str = STRINGIFY(PROG_VERSION);
#ifdef PROG_DEFAULT
static const char *default_app = STRINGIFY(PROG_DEFAULT);
#else
static const char *default_app = "xchpst";
#endif
#define PROG "xchpst"

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
#define max_apps ((ssize_t) (sizeof apps / (sizeof *apps)))

struct runtime runtime = { 0 };

static const char *std_run_dir = "/run/" PROG;
static const char *fallback_run_dir = "/tmp/run-" PROG;
static int run_dir_fd = -1;
char *run_dir;

static struct {
  char uid[16];
  char gid[16];
} extra_env;

static void version(FILE *out) {
  fprintf(out,
          "xchpst-%s (c) Copyright 2024,2025 Andrew Bower <andrew@bower.uk>\n",
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

int ensure_dir(int dirfd, const char *path, int *fd, mode_t mode) {
  for (int creating = 0; *fd == -1 && creating <= 1; creating++) {
    *fd = openat(dirfd, path, O_DIRECTORY | O_CLOEXEC);
    if (*fd == -1 && !creating && (errno == ENOENT))
      mkdirat(dirfd, path, mode);
  }
  if (*fd == -1 && errno != EEXIST)
    return -1;
  return 0;
}

int get_run_dir(void) {
  int rc = ensure_dir(-1, std_run_dir, &run_dir_fd, 0700);
  if (rc == -1) {
    char *xdg_run_dir = getenv("XDG_RUNTIME_DIR");
    if (xdg_run_dir) {
      char *path;
      rc = asprintf(&path, "%s/%s", xdg_run_dir, PROG);
      if (rc != -1) {
        rc = ensure_dir(-1, path, &run_dir_fd, 0700);
        if (rc != -1) {
          run_dir = path;
        } else {
          free(path);
        }
      }
    }
  } else {
    run_dir = strdup(std_run_dir);
  }
  if (rc == -1 &&
      ensure_dir(-1, fallback_run_dir, &run_dir_fd, 0700) != -1)
    run_dir = strdup(fallback_run_dir);

  return run_dir_fd;
}

static int write_once(const char *file, const char *fmt, ...) {
  int fd = open(file, O_WRONLY);
  char *text;
  ssize_t len;
  int rc = 0;
  va_list args;

  if ((rc = fd == -1 ? 1 : 0))
    goto fail0;

  va_start(args, fmt);
  len = vasprintf(&text, fmt, args);
  va_end(args);
  if (text == NULL || len == -1) {
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

  if (option->soft_specified || option->hard_specified) {
    if (getrlimit(resource, &prev) != 0) {
      fprintf(stderr, "warning: resource type %d cannot be controlled on this kernel\n", resource);
      return;
    }
  } else {
    return;
  }

  if (option->hard_specified)
    prev.rlim_max = option->limits.rlim_max;

  if (option->soft_specified) {
    if (option->limits.rlim_cur != RLIM_INFINITY &&
        option->limits.rlim_max != RLIM_INFINITY &&
        option->limits.rlim_cur > prev.rlim_max) {
      fprintf(stderr, "warning capping requested %d soft limit from %lld to maximum %lld\n",
              resource, (long long) option->limits.rlim_cur, (long long) prev.rlim_max);
      prev.rlim_cur = prev.rlim_max;
    } else {
      prev.rlim_cur = option->limits.rlim_cur;
    }
  }

  if (setrlimit(resource, &prev) != 0) {
    fprintf(stderr, "warning: failed to set type %d soft limit\n", resource);
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
  set_rlimit(RLIMIT_MSGQUEUE, &opt.rlimit_msgqueue);
  set_rlimit(RLIMIT_NICE, &opt.rlimit_nice);
  set_rlimit(RLIMIT_RTPRIO, &opt.rlimit_rtprio);
  set_rlimit(RLIMIT_RTTIME, &opt.rlimit_rttime);
  set_rlimit(RLIMIT_SIGPENDING, &opt.rlimit_sigpending);
}

static const struct app *find_app(const char *name) {
  const struct app *app;
  const char *ext = strchrnul(name, '.');

  for (app = apps;
       app - apps < max_apps && strncmp(name, app->name, ext - name);
       app++);

  return app;
}

int main(int argc, char *argv[]) {
  sigset_t newmask;
  sigset_t oldmask;
  char **sub_argv;
  char *executable;
  char *new_root = NULL;
  char *old_root = NULL;
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
  opt.app = find_app(program_invocation_short_name);
  if (opt.app - apps == max_apps)
    opt.app = find_app(default_app);
  assert(opt.app - apps != max_apps);

  if (!options_init())
    return CHPST_ERROR_OPTIONS;
  optind = options_parse(argc, argv);

  if (is_verbose())
    fprintf(stderr, "invoked as %s(%s)\n", opt.app->name, program_invocation_short_name);

  if (!set(OPT_FORK_JOIN) &&
      (opt.new_ns & CLONE_NEWPID)) {
    if (is_verbose())
      fprintf(stderr, "also going to do fork-join since new PID namespace requested\n");
    enable(OPT_FORK_JOIN);
  }

  if (!(opt.new_ns & CLONE_NEWNS) &&
      (set(OPT_NET_NS) || set(OPT_PRIVATE_RUN) || set(OPT_PRIVATE_TMP) ||
       set(OPT_RO_SYS) || set(OPT_RO_HOME) ||
       set(OPT_NEW_ROOT) || set(OPT_PID_NS))) {
    if (is_verbose())
      fprintf(stderr, "also creating mount namespace implicitly due to other options\n");
    opt.new_ns |= CLONE_NEWNS;
  }

  if (opt.exit) {
    ret = opt.error ? CHPST_ERROR_OPTIONS : opt.retcode;
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

  if (optind == argc && !set(OPT_LOGIN))
    opt.error = true;

  if (opt.error) {
    const struct option_info *help_option = find_option(OPT_HELP, NULL);
    if (help_option && opt.app->long_opts && help_option->long_name)
      fprintf(stderr, "%s: error in options. Run %s --%s for usage\n",
              program_invocation_short_name,
              program_invocation_short_name,
              help_option->long_name);
    else
      usage(stderr);
    ret = CHPST_ERROR_OPTIONS;
    goto finish0;
  }

  /* Do xchpsty-type things now! */
  sub_argc = argc - optind;
  if (sub_argc == 0)
    sub_argc++;
  sub_argv = malloc((sub_argc + 1) * sizeof *sub_argv);
  memcpy(sub_argv, argv + optind, (argc - optind) * sizeof *sub_argv);
  sub_argv[sub_argc] = NULL;

  if (set(OPT_UMASK))
    umask(opt.umask);

  if (set(OPT_OOM))
    write_once("/proc/self/oom_score_adj", "%ld", opt.oom_adjust);

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

  if (set(OPT_PGRPHACK)) {
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

  if (set(OPT_ENVUIDGID)) {
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

  if (set(OPT_NICE)) {
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

  if (set(OPT_IO_SCHED)) {
    if (syscall(SYS_ioprio_set,IOPRIO_WHO_PROCESS, 0, opt.ionice_prio) == -1) {
      fprintf(stderr, "warning: failed to set I/O scheduling class\n");
    } else if (is_verbose()) {
      fprintf(stderr, "set IO class to %d:%ld\n",
              IOPRIO_PRIO_CLASS(opt.ionice_prio),
              IOPRIO_PRIO_DATA(opt.ionice_prio));
    }
  }

  if (opt.cpu_affinity.size &&
      sched_setaffinity(0, opt.cpu_affinity.size,
                        opt.cpu_affinity.mask) == -1)
    perror("could not set CPU affinity");

  if (set(OPT_CPU_SCHED) &&
      sched_setscheduler(0, opt.sched_policy, &((struct sched_param) {})) == -1)
    perror("could not change scheduler policy");

  if ((opt.cap_bounds_op != CAP_OP_NONE ||
       opt.caps_op != CAP_OP_NONE) &&
      runtime.absent.caps) {
    fprintf(stderr, "ignoring capabilities as not supported on system");
    opt.cap_bounds_op = opt.caps_op = CAP_OP_NONE;
  }

  if (opt.cap_bounds_op != CAP_OP_NONE)
    if (!set_capabilities_bounding_set())
      goto finish;

  if (set(OPT_SETUIDGID) && opt.users_groups.user.resolved) {
    gid = opt.users_groups.group.gid;
    uid = opt.users_groups.user.uid;
  } else {
    uid = getuid();
    gid = getgid();
  }

  /* Set a login environment */
  if (set(OPT_LOGIN)) {
    struct users_groups *ug = NULL;
    struct users_groups current = { 0 };

    /* Prefer -u, -U then current user, but if the preferred choice
     * is not resolved then we don't set a login environment, rather
     * than picking an unpredictable one. */
    if (set(OPT_SETUIDGID))
      ug = &opt.users_groups;
    else if (set(OPT_ENVUIDGID))
      ug = &opt.env_users_groups;
    else
      usrgrp_resolve_uid(ug = &current, getuid());

    if (ug->user.resolved) {
      if (ug->username && *ug->username) {
        setenv("USER", ug->username, 1);
        setenv("LOGNAME", ug->username, 1);
      }
      if (ug->home && *ug->home) {
        setenv("HOME", ug->home, 1);
      }
      if (ug->shell && *ug->shell) {
        setenv("SHELL", ug->shell, 1);
      }
    }
  }

  if (argc == optind)
    sub_argv[0] = getenv("SHELL");

  executable = sub_argv[0];
  if (opt.argv0)
    sub_argv[0] = opt.argv0;

  if (opt.app_name == NULL)
    opt.app_name = basename(sub_argv[0]);

  {
    uid_t o = set(OPT_SETUIDGID) ? uid : (uid_t) -1;
    gid_t g = set(OPT_SETUIDGID) ? gid : (gid_t) -1;

    if (set(OPT_RUN_DIR) &&
        precreate_dir("/run", 0755, o, g) == -1)
      goto finish;

    if (set(OPT_STATE_DIR) &&
        precreate_dir("/var/lib", 0755, o, g) == -1)
      goto finish;

    if (set(OPT_CACHE_DIR) &&
        precreate_dir("/var/cache", 0755, o, g) == -1)
      goto finish;

    if (set(OPT_LOG_DIR) &&
        precreate_dir("/var/log", 0755, o, g) == -1)
      goto finish;
  }

  if (set(OPT_SETUIDGID) && opt.users_groups.user.resolved) {
    /* You do this backwards: supplemental groups first */
    gid_t *groups = malloc(sizeof(gid_t) * opt.users_groups.num_supplemental);

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
    rc = setresgid(gid, gid, gid);
    if (rc) {
      perror("setresgid");
      goto finish;
    }

    /* Then the actual user */
    rc = setresuid(uid, uid, uid);
    if (rc) {
      perror("setresgid");
      goto finish;
    }
  }

  if (opt.new_ns) {
    rc = unshare(opt.new_ns);
    if (rc == -1) {
      perror(NAME_STR ": unshare()");
      goto finish;
    }
    if (opt.verbosity > 0) fprintf(stderr, "created 0xb%b namespaces\n", opt.new_ns);

    if (opt.new_ns & CLONE_NEWNS) {
      rc = mount(NULL, "/", NULL,
                 MS_REC | MS_SLAVE, NULL);
      if (rc == -1)
        fprintf(stderr, "recursive remounting / as MS_SLAVE: %s", strerror(errno));
    }

    if (opt.new_ns & CLONE_NEWNET)
      special_mount("/sys", "sysfs", "sysfs", NULL);
  }

  if (opt.new_ns & CLONE_NEWUSER) {
    rc = prctl(PR_SET_DUMPABLE, 1);
    setgroups(0, NULL);
    write_once("/proc/self/setgroups", "%s", "deny\n");
    write_once("/proc/self/gid_map", "%u %u %u\n", 0, gid, 1);
    write_once("/proc/self/uid_map", "%u %u %u\n", 0, uid, 1);
    if (setresgid(0, 0, 0) != 0 ||
        setresuid(0, 0, 0) != 0)
      fprintf(stderr, "warning: error becoming root in user namespace, %s\n", strerror(errno));
  }

  if (opt.net_adopt) {
    const char *failed_op = NULL;
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

  if (set(OPT_NEW_ROOT)) {
    if (!create_new_root(basename(executable), &new_root, &old_root))
      goto finish;
  }

  set_resource_limits();

  if (set(OPT_FORK_JOIN)) {
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
      if (sigprocmask(SIG_SETMASK, &oldmask, NULL) == -1)
        perror("warning: could not restore signal mask in child");
    }
  }

  /*************************************
   *  Inside child if fork-join used   *
   *************************************/

  if (set(OPT_NEW_ROOT)) {
    if (!pivot_to_new_root(new_root, old_root))
      goto finish;
    else
      in_new_root = true;
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

  if (opt.new_ns & CLONE_NEWPID) {
    special_mount("/proc", "proc", "procfs", NULL);
  }

  if (set(OPT_PRIVATE_RUN) &&
      private_mount("/run") == -1)
    goto finish;

  if (set(OPT_PRIVATE_TMP) &&
      (private_mount("/tmp") == -1 ||
       private_mount("/var/tmp") == -1))
    goto finish;

  if (set(OPT_PROTECT_HOME) &&
      (private_mount("/home") == -1 ||
       private_mount("/root") == -1 ||
       private_mount("/run/user") == -1))
    goto finish;

  if (!set(OPT_PROTECT_HOME) && set(OPT_RO_HOME) &&
      (remount_ro("/home") == -1 ||
       remount_ro("/root") == -1 ||
       remount_ro("/run/user") == -1))
    goto finish;

  if (set(OPT_RO_SYS) &&
      remount_sys_ro() == -1)
    goto finish;

  if (opt.caps_op != CAP_OP_NONE)
    if (!drop_capabilities())
      goto finish;

  for (unsigned int close_fds = opt.close_fds; close_fds; close_fds &= ~(1 << fd))
    close(fd = /*stdc_trailing_zeros*/ __builtin_ctz(close_fds));

  if (set(OPT_NO_NEW_PRIVS) && prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) == -1)
    perror("could not honour --no-new-privs");

  /* Launch the target */
  rc = execvp(executable, sub_argv);

  /* Handle errors launching */
  assert(rc == -1);
  perror(NAME_STR ": execvp");

join:
  if (set(OPT_FORK_JOIN) && child != 0)
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

  if (run_dir_fd != -1)
    close(run_dir_fd);

  if (lock_fd != -1)
    close(lock_fd);

  free(new_root);
  free(old_root);
  free(sub_argv);
  free(run_dir);

finish0:
  options_free();
  return ret;
}
