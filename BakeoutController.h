#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

class ArduCamController;

class BakeoutController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(double calibratedExposure READ calibratedExposure NOTIFY calibratedExposureChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit BakeoutController(ArduCamController* cam, QObject* parent = nullptr);

    QString status()            const { return m_status; }
    double  calibratedExposure() const { return m_calibratedExposure; }
    bool    busy()               const { return m_busy; }

    Q_INVOKABLE void startCalibration();
    Q_INVOKABLE void captureImages(const QString& folder, const QString& prefix,
                                   int angle, int count);
    Q_INVOKABLE void cancelCapture();
    Q_INVOKABLE void createFolder(const QString& name);
    Q_INVOKABLE void stopStreamingIfActive();

signals:
    void statusChanged();
    void calibratedExposureChanged();
    void calibrationDone(double exposureUs);
    void calibrationFailed(const QString& reason);
    void imageCaptured(const QString& path, int index);
    void imagesSaved(int count);
    void captureCancelled();
    void busyChanged();

private slots:
    void onJpegReceived(const QByteArray& jpeg);
    void onImageCaptureFailed(const QString& reason);

private:
    void   setStatus(const QString& s);
    void   setBusy(bool b);
    double computeAvgBrightness(const QByteArray& jpeg);
    void   doNextCalibrationCapture();

    ArduCamController* m_cam;

    // Calibration
    bool     m_calibrating      = false;
    quint32  m_calLow           = 100;
    quint32  m_calHigh          = 500000;
    int      m_calAttempts      = 0;
    static constexpr int kCalMaxAttempts     = 15;
    static constexpr int kTargetBrightness   = 160;
    static constexpr int kBrightnessTolerance = 5;

    // Image series
    bool    m_capturingSeries   = false;
    int     m_captureRemaining  = 0;
    int     m_captureTotal      = 0;
    QString m_captureFolder;
    QString m_capturePrefix;
    int     m_captureAngle      = 0;

    QString m_status;
    double  m_calibratedExposure = 0.0;
    bool    m_busy               = false;
};
