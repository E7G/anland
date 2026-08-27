/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-License-Identifier: GPL-2.0-or-later

    Native KWin output+input backend that talks to the Android display daemon
    directly (via libdisplay_producer), instead of running nested inside the
    weston "anland" compositor. Port of weston/libweston/backend-anland/anland.c
    to KWin's OutputBackend architecture.
*/
#pragma once

#include "core/outputbackend.h"

#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QPointF>
#include <QVector>
#include <cstdint>
#include <memory>
#include <sys/types.h>

extern "C" {
#include "display_producer.h"
#include "protocol.h"
}

#include "wayland/pointerconstraints_v1.h"
#include "wayland/surface.h"

class QSocketNotifier;
class QTimer;

namespace KWin
{

class AnlandOutput;
class AnlandInputDevice;
class BackendOutput;
class DrmDevice;
class EglBackend;
class EglDisplay;
class InputBackend;
class Window;

class KWIN_EXPORT AnlandBackend : public OutputBackend
{
    Q_OBJECT

public:
    explicit AnlandBackend(const QString &socketPath = QString(), QObject *parent = nullptr);
    ~AnlandBackend() override;

    bool initialize() override;

    std::unique_ptr<EglBackend> createOpenGLBackend() override;
    std::unique_ptr<InputBackend> createInputBackend() override;
    QList<CompositingType> supportedCompositors() const override;
    QList<BackendOutput *> outputs() const override;

    EglDisplay *sceneEglDisplayObject() const override;
    void setEglDisplay(std::unique_ptr<EglDisplay> &&display);

    display_ctx *display() const
    {
        return m_display;
    }
    DrmDevice *drmDevice() const
    {
        return m_drmDevice.get();
    }
    AnlandInputDevice *inputDevice() const
    {
        return m_inputDevice.get();
    }

    bool notifyFramePresented();

    /** Re-run the Workspace output layout after an output changed its mode at
     *  runtime (AnlandOutput::resize). The backend mutates the mode directly via
     *  setState() instead of going through OutputConfiguration, so — exactly like
     *  DrmBackend/VirtualBackend do after altering their output set — it must emit
     *  outputsQueried() itself. Otherwise Workspace::updateOutputs() never runs and
     *  windows keep their old geometry (the mode-changed signal alone does not
     *  trigger a relayout). */
    void notifyOutputsChanged()
    {
        Q_EMIT outputsQueried();
    }

private:
    void setupNotifiers();
    void teardownNotifiers();
    void onInputReadable();
    void onBufferReady();
    void processInputEvent(const InputEvent &ev);
    QPointF mapInputToLogical(const QPointF &devicePoint) const;
    void onReconnectTimer();
    void enterFallback();

    // Clipboard sync — bidirectional bridge between KWin selection / consumer
    void onClipboardChanged();
    void sendClipboardToConsumer(const QByteArray &text);
    void sendClipboardToKWin(const QByteArray &text);

    // Inject UTF-8 text from the consumer's IME into the focused KWin client.
    void sendTextInputToKWin(const QByteArray &text);

    // WSLg-style window metadata/control sideband. Rendering remains compatible
    // with the legacy full-output path until per-window dmabufs are negotiated.
    void setupWindowBridge();
    void trackWindow(Window *window);
    void untrackWindow(Window *window);
    void sendWindowEvent(Window *window, uint16_t action);
    void sendWindowFocus(Window *window);
    void resendWindowSnapshot();
    void handleWindowCommand(uint32_t windowId, uint32_t command);

    // Consumer-var bridge: force the Android app into pointer-capture (relative
    // mouse) mode while a Wayland client holds an active pointer constraint --
    // either a zwp_locked_pointer_v1 (native game pointer lock, and Xwayland's
    // hidden-cursor+confine emulation of it) or a zwp_confined_pointer_v1
    // (Xwayland visible-cursor confine, and native confinement). This overrides
    // the user's pointer_capture setting. The var is resent on every reconnect
    // because the consumer resets it to 0 on fallback.
    void setupMouseCaptureTracking();
    void updateMouseCaptureVar();
    void sendConsumerVar(uint32_t var, uint32_t value);

    // Foreground scheduling: the compositor's own subtree is boosted once per
    // connection (it is always "focused" while rendering); the focused
    // client's subtree is then reported on each activation change
    // (restore-previous before boost-next, one self-contained event each --
    // the cgroup files are the only state, the consumer tracks nothing).
    void setupSchedulingTracking();
    void updateActiveScheduling(bool force = false);
    void sendSchedulingEvent(pid_t pid, uint8_t flags);

    static void fallbackTrampoline(void *data);

    QString m_socketPath;
    display_ctx *m_display = nullptr;

    std::unique_ptr<DrmDevice> m_drmDevice;
    std::unique_ptr<EglDisplay> m_eglDisplay;
    QVector<AnlandOutput *> m_outputs;
    std::unique_ptr<AnlandInputDevice> m_inputDevice;

    QSocketNotifier *m_inputNotifier = nullptr;
    QSocketNotifier *m_bufReadyNotifier = nullptr;
    QTimer *m_reconnectTimer = nullptr;

    bool m_consumerReady = false;
    bool m_inFallback = false;

    // Last known clipboard text — used to de-duplicate (KWin changed -> we sent ->
    // consumer sets the same text on Android -> consumer sends back to KWin).
    // QByteArray is trivially sent over the data channel as UTF-8.
    QByteArray m_clipboardText;

    bool m_windowBridgeEnabled = false;
    uint32_t m_nextWindowId = 1;
    uint32_t m_windowEventSerial = 1;
    QHash<Window *, uint32_t> m_windowIds;
    QHash<uint32_t, QPointer<Window>> m_windowsById;

    // Active pointer-constraint tracking for CONSUMER_VAR_CAPTURE_MOUSE. We mirror
    // KWin's own updatePointerConstraints() triggers (window activation + the
    // focused surface's pointerConstraintsChanged + each constraint's own
    // lockedChanged/confinedChanged) to observe zwp_locked_pointer_v1 and
    // zwp_confined_pointer_v1 enable/disable without touching core input code.
    // The lock covers native pointer lock plus Xwayland's hidden-cursor+confine
    // emulation; the confine covers Xwayland visible-cursor confine and native
    // confinement. The QPointers auto-null when the KWin objects are destroyed,
    // and Qt auto-clears the connections to a destroyed QObject, so re-derivation
    // stays safe.
    QPointer<SurfaceInterface> m_captureMouseSurface;
    QPointer<LockedPointerV1Interface> m_captureMouseLock;
    QPointer<ConfinedPointerV1Interface> m_captureMouseConfined;
    QMetaObject::Connection m_captureMouseSurfaceConn;
    QMetaObject::Connection m_captureMouseLockConn;
    QMetaObject::Connection m_captureMouseConfinedConn;
    bool m_captureMouseActive = false; // last CONSUMER_VAR_CAPTURE_MOUSE value sent

    pid_t m_activeSchedulingPid = -1;
};

} // namespace KWin
