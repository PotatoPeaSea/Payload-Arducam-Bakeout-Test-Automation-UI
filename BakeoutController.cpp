#include "BakeoutController.h"
#include "ArducamController.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QBuffer>
#include <QTimer>

BakeoutController::BakeoutController(ArduCamController* cam, QObject* parent)
    : QObject(parent), m_cam(cam)
{
    connect(m_cam, &ArduCamController::jpegFrameReceived,
            this,  &BakeoutController::onJpegReceived);
}

void BakeoutController::setStatus(const QString& s) {
    if (m_status == s) return;
    m_status = s;
    emit statusChanged();
}

void BakeoutController::setBusy(bool b) {
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

void BakeoutController::stopStreamingIfActive() {
    if (m_cam->streaming())
        m_cam->stopStreaming();
}

void BakeoutController::createFolder(const QString& name) {
    QDir dir(QDir::currentPath());
    if (!dir.exists(name))
        dir.mkdir(name);
}

// ─── Calibration ─────────────────────────────────────────────────────────────

void BakeoutController::startCalibration() {
    if (m_calibrating || m_capturingSeries) return;

    m_cam->setAutoExposure(false);
    m_calibrating  = true;
    m_calLow       = 100;
    m_calHigh      = 500000;
    m_calAttempts  = 0;
    setBusy(true);
    setStatus("Starting exposure calibration...");
    doNextCalibrationCapture();
}

void BakeoutController::doNextCalibrationCapture() {
    quint32 mid = (m_calLow + m_calHigh) / 2;
    m_cam->setExposureUs(mid);
    setStatus(QString("Calibrating: %1 µs  (attempt %2 / %3)")
              .arg(mid).arg(m_calAttempts + 1).arg(kCalMaxAttempts));
    // Give the camera 300 ms to apply the new exposure before capturing
    QTimer::singleShot(300, m_cam, [this]() { m_cam->captureSingle(); });
}

double BakeoutController::computeAvgBrightness(const QByteArray& jpeg) {
    QImage img;
    img.loadFromData(jpeg, "JPG");
    if (img.isNull()) return -1.0;
    QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
    qint64 sum = 0;
    const int pixels = gray.width() * gray.height();
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x)
            sum += line[x];
    }
    return pixels > 0 ? double(sum) / pixels : 0.0;
}

// ─── Image series ─────────────────────────────────────────────────────────────

void BakeoutController::captureImages(const QString& folder, const QString& prefix,
                                       int angle, int count)
{
    if (m_capturingSeries || m_calibrating) return;
    m_captureFolder    = folder;
    m_capturePrefix    = prefix;
    m_captureAngle     = angle;
    m_captureTotal     = count;
    m_captureRemaining = count;
    m_capturingSeries  = true;
    setBusy(true);
    setStatus(QString("Capturing %1 images at %2°...").arg(count).arg(angle));
    m_cam->captureSingle();
}

// ─── JPEG handler ─────────────────────────────────────────────────────────────

void BakeoutController::onJpegReceived(const QByteArray& jpeg) {

    if (m_calibrating) {
        double brightness = computeAvgBrightness(jpeg);
        m_calAttempts++;
        quint32 mid = (m_calLow + m_calHigh) / 2;

        if (brightness < 0) {
            if (m_calAttempts < kCalMaxAttempts) { doNextCalibrationCapture(); return; }
            m_calibrating = false; setBusy(false);
            emit calibrationFailed("Failed to decode frames.");
            return;
        }

        bool inRange = qAbs(int(brightness) - kTargetBrightness) <= kBrightnessTolerance;
        setStatus(QString("Attempt %1: brightness %2  (target %3 ± %4)")
                  .arg(m_calAttempts).arg(brightness, 0, 'f', 1)
                  .arg(kTargetBrightness).arg(kBrightnessTolerance));

        if (inRange || m_calAttempts >= kCalMaxAttempts) {
            m_calibratedExposure = mid;
            m_calibrating = false;
            setBusy(false);
            emit calibratedExposureChanged();
            setStatus(QString("Calibration done — %1 µs  |  brightness %2")
                      .arg(mid).arg(brightness, 0, 'f', 1));
            emit calibrationDone(double(mid));
            return;
        }

        if (brightness < kTargetBrightness) m_calLow  = mid;
        else                                m_calHigh = mid;
        doNextCalibrationCapture();
        return;
    }

    if (m_capturingSeries) {
        QBuffer buffer;
        buffer.setData(jpeg);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPG");
        QSize imgSize = reader.size();

        if (imgSize.isValid() && (imgSize.width() != 2592 || imgSize.height() != 1944)) {
            setStatus(QString("Warning: Image was %1x%2. Expected 2592x1944. Enforcing resolution and retaking...")
                      .arg(imgSize.width()).arg(imgSize.height()));
            m_cam->setResolution(6);
            // Give the camera 1.5s to apply the resolution before retaking
            QTimer::singleShot(1500, m_cam, [this]() { m_cam->captureSingle(); });
            return;
        }

        int imageNum = m_captureTotal - m_captureRemaining + 1;
        QString path = QString("%1/%2-%3-%4.jpg")
                       .arg(m_captureFolder).arg(m_capturePrefix)
                       .arg(m_captureAngle).arg(imageNum);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) { f.write(jpeg); f.close(); }

        m_captureRemaining--;
        emit imageCaptured(QDir::current().absoluteFilePath(path), imageNum);
        setStatus(QString("Saved %1 / %2  →  %3").arg(imageNum).arg(m_captureTotal).arg(path));

        if (m_captureRemaining <= 0) {
            m_capturingSeries = false;
            setBusy(false);
            setStatus(QString("All %1 images captured at %2°.").arg(m_captureTotal).arg(m_captureAngle));
            emit imagesSaved(m_captureTotal);
        } else {
            // Give the ArduCam firmware some breathing room before the next capture command
            QTimer::singleShot(500, m_cam, [this]() { m_cam->captureSingle(); });
        }
    }
}
