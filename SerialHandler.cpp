#include "SerialHandler.h"
#include <QSerialPortInfo>
#include <QTimer>
#include <QDebug>
#include <QThread>

// --- Add: Timeout for each acquisition state ---
static constexpr int STATE_TIMEOUT_MS = 5000; // Increased from 2000ms to 5000ms

SerialHandler::SerialHandler(QObject *parent) : QObject(parent) {
    serial = new QSerialPort(this);
    connect(serial, &QSerialPort::readyRead, this, &SerialHandler::handleReadyRead);
    connect(serial, &QSerialPort::errorOccurred, this, &SerialHandler::handleError);
    // Add timer for timeouts
    timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this]() {
        qWarning() << "SerialHandler: Timeout in state" << (int)acqState;
        if (serial && serial->isOpen()) {
            serial->readAll();
            qDebug() << "[SerialHandler] Cleared serial buffer on timeout.";
        }
        emit errorOccurred(tr("Timeout waiting for data (state %1)").arg((int)acqState));
        resetAcquisitionState();
    });
}

SerialHandler::~SerialHandler() {
    disconnectPort();
}

bool SerialHandler::connectPort(const QString &portName) {
    if (serial->isOpen()) serial->close();
    serial->setPortName(portName);
    serial->setBaudRate(115200);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);
    if (serial->open(QIODevice::ReadWrite)) {
        qDebug() << "Serial port opened successfully:" << portName;
        emit statusMessage(tr("Serial port opened: %1").arg(portName));
        emit connectionStatus(true);
        return true;
    } else {
        qDebug() << "Failed to open serial port:" << portName << serial->errorString();
        emit errorOccurred(tr("Failed to open serial port: %1").arg(portName));
        emit connectionStatus(false);
        return false;
    }
}

void SerialHandler::disconnectPort() {
    if (serial->isOpen()) serial->close();
    emit statusMessage(tr("Serial port closed"));
}

void SerialHandler::startAcquisition() {
    running = true;
    // Send setup and capture commands as in VB.NET
    // ...
    // Example: sendCommand(QByteArray::fromHex("430000")); // 'C' Capture
}

void SerialHandler::stopAcquisition() {
    running = false;
    // Send stop or abort command as in VB.NET
    // ...
}

void SerialHandler::abortAcquisition() {
    running = false;
    // Send abort command as in VB.NET
    // ...
}

void SerialHandler::setOffset(int ch1Offset, int ch2Offset) {
    // Send offset command for both channels
    // ...
}

void SerialHandler::setTrigger(int trigLevel) {
    this->trigLevel = trigLevel;
    if (serial->isOpen()) {
        QByteArray trigLevelCmd(3, 0);
        trigLevelCmd[0] = 0x4C; // 'L'
        trigLevelCmd[1] = (trigLevel >> 8) & 0xFF;
        trigLevelCmd[2] = trigLevel & 0xFF;
        serial->write(trigLevelCmd);
        qDebug() << "[SerialHandler] Sent Trigger Level Command:" << trigLevelCmd.toHex();
    }
}

void SerialHandler::setSampleRate(int rateIdx) {
    // Send sample rate command
    // ...
}

void SerialHandler::setMode(int modeIdx) {
    // Send mode command
    // ...
}

void SerialHandler::setStudentName(const QString &name) {
    // Send student name to device if supported
    // ...
}

void SerialHandler::readSignature() {
    // Send command to read device signature
    // ...
}

void SerialHandler::sendCommand(const QByteArray &cmd) {
    if (serial->isOpen()) {
        serial->write(cmd);
        serial->waitForBytesWritten(100);
    }
}

// --- Asynchronous setup sequence ---
void SerialHandler::sendSetupSequence() {
    setupStep = 0;
    if (!setupTimer) {
        setupTimer = new QTimer(this);
        setupTimer->setSingleShot(true);
        connect(setupTimer, &QTimer::timeout, this, &SerialHandler::handleSetupStep);
    }
    handleSetupStep();
}

void SerialHandler::handleSetupStep() {
    switch (setupStep) {
    case 0: {
        // Offset CH1 - TEMPORARILY DISABLED TO TEST
        qDebug() << "[SerialHandler] CH1 offset command DISABLED for testing, value:" << ch1Offset;
        // QByteArray offsetCmd(3, 0);
        // offsetCmd[0] = 0x4F;
        // offsetCmd[1] = (ch1Offset >> 8) & 0xFF;
        // offsetCmd[2] = ch1Offset & 0xFF;
        // qDebug() << "[SerialHandler] Sending CH1 offset command:" << offsetCmd.toHex() << "value:" << ch1Offset;
        // serial->write(offsetCmd);
        break;
    }
    case 1: {
        // Offset CH2 - TEMPORARILY DISABLED TO TEST
        qDebug() << "[SerialHandler] CH2 offset command DISABLED for testing, value:" << ch2Offset;
        // QByteArray offsetCmd(3, 0);
        // offsetCmd[0] = 0x6F;
        // offsetCmd[1] = (ch2Offset >> 8) & 0xFF;
        // offsetCmd[2] = ch2Offset & 0xFF;
        // qDebug() << "[SerialHandler] Sending CH2 offset command:" << offsetCmd.toHex() << "value:" << ch2Offset;
        // serial->write(offsetCmd);
        break;
    }
    case 2: {
        // Trigger Source (VB.NET: TrigSourceCmd() As Byte = {&H54, &H0, &H0} ' T Trig Source Auto/CH1/CH2 0/1/2
        QByteArray trigSourceCmd(3, 0);
        trigSourceCmd[0] = 0x54; // 'T'
        trigSourceCmd[1] = trigSource; // Use actual trigger source value
        trigSourceCmd[2] = 0x00;
        serial->write(trigSourceCmd);
        break;
    }
    case 3: {
        // Trigger Polarity (VB.NET: TrigPolarityCmd() As Byte = {&H50, &H0, &H0} ' P Trig Polarity L/H H/L 0/1
        QByteArray trigPolarityCmd(3, 0);
        trigPolarityCmd[0] = 0x50; // 'P'
        trigPolarityCmd[1] = trigPolarity; // Use actual trigger polarity value
        trigPolarityCmd[2] = 0x00;
        serial->write(trigPolarityCmd);
        break;
    }
    case 4: {
        // Trigger Level (VB.NET: SetTrigValue() As Byte = {&H4C, &H2, &H0} ' L msb-lsb
        QByteArray trigLevelCmd(3, 0);
        trigLevelCmd[0] = 0x4C; // 'L'
        trigLevelCmd[1] = (trigLevel >> 8) & 0xFF;
        trigLevelCmd[2] = trigLevel & 0xFF;
        serial->write(trigLevelCmd);
        break;
    }
    case 5: {
        // Mode
        QByteArray modeCmd(3, 0);
        modeCmd[0] = 0x46;
        modeCmd[1] = acqMode;
        modeCmd[2] = 0x00;
        serial->write(modeCmd);
        break;
    }
    case 6: {
        // Use 1-based indexing like VB.NET
        int deviceIndex = sampleRateIdx;
        
        QByteArray srCmd(3, 0);
        srCmd[0] = 0x53;
        srCmd[1] = deviceIndex;
        srCmd[2] = 0x00;
        serial->write(srCmd);
        qDebug() << "[SerialHandler] Sent sample rate command:" << srCmd.toHex() << "UI index:" << sampleRateIdx << "device index:" << deviceIndex;
        break;
    }
    case 7: {
        // All setup done, send capture command
        QByteArray cmd;
        cmd.append((char)0x43); cmd.append((char)0x00); cmd.append((char)0x00);
        serial->write(cmd);
        acqState = AcquisitionState::WaitingForDone;
        bytesNeeded = 1;
        timeoutTimer->start(STATE_TIMEOUT_MS);
        qDebug() << "[SerialHandler] Setup complete, sent capture command.";
        return;
    }
    default:
        return;
    }
    setupStep++;
    setupTimer->start(50); // Increased from 20ms to 50ms to match VB.NET timing
}

void SerialHandler::startOscilloscopeAcquisition(int mode, int dataLength, bool dualChannel) {
    if (acquisitionInProgress) {
        qDebug() << "[SerialHandler] Acquisition already in progress, ignoring new request.";
        return;
    }
    acquisitionInProgress = true;
    resetAcquisitionState();
    acqMode = mode;
    acqDataLength = dataLength;
    acqDualChannel = dualChannel;
    qDebug() << "[SerialHandler] Starting acquisition: mode=" << mode << ", len=" << dataLength << ", dual=" << dualChannel;
    sendSetupSequence();
}

void SerialHandler::handleReadyRead() {
    while (serial->bytesAvailable() > 0) {
        if (acqState == AcquisitionState::Idle) {
            qDebug() << "[SerialHandler] Idle state, ignoring data. Bytes available:" << serial->bytesAvailable();
            serial->readAll(); // Clear buffer
            return;
        }
        if (acqState == AcquisitionState::WaitingForDone) {
            qDebug() << "[SerialHandler] WaitingForDone: bytesAvailable=" << serial->bytesAvailable() << ", bytesNeeded=" << bytesNeeded;
            if (serial->bytesAvailable() < bytesNeeded) {
                qDebug() << "[SerialHandler] Not enough bytes for ACK yet.";
                return;
            }
            if (bytesNeeded <= 0 || serial->bytesAvailable() < bytesNeeded) {
                qWarning() << "[SerialHandler] Defensive: Attempted to read more bytes than available for ACK! bytesNeeded=" << bytesNeeded << ", bytesAvailable=" << serial->bytesAvailable();
                return;
            }
            QByteArray ack = serial->read(bytesNeeded);
            qDebug() << "[SerialHandler] Got ACK:" << ack.toHex();
            // Accept any 1-byte acknowledgment for robustness
            // Now request data based on mode
            ch1Raw.clear();
            ch2Raw.clear();
            // --- PATCH: Add delay and clear buffer before data request ---
            serial->readAll(); // Clear any leftover bytes
            QThread::msleep(100); // Wait 100ms before sending data request
            // --- END PATCH ---
            QByteArray dcmd;
            if (acqDualChannel) { 
                dcmd.append((char)0x44); dcmd.append((char)0x01); dcmd.append((char)0x00); // D,1,0 for dual channel
                bytesNeeded = 200;
                acqState = AcquisitionState::WaitingForCh1;
                qDebug() << "[SerialHandler] Sent dual-channel CH1 read command, waiting for" << bytesNeeded << "bytes.";
            }
            else if (acqMode == 2) { 
                dcmd.append((char)0x44); dcmd.append((char)0x03); dcmd.append((char)0x00); // D,3,0 for CH1 only
                bytesNeeded = 400;
                acqState = AcquisitionState::WaitingForCh1;
                qDebug() << "[SerialHandler] Sent CH1-only read command, waiting for" << bytesNeeded << "bytes.";
            }
            else if (acqMode == 3) { 
                dcmd.append((char)0x44); dcmd.append((char)0x04); dcmd.append((char)0x00); // D,4,0 for CH2 only
                bytesNeeded = 400;
                acqState = AcquisitionState::WaitingForCh2; // Directly wait for CH2 data
                qDebug() << "[SerialHandler] Sent CH2-only read command, waiting for" << bytesNeeded << "bytes.";
            }
            qDebug() << "[SerialHandler] Sending data request command:" << dcmd.toHex() << "for mode" << acqMode;
            serial->write(dcmd); serial->waitForBytesWritten(100);
            timeoutTimer->start(STATE_TIMEOUT_MS);
            return;
        }
        if (acqState == AcquisitionState::WaitingForCh1) {
            qDebug() << "[SerialHandler] WaitingForCh1: bytesAvailable=" << serial->bytesAvailable() << ", bytesNeeded=" << bytesNeeded;
            if (serial->bytesAvailable() < bytesNeeded) {
                qDebug() << "[SerialHandler] Not enough bytes for CH1 yet. Waiting for" << (bytesNeeded - serial->bytesAvailable()) << "more bytes.";
                // Add a small delay to allow more data to arrive
                QThread::msleep(10);
                return;
            }
            if (bytesNeeded <= 0 || serial->bytesAvailable() < bytesNeeded) {
                qWarning() << "[SerialHandler] Defensive: Attempted to read more bytes than available for CH1! bytesNeeded=" << bytesNeeded << ", bytesAvailable=" << serial->bytesAvailable();
                return;
            }
            ch1Raw = serial->read(bytesNeeded);
            qDebug() << "[SerialHandler] Got CH1 data, len=" << ch1Raw.size();
            if (ch1Raw.size() > 0) {
                qDebug() << "[SerialHandler] CH1 first 5 bytes:" << ch1Raw.left(5).toHex();
            }
            if (acqDualChannel) {
                // Now request CH2 data
                QByteArray dcmd; dcmd.append((char)0x44); dcmd.append((char)0x02); dcmd.append((char)0x00); // D,2,0
                serial->write(dcmd); serial->waitForBytesWritten(100);
                bytesNeeded = 200;
                acqState = AcquisitionState::WaitingForCh2;
                timeoutTimer->start(STATE_TIMEOUT_MS);
                qDebug() << "[SerialHandler] Sent CH2 read command, waiting for 200 bytes.";
            } else if (acqMode == 2) {
                // CH1-only mode - emit CH1 data in ch1 parameter
                acqState = AcquisitionState::Complete;
                timeoutTimer->stop();
                emit oscilloscopeRawDataReady(ch1Raw, QByteArray(), acqDataLength, false);
                resetAcquisitionState();
            }
            // Note: CH2-only mode (acqMode == 3) goes directly to WaitingForCh2 state
            return;
        }
        if (acqState == AcquisitionState::WaitingForCh2) {
            qDebug() << "[SerialHandler] WaitingForCh2: bytesAvailable=" << serial->bytesAvailable() << ", bytesNeeded=" << bytesNeeded;
            if (serial->bytesAvailable() < bytesNeeded) {
                qDebug() << "[SerialHandler] Not enough bytes for CH2 yet.";
                return;
            }
            if (bytesNeeded <= 0 || serial->bytesAvailable() < bytesNeeded) {
                qWarning() << "[SerialHandler] Defensive: Attempted to read more bytes than available for CH2! bytesNeeded=" << bytesNeeded << ", bytesAvailable=" << serial->bytesAvailable();
                return;
            }
            ch2Raw = serial->read(bytesNeeded);
            qDebug() << "[SerialHandler] Got CH2 data, len=" << ch2Raw.size();
            if (ch2Raw.size() > 0) {
                qDebug() << "[SerialHandler] CH2 first 5 bytes:" << ch2Raw.left(5).toHex();
            }
            acqState = AcquisitionState::Complete;
            timeoutTimer->stop();
            if (acqDualChannel) {
                // Dual channel mode - emit both CH1 and CH2 data
            emit oscilloscopeRawDataReady(ch1Raw, ch2Raw, acqDataLength, true);
            } else if (acqMode == 3) {
                // CH2-only mode - emit CH2 data in ch2 parameter, empty ch1
                emit oscilloscopeRawDataReady(QByteArray(), ch2Raw, acqDataLength, false);
            }
            resetAcquisitionState();
            return;
        }
        // Catch-all: unexpected state/data
        qWarning() << "[SerialHandler] Unexpected state or data. State:" << (int)acqState << ", bytesAvailable=" << serial->bytesAvailable();
        serial->readAll(); // Clear buffer to avoid infinite loop
        return;
    }
}

void SerialHandler::handleError(QSerialPort::SerialPortError error) {
    if (error != QSerialPort::NoError) {
        emit errorOccurred(serial->errorString());
    }
}

void SerialHandler::processData(const QByteArray &data) {
    // Parse and convert data to QVector<double> for plotting
    // ...
}

// --- Add: Setters for protocol parameters (to be called from MainWindow) ---
void SerialHandler::setProtocolParams(int ch1Off, int ch2Off, int trigLvl, int trigSrc, int trigPol, int srIdx) {
    ch1Offset = ch1Off;
    ch2Offset = ch2Off;
    trigLevel = trigLvl;
    trigSource = trigSrc;
    trigPolarity = trigPol;
    sampleRateIdx = srIdx;
    qDebug() << "[SerialHandler] Set protocol params - Sample rate index:" << srIdx << "->" << sampleRateIdx;
}

void SerialHandler::resetAcquisitionState() {
    acqState = AcquisitionState::Idle;
    acqDataLength = 200;
    acqDualChannel = true;
    ch1Raw.clear();
    ch2Raw.clear();
    bytesNeeded = 0;
    acqMode = 1;
    timeoutTimer->stop();
    acquisitionInProgress = false;
} 