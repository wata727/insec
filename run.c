#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

char* concat(const char *s1, const char *s2) {
  char *ret = malloc(strlen(s1) + strlen(s2) + 1);
  if (ret == NULL) {
    perror("malloc");
    exit(1);
  }
  strcpy(ret, s1);
  strcat(ret, s2);
  return ret;
}

int rm_r(const char *path) {
  DIR *d = opendir(path);
  size_t path_len = strlen(path);
  if (d == NULL) {
    perror("opendir");
    return -1;
  }

  struct dirent *dir;
  errno = 0;
  while ((dir = readdir(d)) != NULL) {
    if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, "..")) {
      continue;
    }
    size_t len = path_len + strlen(dir->d_name) + 2;
    char *buf = malloc(len);
    if (buf == NULL) {
      perror("malloc");
      return -1;
    }
    struct stat statbuf;
    snprintf(buf, len, "%s/%s", path, dir->d_name);
    if (!stat(buf, &statbuf)){
      if (S_ISDIR(statbuf.st_mode)) {
        if (rm_r(buf) < 0) {
          return -1;
        }
      } else {
        if (unlink(buf) < 0) {
          perror("unlink");
          return -1;
        }
      }
    }
    free(buf);
  }
  if (errno) {
    perror("readdir");
    return -1;
  }
  if (closedir(d) < 0) {
    perror("closedir");
    return -1;
  }
  if (rmdir(path) < 0) {
    perror("rmdir");
    return -1;
  }
  return 0;
}

int run() {
  char template[] = "/tmp/insec-XXXXXX";
  char *dir = mkdtemp(template);
  if (dir == NULL) {
    perror("mkdtemp");
    return 1;
  }

  pid_t pid = (int)syscall(__NR_clone, CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD, NULL);
  if (pid < 0) {
    perror("clone");
    return 1;
  }

  if (pid == 0) {
    char *rd = concat(dir, "/root");
    char *ud = concat(dir, "/storage");
    char *wd = concat(dir, "/work");
    if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0) {
      perror("mount");
      return 1;
    }
    if (mkdir(rd, 0700) < 0) {
      perror("mkdir");
      return 1;
    }
    if (mkdir(ud, 0700) < 0) {
      perror("mkdir");
      return 1;
    }
    if (mkdir(wd, 0700) < 0) {
      perror("mkdir");
      return 1;
    }
    char *data = concat("upperdir=", ud);
    data = concat(data, ",lowerdir=/,workdir=");
    data = concat(data, wd);
    if (mount("overlayfs", rd, "overlay", MS_MGC_VAL, data) < 0) {
      perror("mount overlayfs");
      return 1;
    }
    char *dev = concat(rd, "/dev");
    if (mount("devtmpfs", dev, "devtmpfs", MS_MGC_VAL, "") < 0) {
      perror("mount devtmpfs");
      return 1;
    }
    if (mount("proc", "/proc", "proc", MS_MGC_VAL, "ro,nosuid,nodev,noexec") < 0) {
      perror("mount /proc ro");
      return 1;
    }
    char *proc = concat(rd, "/proc");
    if (mount("proc", proc, "proc", MS_MGC_VAL, "rw,nosuid,nodev,noexec,relatime") < 0) {
      perror("mount /proc rw");
      return 1;
    }
    char *proc_sys = concat(rd, "/proc/sys");
    if (mount("/proc/sys", proc_sys, NULL, MS_BIND, NULL) < 0) {
      perror("mount /proc/sys");
      return 1;
    }
    char *proc_sysrq_trigger = concat(rd, "/proc/sysrq-trigger");
    if (mount("/proc/sysrq-trigger", proc_sysrq_trigger, NULL, MS_BIND, NULL) < 0) {
      perror("mount /proc/sysrq-trigger");
      return 1;
    }
    char *proc_irq = concat(rd, "/proc/irq");
    if (mount("/proc/irq", proc_irq, NULL, MS_BIND, NULL) < 0) {
      perror("mount /proc/irq");
      return 1;
    }
    char *proc_bus = concat(rd, "/proc/bus");
    if (mount("/proc/bus", proc_bus, NULL, MS_BIND, NULL) < 0) {
      perror("mount /proc/bus");
      return 1;
    }
    char *sys = concat(rd, "/sys");
    if (mount("/sys", sys, NULL, MS_BIND, NULL) < 0) {
      perror("mount /sys");
      return 1;
    }
    if (chdir(rd) < 0) {
      perror("chdir");
      return 1;
    }
    if (mkdir(".orig", 0700) < 0) {
      perror("mkdir");
      return 1;
    }
    if (syscall(__NR_pivot_root, ".", ".orig") < 0) {
      perror("pivot_root");
      return 1;
    }
    if (mount(NULL, "/.orig", NULL, MS_PRIVATE | MS_REC, NULL) < 0) {
      perror("mount");
      return 1;
    }
    if (umount2("/.orig", MNT_DETACH) < 0) {
      perror("umount2");
      return 1;
    }
    if (chroot(".") < 0) {
      perror("chroot");
      return 1;
    }

    char *const argv[] = {
      "/bin/bash",
      NULL,
    };

    execv(argv[0], argv);
    return 0;
  }

  int status;
  if (waitpid(pid, &status, 0) < 0) {
    perror("waitpid");
    return 1;
  }
  if (rm_r(dir) < 0) {
    return 1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 1;
}
