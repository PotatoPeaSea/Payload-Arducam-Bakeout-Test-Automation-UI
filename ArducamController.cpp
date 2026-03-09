#include "ArduCamController.h"
#include <QSerialPortInfo>

// Controller Implementation

ArduCamController::ArduCamController(QObject* parent) : QObject(parent) {
    connect(&m_serial, &QSerialPort::readyRead, this, &ArduCamController::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &ArduCamController::onError);

    // Command-queue pacing timer
    m_cmdTimer.setSingleShot(true);
    m_cmdTimer.setInterval(50); // 50 ms between commands
    connect(&m_cmdTimer, &QTimer::timeout, this, &ArduCamController::drainQueue);

    // Watchdog timer
    m_watchdogTimer.setSingleShot(true);
    m_watchdogTimer.setInterval(30000); // 30 seconds
    connect(&m_watchdogTimer, &QTimer::timeout, this, &ArduCamController::onWatchdogTimeout);
}

QStringList ArduCamController::availablePorts() const {
    QStringList out;
    for (const auto& info : QSerialPortInfo::availablePorts())
        out << info.portName();
    return out;
}

void ArduCamController::connectPort(const QString& portName, int baud) {
    if (m_serial.isOpen())
        m_serial.close();

    m_serial.setPortName(portName);
    m_serial.setBaudRate(baud);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial.open(QIODevice::ReadWrite)) {
        emit logLine(QString("ERROR: Failed to open %1").arg(portName));
        return;
    }

    m_connected = true;
    emit connectedChanged();
    emit logLine(QString("COM %1 is open!").arg(portName));

    // Optional: clear buffers
    m_serial.clear(QSerialPort::AllDirections);
    m_rxBuffer.clear();
    m_currentJpeg.clear();
    m_state = RxState::Text;
}

void ArduCamController::disconnectPort() {
    // Flush the command queue
    m_cmdTimer.stop();
    m_watchdogTimer.stop();
    m_cmdQueue.clear();
    m_waitingForJpeg = false;
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }

    if (m_serial.isOpen())
        m_serial.close();

    m_connected = false;
    m_streaming = false;
    emit connectedChanged();
    emit streamingChanged();
    emit logLine("COM is closed!");
}

// ── Command queue ────────────────────────────────────────────

void ArduCamController::enqueueCommand(const QByteArray &cmd) {
    m_cmdQueue.enqueue(cmd);
    if (!m_busy)
        drainQueue();
}

void ArduCamController::enqueueCommandFront(const QByteArray &cmd) {
    m_cmdQueue.prepend(cmd);
    if (!m_busy)
        drainQueue();
}

void ArduCamController::drainQueue() {
    if (m_cmdQueue.isEmpty()) {
        if (m_busy) {
            m_busy = false;
            emit busyChanged();
        }
        m_watchdogTimer.stop();
        return;
    }

    QByteArray cmd = m_cmdQueue.dequeue();
    m_serial.write(cmd);
    m_serial.flush();

    if (!m_busy) {
        m_busy = true;
        emit busyChanged();
    }

    m_watchdogTimer.start(); // Start or restart watchdog for the new command

    // For CAPTURE_SINGLE (0x10): don't use the pacing timer.
    // Instead, hold the queue until the full JPEG response has been received.
    // This prevents the next 0x10 arriving while the camera is still capturing.
    if (cmd.size() == 1 && (quint8)cmd[0] == 0x10) {
        m_waitingForJpeg = true;
        // drainQueue() will be called again from onReadyRead() once JPEG completes
    } else {
        m_cmdTimer.start(); // fires after 50 ms → next drainQueue()
    }
}

void ArduCamController::onWatchdogTimeout() {
    if (m_busy || m_waitingForJpeg) {
        emit logLine("ERROR: Command watchdog timeout! Force-draining queue.");
        m_waitingForJpeg = false;
        drainQueue();
    }
}

// ── Camera commands (all go through the queue) ───────────────

void ArduCamController::setResolution(int code0to6) {
    if (code0to6 < 0 || code0to6 > 6) return;
    emit commandQueued(QString("SET_RES [%1]").arg(code0to6));
    enqueueCommand(QByteArray(1, char((quint8)code0to6)));
}

void ArduCamController::jpegInit() {
    emit commandQueued("JPEG_INIT [0x11]");
    enqueueCommand(QByteArray(1, char(0x11)));
}

void ArduCamController::captureSingle()
{
    m_streaming = false;
    emit streamingChanged();

    m_pendingSingleShotSave = m_saveSingleShots;

    emit commandQueued("CAPTURE_SINGLE [0x10]");
    enqueueCommand(QByteArray(1, char(0x10))); // single-shot command
}


void ArduCamController::startStreaming() {
    m_streaming = true;
    emit streamingChanged();
    emit commandQueued("START_STREAM [0x20]");
    enqueueCommand(QByteArray(1, char(0x20)));
}

void ArduCamController::stopStreaming() {
    // Priority: push stop to the front of the queue
    emit commandQueued("STOP_STREAM [0x21] (priority)");
    enqueueCommandFront(QByteArray(1, char(0x21)));
    m_streaming = false;
    emit streamingChanged();
}

void ArduCamController::onReadyRead() {
    m_rxBuffer += m_serial.readAll();

    // Keep draining buffer until no more progress
    bool progressed = true;
    while (progressed) {
        progressed = false;

        if (m_state == RxState::Text) {
            int nl = m_rxBuffer.indexOf('\n');
            if (nl >= 0) {
                progressed = true;
                QByteArray lineBytes = m_rxBuffer.left(nl + 1);
                m_rxBuffer.remove(0, nl + 1);

                QString line = QString::fromLatin1(lineBytes).trimmed();
                if (!line.isEmpty()) {
                    m_lastLogLine = line;
                    emit lastLogLineChanged();
                    emit logLine(line);
                }

                // Arduino prints this line right before it starts binary JPEG bytes
                if (line.startsWith("ACK IMG END")) {
                    m_state = RxState::Jpeg;
                    m_currentJpeg.clear();
                }
            }
        } else {
            // RxState::Jpeg
            if (!m_rxBuffer.isEmpty()) {
                progressed = true;
                m_currentJpeg += m_rxBuffer;
                m_rxBuffer.clear();

                // Detect exact End of Image marker location
                QByteArray eoi = QByteArray::fromHex("FFD9");
                int eoiIndex = m_currentJpeg.indexOf(eoi);

                if (eoiIndex >= 0) {
                    int endPos = eoiIndex + 2;
                    QByteArray jpegData = m_currentJpeg.left(endPos);
                    QByteArray remainder = m_currentJpeg.mid(endPos);

                    emit jpegFrameReceived(jpegData);

                    m_frameCounter++;
                    emit frameCounterChanged();

                    m_currentJpeg.clear();
                    m_state = RxState::Text;

                    // Push any remaining bytes (e.g. the next ACK text) back to the buffer
                    m_rxBuffer = remainder;
                    
                    m_watchdogTimer.stop(); // Image finished successfully, stop watchdog

                    // If a CAPTURE_SINGLE was waiting for this JPEG to complete,
                    // now drain the next queued command.
                    if (m_waitingForJpeg) {
                        m_waitingForJpeg = false;
                        QTimer::singleShot(50, this, &ArduCamController::drainQueue);
                    }
                } else {
                    // Safety cap so a broken stream doesn't blow RAM
                    // A 5MP image can easily be up to 5MB, so use 10MB as the safety limit
                    const int maxJpeg = 10 * 1024 * 1024; // 10MB
                    if (m_currentJpeg.size() > maxJpeg) {
                        emit logLine("ERROR: JPEG buffer overflow (no FF D9 found). Resetting parser.");
                        m_currentJpeg.clear();
                        m_state = RxState::Text;
                        m_waitingForJpeg = false; // Fix permanent queue stall on overflow
                        QTimer::singleShot(50, this, &ArduCamController::drainQueue);
                    }
                }
            }
        }
    }
}

void ArduCamController::onError(QSerialPort::SerialPortError e) {
    if (e == QSerialPort::NoError) return;
    emit logLine(QString("Serial error: %1").arg(m_serial.errorString()));
}

void ArduCamController::setExposureEVIndex(int idx)
{
    if (!m_serial.isOpen())
        return;

    // Map UI index -> command byte in your sketch
    // 0: -1.7EV  -> 0xA0
    // 1: -1.3EV  -> 0xA1
    // 2: -1.0EV  -> 0xA2
    // 3: -0.7EV  -> 0xA3
    // 4: -0.3EV  -> 0xA4
    // 5: default -> 0xA5
    // 6: +0.7EV  -> 0xA6
    // 7: +1.0EV  -> 0xA7
    // 8: +1.3EV  -> 0xA8
    // 9: +1.7EV  -> 0xA9

    static const quint8 cmds[] = {
        0xA0, 0xA1, 0xA2, 0xA3, 0xA4,
        0xA5,
        0xA6, 0xA7, 0xA8, 0xA9
    };

    if (idx < 0 || idx >= int(sizeof(cmds)/sizeof(cmds[0])))
        return;

    emit commandQueued(QString("EV_INDEX [0x%1]").arg(cmds[idx], 2, 16, QChar('0')).toUpper());
    enqueueCommand(QByteArray(1, char(cmds[idx])));
}

static void appendU32LE(QByteArray& a, quint32 v) {
    a.append(char(v & 0xFF));
    a.append(char((v >> 8) & 0xFF));
    a.append(char((v >> 16) & 0xFF));
    a.append(char((v >> 24) & 0xFF));
}

static void appendU16LE(QByteArray& a, quint16 v) {
    a.append(char(v & 0xFF));
    a.append(char((v >> 8) & 0xFF));
}

void ArduCamController::setAutoExposure(bool enable) {
    if (!m_serial.isOpen()) return;
    QByteArray p;
    p.reserve(2);
    p.append(char(0xF0));
    p.append(char(enable ? 1 : 0));
    emit commandQueued(QString("AUTO_EXP [0xF0, %1]").arg(enable ? "ON" : "OFF"));
    enqueueCommand(p);
}

void ArduCamController::setExposureUs(quint32 exposureUs) {
    if (!m_serial.isOpen()) return;
    QByteArray p;
    p.reserve(1 + 4);
    p.append(char(0xF1));
    appendU32LE(p, exposureUs);
    emit commandQueued(QString("SET_EXP_US [0xF1, %1µs]").arg(exposureUs));
    enqueueCommand(p);
}

void ArduCamController::setLineTimeUs(quint16 lineTimeUs) {
    if (!m_serial.isOpen()) return;
    QByteArray p;
    p.reserve(1 + 2);
    p.append(char(0xF2));
    appendU16LE(p, lineTimeUs);
    emit commandQueued(QString("SET_LINE_T [0xF2, %1µs]").arg(lineTimeUs));
    enqueueCommand(p);
}

void ArduCamController::setSaveSingleShots(bool on)
{
    if (m_saveSingleShots == on) return;
    m_saveSingleShots = on;
    emit saveSingleShotsChanged();
}
