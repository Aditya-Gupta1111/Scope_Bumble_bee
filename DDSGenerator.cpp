#include "DDSGenerator.h"
#include <QFileDialog>
#include <QDebug>

DDSGenerator::DDSGenerator(QObject *parent) : QObject(parent) {}

void DDSGenerator::runDDS(int waveformIdx, int frequency) {
    // Send DDS run command to device via SerialHandler
    // ...
}
void DDSGenerator::setWaveform(int waveformIdx) {
    // Set waveform type (sine, square, triangle, ramp, arbitrary)
    // ...
}
void DDSGenerator::loadArbitraryWaveform() {
    QString fileName = QFileDialog::getOpenFileName(nullptr, tr("Load Arbitrary Waveform"), "", tr("CSV Files (*.csv)"));
    if (!fileName.isEmpty()) {
        // Load CSV and send to device
        // ...
    }
}
void DDSGenerator::runSweep(int startFreq, int endFreq, int samples, int delayMs) {
    // Run DDS sweep as in VB.NET
    // ...
}

DDSGenerator::~DDSGenerator() {} 