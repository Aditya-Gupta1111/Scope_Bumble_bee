#pragma once
#include <QObject>
#include <QVector>
#include <QColor>

class QCustomPlot;
class QWidget;

class PlotManager : public QObject {
    Q_OBJECT
public:
    explicit PlotManager(QObject *parent = nullptr);
    ~PlotManager();
    QWidget* plotWidget() const;
    void updateWaveform(const QVector<double>& ch1, const QVector<double>& ch2);
    void updateWaveformWithMultipleTraces(const QVector<QVector<double>>& ch1Traces, const QVector<QVector<double>>& ch2Traces);
    void setMode(int mode);
    void setGains(double ch1Gain, double ch2Gain);
    void setTriggerLine(bool enabled, double level, bool onCh2, QColor color = Qt::magenta);
    void updateTriggerLevel(double level, bool onCh2 = false);
    void setDisplayMode(int mode); // 0=Both, 1=CH1, 2=CH2, 3=XY, 4=DFT1, 5=DFT2
    void setXAxisTitle(const QString& title);
    void setYAxisTitle(const QString& title);
    void setY2AxisTitle(const QString& title);
    void setDataLength(int len);
    void setMultiplier(double m);
    void setMaxDFT(double maxDFT);
    void setMaxFrequency(double maxFreq);
    void plotData(QCustomPlot *plot, const QVector<double>& ch1, const QVector<double>& ch2, const QVector<double>& xvals);
    QVector<QVector<double>> getData() const;
    void plotTriggerLine();
    void setAutoYRangeEnabled(bool enabled);
    // TODO: Add methods for updating plots, modes, etc.
private:
    QCustomPlot *plot;
    int currentMode = 0; // 0=Normal, 1=FFT, 2=XY
    double ch1Gain = 1.0, ch2Gain = 1.0;
    bool triggerLineEnabled = false;
    double triggerLevel = 0.0;
    bool triggerOnCh2 = false;
    QColor triggerColor = Qt::magenta;
    int dataLength = 400;
    double multiplier = 1.0;
    double maxDFT = 1.0;
    double maxFrequency = 1.0;
    QString xAxisTitle = "Time (μs)"; // Default X-axis title
    QVector<double> lastCh1, lastCh2, lastX;
    bool autoYRangeEnabled = true;
    void plotFFT(const QVector<double>& ch);
    void plotXY(const QVector<double>& ch1, const QVector<double>& ch2);
    double getYAxisRangeFromGain(double gain) const;
}; 