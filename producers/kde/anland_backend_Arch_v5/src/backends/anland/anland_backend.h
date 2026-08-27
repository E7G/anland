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

class QSocketNotifier;
class QTimer;

namespace KWin
{

class Window;

class AnlandOutput;
class AnlandEglLayer;
class AnlandInputDevice;
class AbstractDataSource;
class DrmDevice;
class EglDisplay;
class RenderDevice;
class InputBackend;
class OpenGLBackend;

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

    display_ctx *display() const
    {
        return m_display;
    }
    /**
     * DRM render device backing GL/EGL. KWin dereferences this during OpenGL
     * compositor setup (syncobj-timeline / dmabuf-feedback probing), so it must
     * be non-null; AnlandEglBackend::drmDevice() forwards to it.
     */
    DrmDevice *drmDevice() const;
    RenderDevice *renderDevice() const
    {
        return m_renderDevice.get();
    }
    AnlandInputDevice *inputDevice() const
    {
        return m_inputDevice.get();
    }

    /**
     * Called by AnlandEglBackend::present(): hand the freshly painted buffer to
     * the consumer (signals the render-done fence channel). Returns true if the buffer was
     * actually handed over (consumer was ready), false otherwise — the caller
     * uses this to decide how to complete the RenderLoop frame.
     */
    bool notifyFramePresented();

    /** True while the consumer fds and dmabufs are available for rendering. */
    bool isConsumerConnected() const;

    /** Import the current consumer dmabufs into a newly attached EGL layer. */
    bool importBuffers(AnlandEglLayer *layer);

    /** Re-run the Workspace output layout after an output changed its mode at
     *  runtime (AnlandOutput::resize). The backend mutates the mode directly via
     *  setState() instead of going through OutputConfiguration, so it must emit
     *  outputsQueried() itself; the mode-changed signal alone does not trigger a
     *  relayout. */
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
    QPointF mapInputDeltaToLogical(const QPointF &deviceDelta) const;
    void onReconnectTimer();
    void enterFallback();

    void onClipboardChanged(bool force = false);
    void sendClipboardToConsumer(const QByteArray &text);
    void sendClipboardToKWin(const QByteArray &text);
    void sendTextInputToKWin(const QByteArray &text);

    void setupWindowBridge();
    void trackWindow(Window *window);
    void untrackWindow(Window *window);
    void sendWindowEvent(Window *window, uint16_t action);
    void sendWindowFocus(Window *window);
    void resendWindowSnapshot();
    void handleWindowCommand(uint32_t windowId, uint32_t command);

    // Foreground scheduling: the compositor subtree is boosted once per
    // connection; focus changes restore the previous client before boosting
    // the next one with a self-contained event.
    void setupSchedulingTracking();
    void updateActiveScheduling(bool force = false);
    void sendSchedulingEvent(pid_t pid, uint8_t flags);

    static void fallbackTrampoline(void *data);

    QString m_socketPath;
    display_ctx *m_display = nullptr;

    std::unique_ptr<RenderDevice> m_renderDevice;
    QVector<AnlandOutput *> m_outputs;
    std::unique_ptr<AnlandInputDevice> m_inputDevice;

    QSocketNotifier *m_inputNotifier = nullptr;
    QSocketNotifier *m_bufReadyNotifier = nullptr;
    QTimer *m_reconnectTimer = nullptr;

    bool m_consumerReady = false;
    bool m_inFallback = false;
    QByteArray m_clipboardText;

    bool m_windowBridgeEnabled = false;
    uint32_t m_nextWindowId = 1;
    uint32_t m_windowEventSerial = 1;
    QHash<Window *, uint32_t> m_windowIds;
    QHash<uint32_t, QPointer<Window>> m_windowsById;
    std::unique_ptr<AbstractDataSource> m_clipboardSource;

    pid_t m_activeSchedulingPid = -1;
};

} // namespace KWin
