#pragma once
#include <QObject>
#include <QVector>

class WaveformExporter : public QObject {
    Q_OBJECT
public:
    explicit WaveformExporter(QObject *parent = nullptr);
    ~WaveformExporter();
    
    // Export oscilloscope data to CSV
    void exportToCSV(const QVector<double> &ch1Data, 
                     const QVector<double> &ch2Data, 
                     const QVector<double> &timeData,
                     const QVector<double> &ch1FFT = QVector<double>(),
                     const QVector<double> &ch2FFT = QVector<double>(),
                     const QVector<double> &freqData = QVector<double>());
    
    // Legacy method for compatibility
    void exportToCSV(const QVector<QVector<double>> &data);
    // TODO: Add CSV export methods
}; 