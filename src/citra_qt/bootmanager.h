// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <QGLWidget>
#include <QImage>
#include <QThread>
#include <QTouchEvent>
#include "core/core.h"
#include "core/frontend/emu_window.h"

class QKeyEvent;
class QScreen;

class GGLWidgetInternal;
class GMainWindow;
class GRenderWindow;

class EmuThread : public QThread {
    Q_OBJECT

public:
    explicit EmuThread(GRenderWindow* render_window);

    /**
     * Start emulation (on new thread)
     * @warning Only call when not running!
     */
    void run() override;

    /**
     * Requests for the emulation thread to stop running
     */
    void RequestStop() {
        stop_run = true;
        Core::System::GetInstance().SetRunning(false);
    };

private:
    bool running{};
    std::atomic<bool> stop_run{false};

    GRenderWindow* render_window{};

signals:
    void ErrorThrown(Core::System::ResultStatus, const std::string&);
};

class GRenderWindow : public QWidget, public EmuWindow {
    Q_OBJECT

public:
    GRenderWindow(QWidget* parent, EmuThread* emu_thread);
    ~GRenderWindow();

    // EmuWindow implementation
    void SwapBuffers() override;
    void MakeCurrent() override;
    void DoneCurrent() override;
    void PollEvents() override;

    void BackupGeometry();
    void RestoreGeometry();
    void restoreGeometry(const QByteArray& geometry); // overridden
    QByteArray saveGeometry();                        // overridden

    qreal windowPixelRatio();

    void closeEvent(QCloseEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    void touchBeginEvent(QTouchEvent* event);
    void touchUpdateEvent(QTouchEvent* event);
    void touchEndEvent(QTouchEvent* event);

    bool event(QEvent* event) override;

    void focusOutEvent(QFocusEvent* event) override;

    void OnClientAreaResized(unsigned width, unsigned height);

    void InitRenderTarget();

    void CaptureScreenshot(u16 res_scale, const QString& screenshot_path);

public slots:
    void moveContext(); // overridden

    void OnEmulationStarting(EmuThread* emu_thread);
    void OnEmulationStopping();
    void OnFramebufferSizeChanged();

signals:
    /// Emitted when the window is closed
    void Closed();

private:
    void OnMinimalClientAreaChangeRequest(
        const std::pair<unsigned, unsigned>& minimal_size) override;

    GGLWidgetInternal* child{};

    QByteArray geometry;

    EmuThread* emu_thread{};

    /// Temporary storage of the screenshot taken
    QImage screenshot_image;

protected:
    void showEvent(QShowEvent* event) override;
};
