package com.anland.consumer;

import java.util.Collection;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** In-process mirror of the KWin top-level tree for the WSLg bridge. */
public final class LinuxWindowRegistry {
    public static final int CREATE = 1;
    public static final int UPDATE = 2;
    public static final int DESTROY = 3;
    public static final int FOCUS = 4;
    public static final int FLAG_ACTIVE = 0x0001;

    public static final class WindowInfo {
        public final int id;
        public int flags, x, y, width, height, pid;
        public String title = "";
        public String appId = "";
        WindowInfo(int id) { this.id = id; }
    }

    private final Map<Integer, WindowInfo> windows = new LinkedHashMap<>();
    private int activeId;

    public synchronized WindowInfo apply(int id, int action, int flags,
                                         int x, int y, int width, int height, int pid,
                                         String title, String appId) {
        if (action == DESTROY) {
            WindowInfo old = windows.remove(id);
            if (activeId == id) activeId = 0;
            return old;
        }
        WindowInfo w = windows.get(id);
        if (w == null) {
            w = new WindowInfo(id);
            windows.put(id, w);
        }
        if (action == CREATE || action == UPDATE) {
            w.flags = flags;
            w.x = x; w.y = y; w.width = width; w.height = height; w.pid = pid;
            if (title != null) w.title = title;
            if (appId != null) w.appId = appId;
        }
        if (action == FOCUS || (flags & FLAG_ACTIVE) != 0) {
            activeId = id;
            w.flags |= FLAG_ACTIVE;
            for (WindowInfo other : windows.values()) {
                if (other.id != id) other.flags &= ~FLAG_ACTIVE;
            }
        }
        return w;
    }

    public synchronized WindowInfo active() { return windows.get(activeId); }
    public synchronized WindowInfo get(int id) { return windows.get(id); }
    public synchronized Collection<WindowInfo> snapshot() {
        return Collections.unmodifiableCollection(new java.util.ArrayList<>(windows.values()));
    }
    public synchronized void clear() { windows.clear(); activeId = 0; }
}
