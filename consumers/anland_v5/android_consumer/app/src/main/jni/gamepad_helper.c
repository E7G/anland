#define _GNU_SOURCE
#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <limits.h>
#include <stddef.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define TAG "AnlandGamepadHelper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define DEVICE_NAME "Anland Virtual Xbox 360 Controller"
#define DROIDSPACES_PIDS "/data/local/Droidspaces/Pids"

enum {
    GP_A      = 1u << 0,
    GP_B      = 1u << 1,
    GP_X      = 1u << 2,
    GP_Y      = 1u << 3,
    GP_LB     = 1u << 4,
    GP_RB     = 1u << 5,
    GP_BACK   = 1u << 6,
    GP_START  = 1u << 7,
    GP_GUIDE  = 1u << 8,
    GP_L3     = 1u << 9,
    GP_R3     = 1u << 10,
};

struct gamepad_state {
    uint32_t buttons;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
    uint8_t lt;
    uint8_t rt;
    int8_t hat_x;
    int8_t hat_y;
} __attribute__((packed));

_Static_assert(sizeof(struct gamepad_state) == 16, "gamepad packet ABI must stay 16 bytes");

struct button_map {
    uint32_t bit;
    unsigned short code;
};

static const struct button_map button_maps[] = {
    {GP_A, BTN_SOUTH},
    {GP_B, BTN_EAST},
    {GP_X, BTN_NORTH},
    {GP_Y, BTN_WEST},
    {GP_LB, BTN_TL},
    {GP_RB, BTN_TR},
    {GP_BACK, BTN_SELECT},
    {GP_START, BTN_START},
    {GP_GUIDE, BTN_MODE},
    {GP_L3, BTN_THUMBL},
    {GP_R3, BTN_THUMBR},
};

static int emit_event(int fd, unsigned short type, unsigned short code, int value)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;

    ssize_t n;
    do {
        n = write(fd, &event, sizeof(event));
    } while (n < 0 && errno == EINTR);

    return n == (ssize_t)sizeof(event) ? 0 : -1;
}

static int setup_axis(int fd, int code)
{
    if (ioctl(fd, UI_SET_ABSBIT, code) < 0) {
        LOGE("UI_SET_ABSBIT(%d) failed: %s", code, strerror(errno));
        return -1;
    }
    return 0;
}

static int create_uinput_device(void)
{
    const char *paths[] = {"/dev/uinput", "/dev/input/uinput", NULL};
    int fd = -1;

    for (int i = 0; paths[i] != NULL; ++i) {
        fd = open(paths[i], O_WRONLY | O_CLOEXEC);
        if (fd >= 0) {
            LOGI("opened %s", paths[i]);
            break;
        }
    }

    if (fd < 0) {
        LOGE("cannot open uinput: %s", strerror(errno));
        return -1;
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0) {
        LOGE("failed to enable uinput event classes: %s", strerror(errno));
        close(fd);
        return -1;
    }

    for (size_t i = 0; i < sizeof(button_maps) / sizeof(button_maps[0]); ++i) {
        if (ioctl(fd, UI_SET_KEYBIT, button_maps[i].code) < 0) {
            LOGE("UI_SET_KEYBIT(%d) failed: %s",
                 button_maps[i].code, strerror(errno));
            close(fd);
            return -1;
        }
    }

    const int axes[] = {
        ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y
    };
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); ++i) {
        if (setup_axis(fd, axes[i]) < 0) {
            close(fd);
            return -1;
        }
    }

    struct uinput_user_dev device;
    memset(&device, 0, sizeof(device));
    snprintf(device.name, sizeof(device.name), "%s", DEVICE_NAME);

    // Xbox 360 USB IDs give SDL/Wine/Steam a well-known controller mapping.
    device.id.bustype = BUS_USB;
    device.id.vendor = 0x045e;
    device.id.product = 0x028e;
    device.id.version = 0x0114;

    device.absmin[ABS_X] = -32767;
    device.absmax[ABS_X] = 32767;
    device.absflat[ABS_X] = 1024;
    device.absmin[ABS_Y] = -32767;
    device.absmax[ABS_Y] = 32767;
    device.absflat[ABS_Y] = 1024;

    device.absmin[ABS_RX] = -32767;
    device.absmax[ABS_RX] = 32767;
    device.absflat[ABS_RX] = 1024;
    device.absmin[ABS_RY] = -32767;
    device.absmax[ABS_RY] = 32767;
    device.absflat[ABS_RY] = 1024;

    device.absmin[ABS_Z] = 0;
    device.absmax[ABS_Z] = 255;
    device.absmin[ABS_RZ] = 0;
    device.absmax[ABS_RZ] = 255;

    device.absmin[ABS_HAT0X] = -1;
    device.absmax[ABS_HAT0X] = 1;
    device.absmin[ABS_HAT0Y] = -1;
    device.absmax[ABS_HAT0Y] = 1;

    if (write(fd, &device, sizeof(device)) != (ssize_t)sizeof(device)) {
        LOGE("writing uinput device descriptor failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        LOGE("UI_DEV_CREATE failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOGI("created %s", DEVICE_NAME);
    return fd;
}

static int read_trimmed_file(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size < 2)
        return -1;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, out, out_size - 1);
    close(fd);
    if (n <= 0)
        return -1;

    out[n] = '\0';
    while (n > 0 &&
           (out[n - 1] == '\n' || out[n - 1] == '\r' ||
            out[n - 1] == ' ' || out[n - 1] == '\t')) {
        out[--n] = '\0';
    }
    return n > 0 ? 0 : -1;
}

static int find_event_node(char *path, size_t path_size)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        DIR *dir = opendir("/sys/class/input");
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "event", 5) != 0)
                    continue;

                char name_path[256];
                snprintf(name_path, sizeof(name_path),
                         "/sys/class/input/%s/device/name", entry->d_name);

                char name[128];
                if (read_trimmed_file(name_path, name, sizeof(name)) == 0 &&
                    strcmp(name, DEVICE_NAME) == 0) {
                    snprintf(path, path_size, "/dev/input/%s", entry->d_name);

                    struct stat st;
                    if (stat(path, &st) == 0 && S_ISCHR(st.st_mode)) {
                        LOGI("uinput event node: %s", path);
                        closedir(dir);
                        return 0;
                    }
                }
            }
            closedir(dir);
        }
        usleep(20000);
    }

    LOGE("could not resolve uinput event node");
    return -1;
}

static int sanitize_container_name(const char *name, char *out, size_t out_size)
{
    if (!name || !*name || !out || out_size < 2)
        return -1;

    size_t j = 0;
    for (size_t i = 0; name[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (j + 1 >= out_size)
            return -1;

        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.') {
            out[j++] = (char)c;
        } else if (c == ' ') {
            out[j++] = '-';
        } else {
            LOGE("unsafe Droidspaces container name");
            return -1;
        }
    }

    out[j] = '\0';
    return j > 0 ? 0 : -1;
}

static pid_t read_container_pid(const char *container_name)
{
    char safe_name[256];
    if (sanitize_container_name(container_name, safe_name, sizeof(safe_name)) < 0)
        return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pid", DROIDSPACES_PIDS, safe_name);

    char value[64];
    if (read_trimmed_file(path, value, sizeof(value)) < 0) {
        LOGE("Droidspaces pid file not found: %s", path);
        return -1;
    }

    char *end = NULL;
    long pid = strtol(value, &end, 10);
    if (end == value || pid <= 1 || pid > 4194304) {
        LOGE("invalid Droidspaces pid in %s", path);
        return -1;
    }
    return (pid_t)pid;
}

static int enter_mount_namespace(pid_t pid, int *host_ns_out)
{
    int host_ns = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    if (host_ns < 0) {
        LOGE("open host mount namespace failed: %s", strerror(errno));
        return -1;
    }

    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/ns/mnt", pid);
    int target_ns = open(path, O_RDONLY | O_CLOEXEC);
    if (target_ns < 0) {
        LOGE("open container mount namespace failed: %s", strerror(errno));
        close(host_ns);
        return -1;
    }

    if (setns(target_ns, CLONE_NEWNS) < 0) {
        LOGE("setns(container) failed: %s", strerror(errno));
        close(target_ns);
        close(host_ns);
        return -1;
    }

    close(target_ns);
    *host_ns_out = host_ns;
    return 0;
}

static int restore_mount_namespace(int host_ns)
{
    if (host_ns < 0)
        return -1;
    int rc = setns(host_ns, CLONE_NEWNS);
    if (rc < 0)
        LOGE("setns(host) failed: %s", strerror(errno));
    close(host_ns);
    return rc;
}

static int inject_event_node(pid_t container_pid,
                             const char *host_event_path,
                             char *container_event_path,
                             size_t container_event_path_size)
{
    struct stat host_st;
    if (stat(host_event_path, &host_st) < 0 || !S_ISCHR(host_st.st_mode)) {
        LOGE("stat(%s) failed: %s", host_event_path, strerror(errno));
        return -1;
    }

    const char *base = strrchr(host_event_path, '/');
    base = base ? base + 1 : host_event_path;
    if (strncmp(base, "event", 5) != 0) {
        LOGE("unexpected event node name: %s", base);
        return -1;
    }

    int host_ns = -1;
    if (enter_mount_namespace(container_pid, &host_ns) < 0)
        return -1;

    int rc = -1;
    do {
        if (mkdir("/dev/input", 0755) < 0 && errno != EEXIST) {
            LOGE("mkdir(/dev/input) failed: %s", strerror(errno));
            break;
        }

        snprintf(container_event_path, container_event_path_size,
                 "/dev/input/%s", base);

        struct stat existing;
        if (lstat(container_event_path, &existing) == 0) {
            if (S_ISCHR(existing.st_mode) && existing.st_rdev == host_st.st_rdev) {
                chmod(container_event_path, 0666);
                rc = 0;
                break;
            }
            LOGE("%s already exists with a different device number",
                 container_event_path);
            break;
        }

        if (mknod(container_event_path, S_IFCHR | 0666, host_st.st_rdev) < 0) {
            LOGE("mknod(%s) failed: %s", container_event_path, strerror(errno));
            break;
        }

        chmod(container_event_path, 0666);
        LOGI("injected %s into container pid %d",
             container_event_path, container_pid);
        rc = 0;
    } while (0);

    if (restore_mount_namespace(host_ns) < 0)
        return -1;
    return rc;
}

static void remove_event_node(pid_t container_pid, const char *container_event_path)
{
    if (container_pid <= 1 || !container_event_path || !*container_event_path)
        return;

    int host_ns = -1;
    if (enter_mount_namespace(container_pid, &host_ns) < 0)
        return;

    unlink(container_event_path);
    restore_mount_namespace(host_ns);
}

static int connect_abstract_socket(const char *name)
{
    if (!name || !*name)
        return -1;

    size_t name_len = strlen(name);
    if (name_len + 1 >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        LOGE("bridge socket name too long");
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        LOGE("socket(AF_UNIX) failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(addr.sun_path + 1, name, name_len);

    socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path)
                         + 1 + name_len);
    if (connect(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        LOGE("connect gamepad bridge failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static int read_full(int fd, void *buffer, size_t size)
{
    uint8_t *p = (uint8_t *)buffer;
    size_t done = 0;

    while (done < size) {
        ssize_t n = read(fd, p + done, size - done);
        if (n == 0)
            return 0;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        done += (size_t)n;
    }
    return 1;
}

static void emit_state(int uinput_fd,
                       const struct gamepad_state *old_state,
                       const struct gamepad_state *state)
{
    for (size_t i = 0; i < sizeof(button_maps) / sizeof(button_maps[0]); ++i) {
        int old_value = !!(old_state->buttons & button_maps[i].bit);
        int new_value = !!(state->buttons & button_maps[i].bit);
        if (old_value != new_value)
            emit_event(uinput_fd, EV_KEY, button_maps[i].code, new_value);
    }

#define EMIT_ABS_IF_CHANGED(field, code) \
    do { \
        if (old_state->field != state->field) \
            emit_event(uinput_fd, EV_ABS, code, state->field); \
    } while (0)

    EMIT_ABS_IF_CHANGED(lx, ABS_X);
    EMIT_ABS_IF_CHANGED(ly, ABS_Y);
    EMIT_ABS_IF_CHANGED(rx, ABS_RX);
    EMIT_ABS_IF_CHANGED(ry, ABS_RY);
    EMIT_ABS_IF_CHANGED(lt, ABS_Z);
    EMIT_ABS_IF_CHANGED(rt, ABS_RZ);
    EMIT_ABS_IF_CHANGED(hat_x, ABS_HAT0X);
    EMIT_ABS_IF_CHANGED(hat_y, ABS_HAT0Y);

#undef EMIT_ABS_IF_CHANGED

    emit_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        LOGE("usage: %s <abstract_bridge_name> [droidspaces_container]", argv[0]);
        return 1;
    }

    const char *bridge_name = argv[1];
    const char *container_name = argc >= 3 ? argv[2] : "";

    int uinput_fd = create_uinput_device();
    if (uinput_fd < 0)
        return 2;

    char host_event_path[PATH_MAX] = {0};
    if (find_event_node(host_event_path, sizeof(host_event_path)) < 0) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        return 3;
    }

    pid_t container_pid = -1;
    char container_event_path[PATH_MAX] = {0};
    if (container_name[0] != '\0') {
        container_pid = read_container_pid(container_name);
        if (container_pid <= 1 ||
            inject_event_node(container_pid, host_event_path,
                              container_event_path,
                              sizeof(container_event_path)) < 0) {
            LOGE("failed to expose virtual gamepad to Droidspaces container '%s'",
                 container_name);
            ioctl(uinput_fd, UI_DEV_DESTROY);
            close(uinput_fd);
            return 4;
        }
    }

    int bridge_fd = connect_abstract_socket(bridge_name);
    if (bridge_fd < 0) {
        remove_event_node(container_pid, container_event_path);
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
        return 5;
    }

    struct gamepad_state old_state;
    memset(&old_state, 0, sizeof(old_state));

    for (;;) {
        struct gamepad_state state;
        int rc = read_full(bridge_fd, &state, sizeof(state));
        if (rc <= 0)
            break;

        emit_state(uinput_fd, &old_state, &state);
        old_state = state;
    }

    struct gamepad_state neutral;
    memset(&neutral, 0, sizeof(neutral));
    emit_state(uinput_fd, &old_state, &neutral);

    close(bridge_fd);
    remove_event_node(container_pid, container_event_path);
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
    LOGI("virtual gamepad stopped");
    return 0;
}
