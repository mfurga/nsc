#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>

#define STACK_SIZE 4096

char *mnt_src;
char *mnt_dst;

char *prog;
char **prog_argv;

static int init_procfs(void) {
  if (mkdir("/proc", 0555) == -1) {
    perror("mkdir error");
  }

  if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
    perror("mount proc");
    return 1;
  }

  return 0;
}

int init_mount(void) {
  if (chdir("/") == -1) {
    perror("chdir error");
    return 1;
  }

  if (mount("/", "/", NULL, MS_PRIVATE | MS_REC, NULL) == -1) {
    perror("mount error");
    return 1;
	}

	if (mount(mnt_src, mnt_dst, NULL, MS_BIND |
                                    MS_REC |
                                    MS_PRIVATE |
                                    MS_RDONLY, NULL) == -1) {
    perror("mount tmpfs");
		return 1;
	}

  if (chdir(mnt_dst) == -1) {
    perror("chdir error");
    return 1;
  }

  if (chroot(".") == -1) {
    perror("chroot error");
    return 1;
  }

  init_procfs();

  return 0;
}

int child(void *arg) {
  (void)arg;

  if (init_mount()) {
    return 1;
  }

  char *argv[] = {
    "/bin/sh",
    NULL
  };

  execv(argv[0], argv);

  return 0;
}


int main(int argc, char **argv) {

  for (int i = 0; i < argc; i++) {

    if (strcmp(argv[i], "--chroot") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing --chroot argument\n");
        return 1;
      }

      mnt_src = argv[++i];
      continue;
    }

    if (strcmp(argv[i], "--") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing program\n");
        return 1;
      }

      prog = argv[++i];
      prog_argv = argv + i;
      break;
    }

  }

  if (prog == NULL) {
    fprintf(stderr, "missing program\n");
    return 1;
  }

  char dst[64];
  snprintf(dst, sizeof(dst), "/run/user/%d/sandbox", getuid());

  if (mkdir(dst, 0755) == -1) {
    if (errno != EEXIST) {
      perror("mkdir error");
      return 1;
    }
  }

  mnt_dst = dst;

  void *p = malloc(STACK_SIZE);

  int ns_flags = CLONE_NEWPID |
                 CLONE_NEWUSER |
                 CLONE_NEWNS;

  int pid = clone(child,
                  p + STACK_SIZE,
                  ns_flags | SIGCHLD,
                  NULL,
                  NULL,
                  NULL);

  if (pid == -1) {
    perror("clone error");
    return 1;
  }

  waitpid(pid, NULL, 0);

  return 0;
}

