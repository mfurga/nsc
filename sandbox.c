#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "debug.h"

/* config */
#define STACK_SIZE            4096
#define MAP_BUF_SIZE          64 
#define PATH_BUF_SIZE         128
#define MAP_SIZE              16

typedef struct {
  struct {
    int inside;
    int outside;
  } map[MAP_SIZE];
  int idx;
} id_map_t;

char *mnt_src;
char *mnt_dst;

char *prog;
char **prog_argv;

int child_pid;
int pipefd[2];

id_map_t uid_map;
id_map_t gid_map;

static void id_map_append(id_map_t *id_map, int inside, int outside) {
  if (id_map->idx >= MAP_SIZE) {
    FATAL("error");
  }

  id_map->map[id_map->idx].inside = inside;
  id_map->map[id_map->idx].outside = outside;
  id_map->idx++;
}

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

  /* close write end of the pipe */
  close(pipefd[1]);

  /* wait until parent has updated the user IDs mappings */
  char ch;
  if (read(pipefd[0], &ch, 1) != 0) {
    FATAL("read error");
  }

  /* close read end of the pipe */
  close(pipefd[0]);

  /* run program */
  execv(prog, prog_argv);

  /* never goes here! */
  PFATAL("execv error");
  return 1;
}


void init_dirs(void) {
  mnt_dst = malloc(PATH_BUF_SIZE);
  if (mnt_dst == NULL) {
    FATAL("malloc error: no memory?");
  }

  snprintf(mnt_dst, PATH_BUF_SIZE, "/run/user/%d/sandbox", getuid());

  if (mkdir(mnt_dst, 0755) == -1) {
    if (errno != EEXIST) {
      PFATAL("mkdir error");
    }
  }
}

static void proc_setgroups_write(char *str) {
  char setgroups_file[PATH_BUF_SIZE];
  int fd;

  snprintf(setgroups_file, PATH_BUF_SIZE, "/proc/%d/setgroups", child_pid);

  fd = open(setgroups_file, O_RDWR);
  if (fd == -1) {

    /* We may be on a system that doesn't support
    /proc/PID/setgroups. In that case, the file won't exist,
    and the system won't impose the restrictions that Linux 3.19
    added. That's fine: we don't need to do anything in order
    to permit 'gid_map' to be updated.

    However, if the error from open() was something other than
    the ENOENT error that is expected for that case,  let the
    user know. */
    if (errno != ENOENT) {
      PFATAL("Failed to open %s", setgroups_file);
    }
  }

  if (write(fd, str, strlen(str)) == -1) {
    PFATAL("Failed to write %s", setgroups_file);
  }

  close(fd);
}

static void map_user(id_map_t *id_map, const char *map_file) {
  char map_buf[MAP_BUF_SIZE];
  size_t sz = 0;

  for (int i = 0; i < id_map->idx; i++) {
    sz += snprintf(map_buf + sz,
                   sizeof(map_buf) - sz,
                   "%d %d 1\n",
                   id_map->map[i].inside,
                   id_map->map[i].outside);
  }

  int fd = open(map_file, O_RDWR);
  if (fd == -1) {
    PFATAL("open error");
  }

  if (write(fd, map_buf, strlen(map_buf)) != (int)strlen(map_buf)) {
    close(pipefd[1]);
    kill(child_pid, SIGKILL);
    PFATAL("write error");
  }

  close(fd);
}

static void init_user_from_parent(void) {
  char uid_map_file[PATH_BUF_SIZE];
  char gid_map_file[PATH_BUF_SIZE];

  snprintf(uid_map_file, sizeof(uid_map_file), "/proc/%d/uid_map", child_pid);
  snprintf(gid_map_file, sizeof(gid_map_file), "/proc/%d/gid_map", child_pid);

  map_user(&uid_map, uid_map_file);
  proc_setgroups_write("deny");
  map_user(&gid_map, gid_map_file);
}

static void usage(const char *argv0) {
  printf(
    "\n%1$s [OPTIONS ...] -- /path/to/app [args ...]\n\n"

    "OPTIONS:\n"
    " -u in:out, --user in:out        - set user IDs mapping\n"
    " -g in:out, --group in:out       - set groups IDs mapping\n\n"

    "EXAMPLES:\n"
    "%1$s --chroot / -- /bin/sh -i\n\n",
    argv0
  );

  exit(1);
}

int main(int argc, char **argv) {
  int opt;
  int inside, outside;

  static const struct option opts[] = {
    { .name = "user",   .has_arg = 1, NULL, 'u' },
    { .name = "group",  .has_arg = 1, NULL, 'g' },
    { .name = "chroot", .has_arg = 1, NULL, 'c' },
    { .name = "help",   .has_arg = 0, NULL, 'h' },
    { NULL, 0, NULL, 0}
  };

  while ((opt = getopt_long(argc, argv, "+u:g:c:h", opts, NULL)) != -1) {
    switch (opt) {
      case 'u':
        if (sscanf(optarg, "%d:%d", &inside, &outside) != 2) {
          FATAL("Invalid user IDs mapping format");
        }

        id_map_append(&uid_map, inside, outside);
        break;

      case 'g':
        if (sscanf(optarg, "%d:%d", &inside, &outside) != 2) {
          FATAL("Invalid group IDs mapping format");
        }

        id_map_append(&gid_map, inside, outside);
        break;

      case 'c':
        mnt_src = optarg;
        break;

      case 'h':
        /* fall-through */
      default:
        usage(argv[0]);
        break;
    }
  }

  if (optind == argc) {
    usage(argv[0]);
  }

  prog = argv[optind];
  prog_argv = argv + optind;

  init_dirs();

  if (pipe(pipefd) == -1) {
    PFATAL("pipe error");
  }

  void *p = malloc(STACK_SIZE);
  if (p == NULL) {
    FATAL("malloc error: no memory?");
  }

  int ns_flags = CLONE_NEWPID |
                 CLONE_NEWUSER |
                 CLONE_NEWNS;

  child_pid = clone(init_sandbox_and_run,
                    p + STACK_SIZE,
                    ns_flags | SIGCHLD,
                    NULL,
                    NULL,
                    NULL);

  if (child_pid == -1) {
    PFATAL("clone error");
  }

  /* close read end of the pipe */
  close(pipefd[0]);

  init_user_from_parent();

  /* continue execution of the child process */
  close(pipefd[1]);

  waitpid(child_pid, NULL, 0);

  return 0;
}
