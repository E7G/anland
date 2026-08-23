package com.anland.consumer;

import android.content.Context;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

/**
 * Root-backed bridge for the optional on-screen gamepad.
 *
 * The Android UI sends a compact 16-byte state packet to a tiny root helper.
 * The helper owns /dev/uinput and creates a single evdev controller, then exposes
 * only that event node inside the selected Droidspaces container.
 */
public final class GamepadBridge {
    private static final String TAG = "AnlandGamepad";
    private static final Object LOCK = new Object();
    private static final byte[] NEUTRAL = new byte[16];

    private static LocalServerSocket server;
    private static LocalSocket client;
    private static OutputStream output;
    private static Process helper;
    private static Thread worker;
    private static String targetContainer = "";
    private static byte[] lastPacket = NEUTRAL.clone();
    private static int generation = 0;
    private static volatile String lastError = "";

    private GamepadBridge() {}

    public static void start(Context context, String containerName) {
        final Context app = context.getApplicationContext();
        final String target = containerName == null ? "" : containerName.trim();

        synchronized (LOCK) {
            if (worker != null && worker.isAlive() && target.equals(targetContainer))
                return;

            stopLocked();
            final int myGeneration = ++generation;
            targetContainer = target;
            lastPacket = NEUTRAL.clone();
            lastError = "";
            worker = new Thread(() -> runBridge(app, target, myGeneration),
                    "anland-gamepad-bridge");
            worker.setDaemon(true);
            worker.start();
        }
    }

    private static void runBridge(Context context, String containerName, int myGeneration) {
        String bridgeName = "anland_gamepad_" + android.os.Process.myPid()
                + "_" + Long.toUnsignedString(System.nanoTime());
        LocalServerSocket localServer = null;
        Process localHelper = null;

        try {
            localServer = new LocalServerSocket(bridgeName);
            synchronized (LOCK) {
                if (myGeneration != generation) {
                    localServer.close();
                    return;
                }
                server = localServer;
            }

            String helperPath = context.getApplicationInfo().nativeLibraryDir
                    + "/libgamepadhelper.so";
            String command = shellQuote(helperPath) + " "
                    + shellQuote(bridgeName) + " " + shellQuote(containerName);
            localHelper = new ProcessBuilder("su", "-c", command)
                    .redirectErrorStream(true)
                    .start();

            synchronized (LOCK) {
                if (myGeneration != generation) {
                    localHelper.destroy();
                    localServer.close();
                    return;
                }
                helper = localHelper;
            }

            final Process logProcess = localHelper;
            Thread logger = new Thread(() -> drainHelperLog(logProcess),
                    "anland-gamepad-helper-log");
            logger.setDaemon(true);
            logger.start();

            final Process watchedHelper = localHelper;
            final LocalServerSocket watchedServer = localServer;
            Thread watcher = new Thread(() -> {
                try {
                    int rc = watchedHelper.waitFor();
                    if (rc != 0) {
                        lastError = "virtual gamepad helper exited with code " + rc;
                        Log.w(TAG, lastError);
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                } finally {
                    synchronized (LOCK) {
                        if (myGeneration == generation && client == null) {
                            try {
                                watchedServer.close();
                            } catch (Exception ignored) {}
                        }
                    }
                }
            }, "anland-gamepad-helper-watch");
            watcher.setDaemon(true);
            watcher.start();

            LocalSocket accepted = localServer.accept();
            OutputStream acceptedOut = accepted.getOutputStream();

            synchronized (LOCK) {
                if (myGeneration != generation) {
                    accepted.close();
                    return;
                }
                client = accepted;
                output = acceptedOut;
                writePacketLocked(lastPacket);
                lastError = "";
                Log.i(TAG, "virtual gamepad connected"
                        + (containerName.isEmpty() ? "" : " for " + containerName));
            }
        } catch (Exception e) {
            synchronized (LOCK) {
                if (myGeneration == generation) {
                    lastError = "virtual gamepad start failed: " + e;
                    Log.w(TAG, lastError);
                }
            }
        } finally {
            // The live server/client/process are owned by the static fields and are
            // closed by stop(). Only close a local object if this worker lost the race.
            synchronized (LOCK) {
                if (myGeneration != generation) {
                    closeQuietly(localServer);
                    if (localHelper != null) localHelper.destroy();
                }
            }
        }
    }

    private static void drainHelperLog(Process process) {
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(process.getInputStream()))) {
            String line;
            while ((line = reader.readLine()) != null)
                Log.i(TAG, "helper: " + line);
        } catch (Exception ignored) {}
    }

    public static void sendState(int buttons,
                                 int lx, int ly, int rx, int ry,
                                 int lt, int rt, int hatX, int hatY) {
        ByteBuffer b = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(buttons);
        b.putShort(clampAxis(lx));
        b.putShort(clampAxis(ly));
        b.putShort(clampAxis(rx));
        b.putShort(clampAxis(ry));
        b.put((byte) clamp(lt, 0, 255));
        b.put((byte) clamp(rt, 0, 255));
        b.put((byte) clamp(hatX, -1, 1));
        b.put((byte) clamp(hatY, -1, 1));
        byte[] packet = b.array();

        synchronized (LOCK) {
            if (Arrays.equals(packet, lastPacket))
                return;
            lastPacket = packet;
            writePacketLocked(packet);
        }
    }

    public static void neutralize() {
        synchronized (LOCK) {
            lastPacket = NEUTRAL.clone();
            writePacketLocked(lastPacket);
        }
    }

    public static void stop() {
        synchronized (LOCK) {
            stopLocked();
            generation++;
        }
    }

    private static void stopLocked() {
        if (output != null) {
            try {
                output.write(NEUTRAL);
                output.flush();
            } catch (Exception ignored) {}
        }

        closeQuietly(client);
        client = null;
        output = null;

        closeQuietly(server);
        server = null;

        if (helper != null) {
            helper.destroy();
            helper = null;
        }

        worker = null;
        targetContainer = "";
        lastPacket = NEUTRAL.clone();
    }

    public static boolean isConnected() {
        synchronized (LOCK) {
            return client != null && output != null;
        }
    }

    public static String getLastError() {
        return lastError;
    }

    private static void writePacketLocked(byte[] packet) {
        if (output == null)
            return;
        try {
            output.write(packet);
            output.flush();
        } catch (Exception e) {
            lastError = "virtual gamepad write failed: " + e;
            Log.w(TAG, lastError);
            closeQuietly(client);
            client = null;
            output = null;
        }
    }

    private static short clampAxis(int v) {
        return (short) clamp(v, -32767, 32767);
    }

    private static int clamp(int v, int min, int max) {
        return Math.max(min, Math.min(max, v));
    }

    private static String shellQuote(String value) {
        if (value == null || value.isEmpty())
            return "''";
        return "'" + value.replace("'", "'\\''") + "'";
    }

    private static void closeQuietly(Object object) {
        if (object == null) return;
        try {
            if (object instanceof LocalSocket)
                ((LocalSocket) object).close();
            else if (object instanceof LocalServerSocket)
                ((LocalServerSocket) object).close();
        } catch (Exception ignored) {}
    }
}
