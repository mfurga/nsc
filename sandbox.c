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

#define C_RED "\033[1;31m"
#define C_RST "\033[0m"

#define FATAL(x...) do { \
  printf(C_RED "[-]" C_RST " PROGRAM FATAL : " x); \
  printf("\nLocation : %s(), %s:%u\n\n", \
         __FUNCTION__, __FILE__, __LINE__); \
  exit(1); \
  } while (0)

#define PFATAL(x...) do {\
  printf(C_RED "[-]" C_RST " PROGRAM FATAL : " x); \
  printf("\nLocation : %s(), %s:%u\n", \
         __FUNCTION__, __FILE__, __LINE__); \
  printf("System message: %s\n\n", strerror(errno)); \
  exit(1); \
  } while (0)


char *mnt_src;
char *mnt_dst;

char *prog;
char **prog_argv;

void init_mount(void) {
  if (chdir("/") == -1) {
    PFATAL("chdir error");
  }

  if (mount("/", "/", NULL, MS_PRIVATE | MS_REC, NULL) == -1) {
    PFATAL("mount error");
  }

  if (mount(mnt_src, mnt_dst, NULL, MS_BIND |
                                    MS_REC |
                                    MS_PRIVATE |
                                    MS_RDONLY, NULL) == -1) {
    PFATAL("mount bind error");
  }

  if (chdir(mnt_dst) == -1) {
    PFATAL("chdir error");
  }

  if (chroot(".") == -1) {
    PFATAL("chroot error");
  }

  /* init procfs */

  if (mkdir("/proc", 0555) == -1) {
    if (errno != EEXIST) {
      PFATAL("mkdir error");
    }
  }

  if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
    PFATAL("mount proc error");
  }
}

int init_sandbox_and_run(void *arg) {
  (void)arg;

  /* init sandbox environment */
  init_mount();

  /* run program */
  execv(prog, prog_argv);

  /* never goes here! */
  PFATAL("execv error");
  return 1;
}

static void parse_program_args(int argc, char **argv) {
  for (int i = 0; i < argc; i++) {

    if (strcmp(argv[i], "--") == 0) {
      if (i + 1 >= argc) {
        FATAL("missing program argument");
      }

      prog = argv[++i];
      prog_argv = argv + i;
      break;
    }

    /* --chroot option */
    if (strcmp(argv[i], "--chroot") == 0) {
      if (i + 1 >= argc) {
        FATAL("missing --chroot argument");
      }

      mnt_src = argv[++i];
      continue;
    }

    /* show help */
    if (strcmp(argv[i], "-h") == 0 ||
        strcmp(argv[i], "--help") == 0) {

      printf(
        "Usage: %s [OPTIONS ...] -- [PROG] [PROG ARGS ...]\n\n"
        "OPTIONS:\n"
        "  --chroot: \n\n"
        "EXAMPLES:\n"
        "%s --chroot / -- /bin/sh -i\n\n",
        argv[0],
        argv[0]
      );

      exit(0);
    }
  }

  if (prog == NULL) {
    FATAL("missing program argument");
  }
}

void init_dirs(void) {
  mnt_dst = malloc(64);
  if (mnt_dst == NULL) {
    FATAL("malloc error: no memory?");
  }

  snprintf(mnt_dst, 64, "/run/user/%d/sandbox", getuid());

  if (mkdir(mnt_dst, 0755) == -1) {
    if (errno != EEXIST) {
      PFATAL("mkdir error");
    }
  }
}

int main(int argc, char **argv) {
  parse_program_args(argc, argv);
  
  init_dirs();

  void *p = malloc(STACK_SIZE);
  if (p == NULL) {
    FATAL("malloc error: no memory?");
  }

  int ns_flags = CLONE_NEWPID |
                 CLONE_NEWUSER |
                 CLONE_NEWNS;

  int pid = clone(init_sandbox_and_run,
                  p + STACK_SIZE,
                  ns_flags | SIGCHLD,
                  NULL,
                  NULL,
                  NULL);

  if (pid == -1) {
    PFATAL("clone error");
  }

  waitpid(pid, NULL, 0);

  return 0;
}
