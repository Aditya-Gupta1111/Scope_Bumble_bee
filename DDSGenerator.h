#pragma once
#include <QObject>

class DDSGenerator : public QObject {
    Q_OBJECT
public:
    explicit DDSGenerator(QObject *parent = nullptr);
    ~DDSGenerator();
    void runDDS(int waveformIdx, int frequency);
    void setWaveform(int waveformIdx);
    void loadArbitraryWaveform();
    void runSweep(int startFreq, int endFreq, int samples, int delayMs);
    // TODO: Add DDS waveform generation methods
}; 