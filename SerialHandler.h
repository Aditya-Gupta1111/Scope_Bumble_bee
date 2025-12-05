#pragma once
#include <QObject>
#include <QSerialPort>
#include <QVector>
#include <QByteArray>
#include <QTimer>

static int findSampleRateIndex(double freq);
static void findLocalExtrema(const QVector<double>& data, double& avgMax, double& avgMin);

class SerialHandler : public QObject {
    Q_OBJECT
public:
    explicit SerialHandler(QObject *parent = nullptr);
    ~SerialHandler();

    bool connectPort(const QString &portName);
    void disconnectPort();
    void startAcquisition();
    void stopAcquisition();
    void abortAcquisition();
    void setOffset(int ch1Offset, int ch2Offset);
    void setTrigger(int trigLevel);
    void setSampleRate(int rateIdx);
    void setMode(int modeIdx);
    void setStudentName(const QString &name);
    void readSignature();
    void sendCommand(const QByteArray &cmd);
    void openPort(const QString &portName) { connectPort(portName); }
    void closePort() { disconnectPort(); }

    // Oscilloscope state machine
    enum class AcquisitionState {
        Idle,
        WaitingForDone,
        WaitingForCh1,
        WaitingForCh2,
        Complete
    };

    void startOscilloscopeAcquisition(int mode, int dataLength, bool dualChannel);
    void resetAcquisitionState();

    // --- Add: Set protocol parameters from MainWindow ---
    void setProtocolParams(int ch1Off, int ch2Off, int trigLvl, int trigSrc, int trigPol, int srIdx);

signals:
    void oscilloscopeDataReady(const QVector<double>& ch1, const QVector<double>& ch2, const QVector<double>& xvals);
    void oscilloscopeRawDataReady(const QByteArray &ch1, const QByteArray &ch2, int dataLength, bool dualChannel);
    void errorOccurred(const QString &msg);
    void statusMessage(const QString &msg);
    void connectionStatus(bool connected);
    void portError(const QString &error);
    void dataReceived(const QByteArray &data);

private slots:
    void handleReadyRead();
    void handleError(QSerialPort::SerialPortError error);
    void handleSetupStep();

private:
    void processData(const QByteArray &data);
    void sendSetupSequence();
    QSerialPort *serial;
    QByteArray rxBuffer;
    bool running = false;
    // Add all protocol state as needed
    // Oscilloscope state machine variables
    AcquisitionState acqState = AcquisitionState::Idle;
    int acqMode = 1;
    int acqDataLength = 200;
    bool acqDualChannel = true;
    QByteArray ch1Raw, ch2Raw;
    int bytesNeeded = 0;
    // --- Protocol parameters ---
    int ch1Offset = 0;
    int ch2Offset = 0;
    int trigLevel = 0;
    int trigSource = 0; // 0=Auto, 1=CH1, 2=CH2
    int trigPolarity = 0; // 0=L->H, 1=H->L
    int sampleRateIdx = 3;
    // --- Timeout for state machine ---
    QTimer* timeoutTimer = nullptr;
    // --- Asynchronous setup sequence ---
    int setupStep = 0;
    QTimer* setupTimer = nullptr;
    // --- Prevent multiple simultaneous acquisitions ---
    bool acquisitionInProgress = false;
}; 