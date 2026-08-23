/*
 * settopapp — a tiny root helper for the "foreground scheduling" feature.
 *
 * When the producer (the Linux desktop compositor) connects to the display
 * daemon, the consumer wants the whole producer process tree moved into the
 * Android "top-app" cpuset/schedtune groups so it gets foreground CPU priority
 * while it renders. The consumer cannot see the producer's pid directly: its
 * data/fence/audio sockets are socketpairs the consumer itself created and
 * passed along via SCM_RIGHTS, and a socketpair's SO_PEERCRED reports the
 * creator, not whoever holds the other end now.
 *
 * So the reverse lookup runs as root here instead, via NETLINK_INET_DIAG
 * (UNIX_DIAG): given the *inode* of the consumer's end of the data socketpair
 * (the consumer reads it from /proc/self/fd before exec'ing this helper), the
 * kernel reports the peer socket's inode; scanning /proc for a process whose
 * fds include that inode
 * finds the producer process holding the other end. From there we walk up to
 * the root of the producer's own process tree (stopping at container/superuser
 * boundaries so we never touch init, zygote or the su chain), then move that
 * whole tree into the top-app groups.
 *
 *   usage: libsettopapp.so <data_socket_inode>            -> move tree to top-app
 *          libsettopapp.so <pid> restore                  -> move tree back to "/"
 *
 * After a successful move the helper prints the tree-root pid on stdout, so the
 * consumer can cache it and later restore with "<pid> restore" without doing
 * the lookup again.
 *
 * It is shipped inside the APK as lib*.so so Android extracts it into the app's
 * nativeLibraryDir with execute permission.
 */
#define _GNU_SOURCE
#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
/* The NDK sysroot ships only a subset of the kernel UAPI headers and lacks
 * socket_diag.h (and, on some releases, unix_diag.h). Both are trivial stable
 * UAPI definitions, so fall back to inline copies when they are missing. */
#if __has_include(<linux/socket_diag.h>)
#include <linux/socket_diag.h>
#else
#define SOCK_DIAG_BY_FAMILY 20
#endif
#if __has_include(<linux/unix_diag.h>)
#include <linux/unix_diag.h>
#else
struct unix_diag_req {
    __u8    sdiag_family;
    __u8    pad;
    __u16   udiag_states;
    __u32   udiag_ino;
    __u32   udiag_show;
    __u32   udiag_cookie[2];
};
struct unix_diag_msg {
    __u8    udiag_family;
    __u8    udiag_type;
    __u8    udiag_state;
    __u8    udiag_pad;
    __u32   udiag_cookie[2];
    __u32   udiag_ino;
    __u32   udiag_peer;
    __u32   udiag_rqueue;
    __u32   udiag_wqueue;
    __u32   udiag_cookie2[0];
};
#define UNIX_DIAG_PEER    3
#define UDIAG_SHOW_PEER   0x00000010
#endif

#define TAG "AnlandSetTopApp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

/* Tree capacity: a desktop session (kwin + plasmashell + helpers) is a few
 * dozen processes; allow generous headroom for Wayland clients. */
#define MAX_PIDS 512

static int write_file(const char *path, const char *content)
{
    /* O_APPEND so repeated pid writes accumulate in the file (real cgroupfs
     * treats each write as a command and ignores the offset, but the flag
     * makes the helper's behaviour identical against a plain file too). */
    int fd = open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t len = (ssize_t)strlen(content);
    ssize_t n = write(fd, content, (size_t)len);
    close(fd);
    return (n == len) ? 0 : -1;
}

/* Append "pid\n" to a cgroup.procs file. */
static int write_cgroup_proc(const char *group_procs, pid_t pid)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", (int)pid);
    return write_file(group_procs, buf) == 0 && len > 0 ? 0 : -1;
}

/* True if pid is already in pids[0..n). */
static bool pid_seen(const pid_t *pids, int n, pid_t pid)
{
    for (int i = 0; i < n; i++)
        if (pids[i] == pid)
            return true;
    return false;
}

/* Depth-first walk of the process tree rooted at root, collecting every pid
 * (root included) exactly once. The tree is snapshotted before any cgroup
 * write: a child that dies or reparents mid-walk is skipped here but is not
 * left boosted either (its cgroup membership dies with it). Returns the
 * count, or -1 if the table overflowed. */
static int collect_tree(pid_t root, pid_t *pids, int max_pids)
{
    int n = 0;
    int stack[MAX_PIDS];
    int top = 0;
    stack[top++] = root;

    while (top > 0) {
        pid_t cur = stack[--top];
        if (pid_seen(pids, n, cur))
            continue;
        pids[n++] = cur;
        if (n >= max_pids)
            return -1;

        /* children of every thread of cur; dedup against both the collected
         * table and the pending stack entries, else a grandchild gets pushed
         * twice and the second pop is (correctly, but wastefully) skipped */
        char tpath[48];
        snprintf(tpath, sizeof(tpath), "/proc/%d/task", (int)cur);
        DIR *d = opendir(tpath);
        if (!d)
            continue;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (de->d_name[0] < '0' || de->d_name[0] > '9')
                continue;
            char cpath[128];
            snprintf(cpath, sizeof(cpath), "/proc/%d/task/%s/children",
                     (int)cur, de->d_name);
            FILE *f = fopen(cpath, "r");
            if (!f)
                continue;
            int child;
            while (fscanf(f, "%d", &child) == 1) {
                if (child <= 0 || pid_seen(pids, n, (pid_t)child))
                    continue;
                if (n >= max_pids || top >= MAX_PIDS) {
                    fclose(f);
                    closedir(d);
                    return -1;
                }
                stack[top++] = (pid_t)child;
            }
            fclose(f);
        }
        closedir(d);
    }
    return n;
}

/* Read /proc/<pid>/stat and return the parent pid (field 4). The comm field
 * (2nd) is parenthesised and may contain spaces or parens itself, so locate
 * the LAST ')' first and parse after it. Returns 0 on failure. */
static pid_t get_ppid(pid_t pid)
{
    char path[48];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    char line[512];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return 0;
    }
    fclose(f);

    char *rp = strrchr(line, ')');
    if (!rp)
        return 0;
    /* fields after comm: state ppid ... */
    char state_c = 0;
    int ppid = 0;
    if (sscanf(rp + 1, " %c %d", &state_c, &ppid) != 2)
        return 0;
    return (pid_t)ppid;
}

/* Names whose subtrees we never enter when walking up: they are container
 * init processes / Android framework roots / the su chain itself. Stopping
 * here keeps the moved tree exactly the producer session. */
static const char *const stop_names[] = {
    "init", "systemd", "zygote", "zygote64", "app_process",
    "magisk", "magiskd", "su", "daemonsu", "supersu",
    NULL
};

/* True when `pid` itself carries one of the stop names (comm, 15 chars max). */
static bool is_stop_name(pid_t pid)
{
    char path[48];
    snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char comm[24];
    bool hit = false;
    if (fgets(comm, sizeof(comm), f)) {
        char *nl = strchr(comm, '\n');
        if (nl) *nl = '\0';
        for (const char *const *p = stop_names; *p; p++)
            if (strcmp(comm, *p) == 0) { hit = true; break; }
    }
    fclose(f);
    return hit;
}

/* Walk up from `pid` to the highest ancestor that is still part of the
 * producer's own session (stops before init/systemd/zygote/su and friends).
 * Guards against loops (broken /proc) with a depth cap. */
static pid_t find_tree_root(pid_t pid)
{
    pid_t cur = pid;
    for (int depth = 0; depth < 32; depth++) {
        pid_t ppid = get_ppid(cur);
        if (ppid <= 1 || is_stop_name(ppid))
            return cur;
        cur = ppid;
    }
    return cur;
}

/*
 * NETLINK_INET_DIAG (UNIX_DIAG) lookup: given the inode of our end of the
 * data socketpair, find the peer socket's inode. AF_UNIX diag has no exact
 * get-by-inode op (a plain request returns NLMSG_ERROR/-ESTALE), so like ss(1)
 * we dump every unix socket with UDIAG_SHOW_PEER and match udiag_ino locally.
 * Returns the peer inode, or 0 on failure.
 */
static unsigned long long peer_inode_of(unsigned long long inode)
{
    struct {
        struct nlmsghdr nlh;
        struct unix_diag_req udr;
    } req;
    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(req.udr));
    req.nlh.nlmsg_type  = SOCK_DIAG_BY_FAMILY;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq   = 1;
    req.udr.sdiag_family = AF_UNIX;
    req.udr.udiag_states = ~0U;   /* all states; 0 matches nothing */
    req.udr.udiag_show   = UDIAG_SHOW_PEER;

    int nl = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_INET_DIAG);
    if (nl < 0) {
        LOGE("netlink socket failed: %s", strerror(errno));
        return 0;
    }

    if (send(nl, &req, req.nlh.nlmsg_len, 0) < 0) {
        LOGE("netlink send failed: %s", strerror(errno));
        close(nl);
        return 0;
    }

    unsigned long long peer = 0;
    char buf[8192];
    while (!peer) {
        ssize_t n = recv(nl, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        for (struct nlmsghdr *h = (struct nlmsghdr *)buf;
             NLMSG_OK(h, (unsigned)n); h = NLMSG_NEXT(h, n)) {
            if (h->nlmsg_type == NLMSG_DONE)
                goto out;
            if (h->nlmsg_type == NLMSG_ERROR) {
                LOGE("netlink error reply");
                goto out;
            }
            struct unix_diag_msg *m = NLMSG_DATA(h);
            if (m->udiag_ino != (__u32)inode)
                continue;
            int len = h->nlmsg_len - NLMSG_LENGTH(sizeof(*m));
            struct rtattr *ra = (struct rtattr *)(m + 1);
            while (RTA_OK(ra, len)) {
                if (ra->rta_type == UNIX_DIAG_PEER) {
                    peer = *(unsigned int *)RTA_DATA(ra);
                    break;
                }
                ra = RTA_NEXT(ra, len);
            }
        }
    }
out:
    close(nl);
    return peer;
}

/*
 * Scan the fd tables of every process for one holding open a socket with the
 * given inode.
 * Returns the pid, or 0 if none (or on error).
 */
static pid_t holder_of_inode(unsigned long long inode)
{
    char target[64];
    snprintf(target, sizeof(target), "socket:[%llu]", inode);

    DIR *proc = opendir("/proc");
    if (!proc)
        return 0;

    pid_t found = 0;
    struct dirent *de;
    while ((de = readdir(proc)) && !found) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        int pid = atoi(de->d_name);
        if (pid == getpid())
            continue;

        char fdpath[48];
        snprintf(fdpath, sizeof(fdpath), "/proc/%d/fd", pid);
        DIR *fd = opendir(fdpath);
        if (!fd)
            continue;   /* not ours to read (kernel threads, other UIDs...) */
        struct dirent *fde;
        while ((fde = readdir(fd))) {
            if (fde->d_name[0] == '.')
                continue;
            char linkpath[128], link[128];
            snprintf(linkpath, sizeof(linkpath), "/proc/%d/fd/%s", pid, fde->d_name);
            ssize_t l = readlink(linkpath, link, sizeof(link) - 1);
            if (l <= 0)
                continue;
            link[l] = '\0';
            if (strcmp(link, target) == 0) {
                found = (pid_t)pid;
                break;
            }
        }
        closedir(fd);
    }
    closedir(proc);
    return found;
}

/* Move every pid of the tree into the two top-app groups (or back to the root
 * groups when group paths are the "/" ones). Errors on individual pids are
 * logged and skipped (a process may have died between collect and write). */
static int move_tree(pid_t root, const char *cpu_procs, const char *cpuset_procs,
                     const char *what)
{
    pid_t pids[MAX_PIDS];
    int n = collect_tree(root, pids, MAX_PIDS);
    if (n <= 0) {
        LOGE("%s: collect_tree(%d) failed (%d)", what, (int)root, n);
        return -1;
    }
    LOGI("%s: %d processes from root %d", what, n, (int)root);

    int moved = 0;
    for (int i = 0; i < n; i++) {
        int ok = 0;
        if (write_cgroup_proc(cpu_procs, pids[i]) == 0)
            ok++;
        if (write_cgroup_proc(cpuset_procs, pids[i]) == 0)
            ok++;
        if (ok > 0)
            moved++;
        else
            LOGI("%s: pid %d skipped (dead?)", what, (int)pids[i]);
    }

    /* Processes forked after the snapshot inherit their parent's cgroup, so
     * they follow the move; nothing left to sweep. */
    LOGI("%s: moved %d/%d processes", what, moved, n);
    return moved > 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        LOGE("usage: %s <data_socket_inode>", argv[0]);
        LOGE("       %s <pid> restore", argv[0]);
        return 1;
    }

    if (argc >= 3 && strcmp(argv[2], "restore") == 0) {
        pid_t root = (pid_t)atoi(argv[1]);
        if (root <= 0) {
            LOGE("restore: bad pid '%s'", argv[1]);
            return 1;
        }
        /* "/" (the root cgroup of each hierarchy) == the top of the mounted
         * controller: /dev/cpuctl/cgroup.procs and /dev/cpuset/cgroup.procs. */
        int rc = move_tree(root, "/dev/cpuctl/cgroup.procs",
                           "/dev/cpuset/cgroup.procs", "restore");
        if (rc < 0)
            return 2;
        printf("%d\n", (int)root);
        return 0;
    }

    unsigned long long inode = strtoull(argv[1], NULL, 10);
    if (inode == 0) {
        LOGE("bad inode '%s'", argv[1]);
        return 1;
    }

    unsigned long long peer = peer_inode_of(inode);
    if (!peer) {
        LOGE("no peer socket found for inode %llu (diag unavailable?)", inode);
        return 3;
    }

    pid_t producer = holder_of_inode(peer);
    if (!producer) {
        LOGE("no process holds peer socket inode %llu", peer);
        return 4;
    }

    pid_t root = find_tree_root(producer);
    LOGI("producer pid %d, tree root %d", (int)producer, (int)root);

    if (move_tree(root, "/dev/cpuctl/top-app/cgroup.procs",
                  "/dev/cpuset/top-app/cgroup.procs", "top-app") < 0)
        return 5;

    /* Hand the tree root back so the consumer can restore without a second
     * diag round-trip (the producer may be gone by then). */
    printf("%d\n", (int)root);
    return 0;
}
