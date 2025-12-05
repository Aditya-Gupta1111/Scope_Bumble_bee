#include "WaveformExporter.h"
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

WaveformExporter::WaveformExporter(QObject *parent) : QObject(parent) {}

void WaveformExporter::exportToCSV(const QVector<double> &ch1Data, 
                                   const QVector<double> &ch2Data, 
                                   const QVector<double> &timeData,
                                   const QVector<double> &ch1FFT,
                                   const QVector<double> &ch2FFT,
                                   const QVector<double> &freqData) {
    
    QString fileName = QFileDialog::getSaveFileName(nullptr, tr("Export Oscilloscope Data"), "", tr("CSV Files (*.csv)"));
    if (fileName.isEmpty()) {
        qDebug() << "[WaveformExporter] Export cancelled by user";
        return;
    }
    
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "[WaveformExporter] Failed to open file for writing:" << fileName;
        return;
    }
    
    QTextStream out(&file);
    
    // Write header with timestamp
    out << "# Oscilloscope Data Export\n";
    out << "# Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    out << "# Data Points: " << ch1Data.size() << "\n";
    out << "# Time Unit: microseconds\n";
    out << "# Voltage Unit: Volts\n";
    out << "\n";
    
    // Write data headers
    out << "Time(us),CH1(V),CH2(V)\n";
    
    // Write data rows
    int maxRows = qMax(ch1Data.size(), qMax(ch2Data.size(), timeData.size()));
    for (int i = 0; i < maxRows; ++i) {
        double time = (i < timeData.size()) ? timeData[i] : i;
        double ch1 = (i < ch1Data.size()) ? ch1Data[i] : 0.0;
        double ch2 = (i < ch2Data.size()) ? ch2Data[i] : 0.0;
        
        out << QString::number(time, 'f', 3) << ","
            << QString::number(ch1, 'f', 3) << ","
            << QString::number(ch2, 'f', 3) << "\n";
    }
    
    // If FFT data is available, add it to the same file
    if (!ch1FFT.isEmpty() || !ch2FFT.isEmpty()) {
        out << "\n";
        out << "# FFT Data\n";
        out << "# Frequency Unit: Hz\n";
        out << "# Magnitude Unit: dB\n";
        out << "\n";
        out << "Frequency(Hz),CH1_FFT(dB),CH2_FFT(dB)\n";
        
        int maxFFTRows = qMax(ch1FFT.size(), qMax(ch2FFT.size(), freqData.size()));
        for (int i = 0; i < maxFFTRows; ++i) {
            double freq = (i < freqData.size()) ? freqData[i] : i;
            double ch1fft = (i < ch1FFT.size()) ? ch1FFT[i] : 0.0;
            double ch2fft = (i < ch2FFT.size()) ? ch2FFT[i] : 0.0;
            
            out << QString::number(freq, 'f', 1) << ","
                << QString::number(ch1fft, 'f', 2) << ","
                << QString::number(ch2fft, 'f', 2) << "\n";
        }
    }
    
    file.close();
    qDebug() << "[WaveformExporter] Successfully exported data to:" << fileName;
}

void WaveformExporter::exportToCSV(const QVector<QVector<double>> &data) {
    QString fileName = QFileDialog::getSaveFileName(nullptr, tr("Export CSV"), "", tr("CSV Files (*.csv)"));
    if (fileName.isEmpty()) return;
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    // Write headers
    out << "Time,CH1,CH2\n";
    int nRows = data[0].size();
    for (int i = 0; i < nRows; ++i) {
        out << data[0][i] << "," << data[1][i] << "," << data[2][i] << "\n";
    }
    file.close();
}

WaveformExporter::~WaveformExporter() {} 