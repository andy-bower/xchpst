/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: (c) Copyright 2024 Andrew Bower <andrew@bower.uk> */

/* xchpst: eXtended Change Process State
 * A tool that is backwards compatible with chpst(8) from runit(8),
 * offering additional options to harden process with namespace isolation
 * and more. */

#include <poll.h>
#include <signal.h>
#include <linux/prctl.h>
#include <sys/file.h>
#include <sys/pidfd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

#include "xchpst.h"
#include "join.h"

bool join(pid_t child, sigset_t *mask, sigset_t *oldmask, int *retcode) {
  enum {
    /* Offsets into poll set */
    my_pidfd = 0,
    my_signalfd = 1
  };
  struct signalfd_siginfo siginf;
  siginfo_t pidinf;
  int ready;
  int sfd;
  int pidfd;
  int rc;

  pidfd = pidfd_open(child, PIDFD_NONBLOCK);
  if (pidfd == -1) {
    perror("error setting up child supervision");
    kill(child, SIGKILL);
    return false;
  }

  sfd = signalfd(-1, mask, SFD_NONBLOCK);
  if (sfd == -1) {
    close(pidfd);
    perror("error setting up signal proxy");
    pidfd_send_signal(pidfd, SIGKILL, nullptr, 0);
    return false;
  }

  struct pollfd pollset[2] = {
    [my_pidfd] = { .fd = pidfd, .events = POLLIN },
    [my_signalfd] = { .fd = sfd, .events = POLLIN },
  };

  while(true) {
    ready = poll(pollset, 2, -1);
    if (ready == -1 && errno != EINTR) {
      perror("poll");
    } else if (ready != 0) {
      if (pollset[my_pidfd].revents & POLLIN) {

        /* Handle event on pidfd */
        pidinf.si_pid = 0;
        pidinf.si_signo = 0;
        rc = waitid(P_PIDFD, pidfd, &pidinf, WEXITED | WNOHANG);
        if (rc == -1) {
          perror("waitid");
        } else if (rc == 0 &&
                 pidinf.si_signo == SIGCHLD &&
                 pidinf.si_pid == child) {
          if (pidinf.si_code == CLD_KILLED || pidinf.si_code == CLD_DUMPED) {
            if (is_verbose())
              fprintf(stderr, "child killed by signal %d\n", pidinf.si_status);
            *retcode = 128 + pidinf.si_status;
          } else if (pidinf.si_code == CLD_EXITED) {
            *retcode = pidinf.si_code;
          }
          break;
        } else {
          fprintf(stderr, "got SIGCHLD from someone else's child (%d)!\n",
                  pidinf.si_pid);
        }
      }

      if (pollset[my_signalfd].revents & POLLIN) {

        /* Handle a signal received by parent process and pass to child */
        rc = read(sfd, &siginf, sizeof siginf);
        if (rc != sizeof siginf) {
          perror("read signalfd");
          break;
        }
        if (is_verbose())
          fprintf(stderr, "passing on signal %d to child\n", siginf.ssi_signo);
        pidfd_send_signal(pidfd, siginf.ssi_signo, nullptr, 0);
      }
    }
  }

  close(sfd);
  close(pidfd);
  sigprocmask(SIG_SETMASK, oldmask, nullptr);

  return true;
}
