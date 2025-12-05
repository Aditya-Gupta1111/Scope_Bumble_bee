#include "PlotManager.h"
#include <QWidget>
// You must add QCustomPlot to your project for this to work
#include "qcustomplot.h"
#include <cmath>
#include <complex>
#include <QLinearGradient>
#include <QDebug>
#include <QFont>
#include <QBrush>

namespace {
const double PI = 3.14159265358979323846;

// Simple, reliable DFT implementation
QVector<double> simpleDFT(const QVector<double>& inputData) {
    if (inputData.isEmpty()) return QVector<double>();
    
    int N = inputData.size();
    QVector<double> magnitude(N/2);
    
    // Simple DFT calculation
    for (int k = 0; k < N/2; ++k) {
        double real = 0.0;
        double imag = 0.0;
        
        for (int n = 0; n < N; ++n) {
            double angle = 2.0 * PI * k * n / N;
            real += inputData[n] * cos(angle);
            imag += inputData[n] * sin(angle);
            }
        
        magnitude[k] = sqrt(real * real + imag * imag) / N;
    }
    
    return magnitude;
}
}

PlotManager::PlotManager(QObject *parent)
    : QObject(parent), plot(new QCustomPlot)
{
    // Create one graph by default - will add more as needed
    plot->addGraph(); // CH1
    plot->graph(0)->setPen(QPen(Qt::blue));
    plot->xAxis->setLabel("Sample");
    plot->yAxis->setLabel("Value");
    plot->xAxis->setRange(0, 399);
    plot->yAxis->setRange(-20.0, 20.0);
    plot->yAxis->setTickLabelColor(Qt::blue);
    plot->yAxis->setLabelColor(Qt::blue);
    plot->yAxis2->setTickLabelColor(Qt::red);
    plot->yAxis2->setLabelColor(Qt::red);
}

PlotManager::~PlotManager() {
    delete plot;
}

QWidget* PlotManager::plotWidget() const {
    return plot;
}

void PlotManager::setMode(int mode) {
    currentMode = mode;
}

void PlotManager::setGains(double g1, double g2) {
    ch1Gain = g1; ch2Gain = g2;
}

void PlotManager::setTriggerLine(bool enabled, double level, bool onCh2, QColor color) {
    triggerLineEnabled = enabled;
    triggerLevel = level;
    triggerOnCh2 = onCh2;
    triggerColor = color;
}

void PlotManager::updateTriggerLevel(double level, bool onCh2) {
    triggerLevel = level;
    triggerOnCh2 = onCh2;
    // Clear existing trigger lines
    plot->clearItems();
    // Redraw trigger line
    plotTriggerLine();
    plot->replot();
}

void PlotManager::setDisplayMode(int mode) {
    currentMode = mode;
}

void PlotManager::setXAxisTitle(const QString& title) { 
    xAxisTitle = title; 
    if (plot) {
        plot->xAxis->setLabel(title); 
    }
}

void PlotManager::setYAxisTitle(const QString& title) { plot->yAxis->setLabel(title); }

void PlotManager::setY2AxisTitle(const QString& title) { plot->yAxis2->setLabel(title); }

void PlotManager::setDataLength(int len) { dataLength = len; }

void PlotManager::setMultiplier(double m) { multiplier = m; }

void PlotManager::setMaxDFT(double m) { maxDFT = m; }

void PlotManager::setMaxFrequency(double m) { maxFrequency = m; }

void PlotManager::setAutoYRangeEnabled(bool enabled) {
    autoYRangeEnabled = enabled;
}

void PlotManager::updateWaveform(const QVector<double>& ch1, const QVector<double>& ch2)
{
    if (!plot) return;
    
    // Clear previous graphs and items
    plot->clearGraphs();
    plot->clearItems();
    
    // Hide all axes initially
    plot->yAxis->setVisible(false);
    plot->yAxis2->setVisible(false);
    
    // Set default axis colors
    plot->yAxis->setTickLabelColor(Qt::red);
    plot->yAxis->setLabelColor(Qt::red);
    plot->yAxis2->setTickLabelColor(Qt::blue);
    plot->yAxis2->setLabelColor(Qt::blue);
    
    // Align labels inside
    plot->yAxis->setTickLabelSide(QCPAxis::lsInside);
    plot->yAxis2->setTickLabelSide(QCPAxis::lsInside);
    
    // Display grid lines
    plot->yAxis->grid()->setVisible(true);
    plot->yAxis2->grid()->setVisible(true);
    
    // Fill axis background with gradient (white to light gray)
    QLinearGradient gradient(0, 0, 0, 1);
    gradient.setColorAt(0, Qt::white);
    gradient.setColorAt(1, QColor(211, 211, 211)); // Light gray
    gradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    plot->axisRect()->setBackground(QBrush(gradient));
    
    // Add text objects for branding (similar to VB.NET)
    QCPItemText *scopeText = new QCPItemText(plot);
    scopeText->position->setType(QCPItemPosition::ptAxisRectRatio);
    scopeText->position->setCoords(1.0, 0.0); // Top right
    scopeText->setText("ScopeX");
    scopeText->setFont(QFont("Arial", 10));
    scopeText->setColor(Qt::black);
    scopeText->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
    
    QCPItemText *signatureText = new QCPItemText(plot);
    signatureText->position->setType(QCPItemPosition::ptAxisRectRatio);
    signatureText->position->setCoords(0.0, 1.0); // Bottom left
    signatureText->setText("Student 12345"); // Replace with actual values
    signatureText->setFont(QFont("Arial", 8));
    signatureText->setColor(Qt::black);
    signatureText->setPositionAlignment(Qt::AlignLeft | Qt::AlignBottom);
    
    // Enable auto scroll range and point values
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    
    // Mode-specific plotting logic (exactly as in VB.NET)
    if (currentMode == 0) { // Both Channels (BothCh_Display_RadioButton)
        // Ensure we have 2 graphs for both channels
        plot->addGraph(); // CH1
        plot->addGraph(); // CH2
        
        QVector<double> x(ch1.size());
        QVector<double> ch1_shifted(ch1.size()), ch2_shifted(ch2.size());
        for (int i = 0; i < x.size(); ++i) {
            x[i] = i * multiplier;
            ch1_shifted[i] = ch1[i] * ch1Gain;
            ch2_shifted[i] = ch2[i] * ch2Gain;
        }
        
        // CH1 data
        plot->graph(0)->setData(x, ch1_shifted);
        plot->graph(0)->setPen(QPen(Qt::red, 2));
        plot->graph(0)->setVisible(true);
        plot->graph(0)->setValueAxis(plot->yAxis);
        
        // CH2 data
        plot->graph(1)->setData(x, ch2_shifted);
        plot->graph(1)->setPen(QPen(Qt::blue, 2));
        plot->graph(1)->setVisible(true);
        plot->graph(1)->setValueAxis(plot->yAxis2);
        
        // Axis setup
        plot->xAxis->setLabel(xAxisTitle);
        plot->yAxis->setLabel("Ch1 Volts");
        plot->yAxis2->setLabel("Ch2 Volts");
        plot->yAxis->setVisible(true);
        plot->yAxis2->setVisible(true);
        plot->yAxis2->setTickLabelColor(Qt::blue);
        plot->yAxis2->setLabelColor(Qt::blue);
        
        // Set Y-axis ranges based on gains (exactly as VB.NET)
        double ch1Range = 10.0 / ch1Gain;
        double ch2Range = 10.0 / ch2Gain;
        plot->yAxis->setRange(-ch1Range, ch1Range);
        plot->yAxis2->setRange(-ch2Range, ch2Range);
        
        // Set X-axis range
        if (x.size() > 1) {
            plot->xAxis->setRange(0, (x.size() - 1) * multiplier);
        } else {
            plot->xAxis->setRange(0, 1);
        }
        
        // Auto-scale if enabled
        if (autoYRangeEnabled) {
            plot->graph(0)->rescaleValueAxis(true);
            plot->graph(1)->rescaleValueAxis(true);
        }
        
    } else if (currentMode == 1) { // CH1 only (CH1_Display_RadioButton)
        // Ensure we have 1 graph for CH1
        plot->addGraph();
        
        QVector<double> x(ch1.size()), ch1_shifted(ch1.size());
        for (int i = 0; i < x.size(); ++i) {
            x[i] = i * multiplier;
            ch1_shifted[i] = ch1[i] * ch1Gain;
        }
        
        plot->graph(0)->setData(x, ch1_shifted);
        plot->graph(0)->setPen(QPen(Qt::red, 2));
        plot->graph(0)->setVisible(true);
        plot->graph(0)->setValueAxis(plot->yAxis);
        
        // Axis setup
        plot->xAxis->setLabel(xAxisTitle);
        plot->yAxis->setLabel("Ch1 Volts");
        plot->yAxis->setVisible(true);
        plot->yAxis2->setVisible(false);
        plot->yAxis->setTickLabelColor(Qt::red);
        plot->yAxis->setLabelColor(Qt::red);
        
        // Set Y-axis range based on CH1 gain
        double ch1Range = 10.0 / ch1Gain;
        plot->yAxis->setRange(-ch1Range, ch1Range);
        
        // Set X-axis range
        if (x.size() > 1) {
            plot->xAxis->setRange(0, (x.size() - 1) * multiplier);
        } else {
            plot->xAxis->setRange(0, 1);
        }
        
        // Auto-scale if enabled
        if (autoYRangeEnabled) {
            plot->graph(0)->rescaleValueAxis(true);
        }
        
    } else if (currentMode == 2) { // CH2 only (CH2_Display_RadioButton)
        // Ensure we have 1 graph for CH2
        plot->addGraph();
        
        QVector<double> x(ch2.size()), ch2_shifted(ch2.size());
        for (int i = 0; i < x.size(); ++i) {
            x[i] = i * multiplier;
            ch2_shifted[i] = ch2[i] * ch2Gain;
        }
        
        plot->graph(0)->setData(x, ch2_shifted);
        plot->graph(0)->setPen(QPen(Qt::blue, 2));
        plot->graph(0)->setVisible(true);
        plot->graph(0)->setValueAxis(plot->yAxis2);
        
        // Axis setup
        plot->xAxis->setLabel(xAxisTitle);
        plot->yAxis2->setLabel("Ch2 Volts");
        plot->yAxis->setVisible(false);
        plot->yAxis2->setVisible(true);
        plot->yAxis2->setTickLabelColor(Qt::blue);
        plot->yAxis2->setLabelColor(Qt::blue);
        
        // Set Y-axis range based on CH2 gain
        double ch2Range = 10.0 / ch2Gain;
        plot->yAxis2->setRange(-ch2Range, ch2Range);
        
        // Set X-axis range
        if (x.size() > 1) {
            plot->xAxis->setRange(0, (x.size() - 1) * multiplier);
        } else {
            plot->xAxis->setRange(0, 1);
        }
        
        // Auto-scale if enabled
        if (autoYRangeEnabled) {
            plot->graph(0)->rescaleValueAxis(true);
        }
        
    } else if (currentMode == 3) { // XY mode (XY_Display_RadioButton)
        // Ensure we have 1 graph for XY mode
        plot->addGraph();
        
        int N = std::min(ch1.size(), ch2.size());
        QVector<double> x(N), y(N);
        for (int i = 0; i < N; ++i) {
            x[i] = ch1[i] * ch1Gain; // CH1 as X
            y[i] = ch2[i] * ch2Gain; // CH2 as Y
        }
        
        plot->graph(0)->setData(x, y);
        plot->graph(0)->setPen(QPen(Qt::darkGreen, 2));
        plot->graph(0)->setVisible(true);
        plot->graph(0)->setValueAxis(plot->yAxis);
        
        // Axis setup for XY mode
        plot->xAxis->setLabel("Ch1 Volts");
        plot->yAxis->setLabel("Ch2 Volts");
        plot->yAxis->setVisible(true);
        plot->yAxis2->setVisible(false);
        plot->yAxis->setTickLabelColor(Qt::red);
        plot->yAxis->setLabelColor(Qt::red);
        
        // Set ranges based on gains (exactly as VB.NET)
        double ch1Range = 10.0 / ch1Gain;
        double ch2Range = 10.0 / ch2Gain;
        plot->xAxis->setRange(-ch1Range, ch1Range);
        plot->yAxis->setRange(-ch2Range, ch2Range);
        
        // Auto-scale if enabled
        if (autoYRangeEnabled) {
            plot->graph(0)->rescaleValueAxis(true);
        }
        
    } else if (currentMode == 4) { // DFT CH1 (DFT_CH1_Display_RadioButton)
        // Ensure we have 1 graph for DFT
        plot->addGraph();
        
        QVector<double> mag = simpleDFT(ch1);
        if (!mag.isEmpty()) {
            QVector<double> freq(mag.size());
            for (int i = 0; i < freq.size(); ++i) {
                freq[i] = i * (maxFrequency / ch1.size());
            }
            
            plot->graph(0)->setData(freq, mag);
            plot->graph(0)->setPen(QPen(Qt::red, 2));
            plot->graph(0)->setVisible(true);
            plot->graph(0)->setValueAxis(plot->yAxis);
            
            // Axis setup for DFT
            plot->xAxis->setLabel("Frequency (Hz)");
            plot->yAxis->setLabel("Ch1 Magnitude");
            plot->yAxis->setVisible(true);
            plot->yAxis2->setVisible(false);
            plot->yAxis->setTickLabelColor(Qt::red);
            plot->yAxis->setLabelColor(Qt::red);
            
            // Set ranges for DFT
            plot->xAxis->setRange(0, maxFrequency / 2);
        plot->yAxis->setRange(0, maxDFT);
        }
        
        // Auto-scale if enabled
        if (autoYRangeEnabled && !mag.isEmpty()) {
            double maxMag = *std::max_element(mag.begin(), mag.end());
            plot->yAxis->setRange(0, maxMag * 1.1);
        }
        
    } else if (currentMode == 5) { // DFT CH2 (DFT_CH2_Display_RadioButton)
        // Ensure we have 1 graph for DFT
        plot->addGraph();
        
        QVector<double> mag = simpleDFT(ch2);
        if (!mag.isEmpty()) {
            QVector<double> freq(mag.size());
            for (int i = 0; i < freq.size(); ++i) {
                freq[i] = i * (maxFrequency / ch2.size());
            }
            
            plot->graph(0)->setData(freq, mag);
            plot->graph(0)->setPen(QPen(Qt::blue, 2));
            plot->graph(0)->setVisible(true);
            plot->graph(0)->setValueAxis(plot->yAxis);
            
            // Axis setup for DFT
            plot->xAxis->setLabel("Frequency (Hz)");
            plot->yAxis->setLabel("Ch2 Magnitude");
            plot->yAxis->setVisible(true);
            plot->yAxis2->setVisible(false);
            plot->yAxis->setTickLabelColor(Qt::blue);
            plot->yAxis->setLabelColor(Qt::blue);
            
            // Set ranges for DFT
            plot->xAxis->setRange(0, maxFrequency / 2);
        plot->yAxis->setRange(0, maxDFT);
        }
        
        // Auto-scale if enabled
        if (autoYRangeEnabled && !mag.isEmpty()) {
            double maxMag = *std::max_element(mag.begin(), mag.end());
            plot->yAxis->setRange(0, maxMag * 1.1);
        }
        
    } else if (currentMode == 6) { // NEW: FFT Both CH1 & CH2
        // Ensure we have 2 graphs for both FFTs
        plot->addGraph(); // CH1 FFT
        plot->addGraph(); // CH2 FFT
        QVector<double> mag1 = simpleDFT(ch1);
        QVector<double> mag2 = simpleDFT(ch2);
        QVector<double> freq1(mag1.size()), freq2(mag2.size());
        for (int i = 0; i < freq1.size(); ++i) freq1[i] = i * (maxFrequency / ch1.size());
        for (int i = 0; i < freq2.size(); ++i) freq2[i] = i * (maxFrequency / ch2.size());
        plot->graph(0)->setData(freq1, mag1);
        plot->graph(0)->setPen(QPen(Qt::red, 2));
        plot->graph(0)->setVisible(true);
        plot->graph(0)->setValueAxis(plot->yAxis);
        plot->graph(1)->setData(freq2, mag2);
        plot->graph(1)->setPen(QPen(Qt::blue, 2));
        plot->graph(1)->setVisible(true);
        plot->graph(1)->setValueAxis(plot->yAxis);
        // Axis setup for DFT Both
        plot->xAxis->setLabel("Frequency (Hz)");
        plot->yAxis->setLabel("Magnitude");
        plot->yAxis->setVisible(true);
        plot->yAxis2->setVisible(false);
        plot->yAxis->setTickLabelColor(Qt::black);
        plot->yAxis->setLabelColor(Qt::black);
        // Set ranges for DFT Both
        double maxF = std::max(maxFrequency / 2, std::max(freq1.last(), freq2.last()));
        plot->xAxis->setRange(0, maxF);
        double maxMag = 0;
        if (!mag1.isEmpty()) maxMag = std::max(maxMag, *std::max_element(mag1.begin(), mag1.end()));
        if (!mag2.isEmpty()) maxMag = std::max(maxMag, *std::max_element(mag2.begin(), mag2.end()));
        plot->yAxis->setRange(0, maxMag * 1.1);
        // Auto-scale if enabled
        if (autoYRangeEnabled) {
            plot->graph(0)->rescaleValueAxis(true);
            plot->graph(1)->rescaleValueAxis(true);
        }
    }
    
    // Always show trigger level line if enabled
    if (triggerLineEnabled) {
        plotTriggerLine();
    }
    
    // Make sure the graph gets redrawn
    plot->replot();
}

void PlotManager::plotFFT(const QVector<double>& ch) {
    if (ch.isEmpty()) return;
    
    QVector<double> mag = simpleDFT(ch);
    if (mag.isEmpty()) return;
    
    QVector<double> x(mag.size());
    for (int i = 0; i < x.size(); ++i) {
        x[i] = i * (1.0 / ch.size()); // Frequency array
    }
    
    // Ensure we have a graph
    if (plot->graphCount() < 1) plot->addGraph();
    
    plot->graph(0)->setData(x, mag);
    plot->graph(0)->setVisible(true);
    if (plot->graphCount() > 1) plot->graph(1)->setVisible(false);
    
    plot->xAxis->setLabel("Frequency (Hz)");
    plot->yAxis->setLabel("Magnitude");
    plot->xAxis->setRange(0, x.last());
    plot->yAxis->setRange(0, *std::max_element(mag.begin(), mag.end()) * 1.1);
}

void PlotManager::plotXY(const QVector<double>& ch1, const QVector<double>& ch2) {
    // Ensure we have 1 graph for XY mode
    if (plot->graphCount() < 1) plot->addGraph();
    while (plot->graphCount() > 1) plot->removeGraph(plot->graphCount() - 1);
    
    int N = std::min(ch1.size(), ch2.size());
    QVector<double> x(N), y(N);
    for (int i = 0; i < N; ++i) { x[i] = ch1[i]; y[i] = ch2[i]; }
    plot->graph(0)->setData(x, y);
    plot->graph(0)->setVisible(true);
    plot->xAxis->setLabel("CH1");
    plot->yAxis->setLabel("CH2");
    plot->xAxis->setRange(*std::min_element(x.begin(), x.end()), *std::max_element(x.begin(), x.end()));
    plot->yAxis->setRange(*std::min_element(y.begin(), y.end()), *std::max_element(y.begin(), y.end()));
}

void PlotManager::plotTriggerLine() {
    // Don't draw trigger line if trigger level is not set or if plot is not ready
    if (qIsNaN(triggerLevel) || !plot || plot->xAxis->range().size() <= 0) {
        return;
    }
    
    QCPItemLine *line = new QCPItemLine(plot);
    if (triggerOnCh2) {
        line->start->setCoords(plot->xAxis->range().lower, triggerLevel);
        line->end->setCoords(plot->xAxis->range().upper, triggerLevel);
        line->setPen(QPen(triggerColor, 2, Qt::DashLine));
        line->setClipToAxisRect(true);
        line->start->setType(QCPItemPosition::ptPlotCoords);
        line->end->setType(QCPItemPosition::ptPlotCoords);
        line->start->setAxes(plot->xAxis, plot->yAxis2);
        line->end->setAxes(plot->xAxis, plot->yAxis2);
    } else {
        line->start->setCoords(plot->xAxis->range().lower, triggerLevel);
        line->end->setCoords(plot->xAxis->range().upper, triggerLevel);
        line->setPen(QPen(triggerColor, 2, Qt::DashLine));
        line->setClipToAxisRect(true);
        line->start->setType(QCPItemPosition::ptPlotCoords);
        line->end->setType(QCPItemPosition::ptPlotCoords);
        line->start->setAxes(plot->xAxis, plot->yAxis);
        line->end->setAxes(plot->xAxis, plot->yAxis);
    }
}

void PlotManager::plotData(QCustomPlot *plot, const QVector<double>& ch1, const QVector<double>& ch2, const QVector<double>& xvals) {
    qDebug() << "[PlotManager] plotData: Received CH1 size:" << ch1.size() << "CH2 size:" << ch2.size() << "X size:" << xvals.size();
    qDebug() << "[PlotManager] plotData: CH1 first 5 values:" << (ch1.size() >= 5 ? QVector<double>(ch1.begin(), ch1.begin() + 5) : ch1);
    qDebug() << "[PlotManager] plotData: CH2 first 5 values:" << (ch2.size() >= 5 ? QVector<double>(ch2.begin(), ch2.begin() + 5) : ch2);
    qDebug() << "[PlotManager] plotData: X first 5 values:" << (xvals.size() >= 5 ? QVector<double>(xvals.begin(), xvals.begin() + 5) : xvals);
    
    lastCh1 = ch1;
    lastCh2 = ch2;
    lastX = xvals;
    // Defensive: Ensure at least two graphs exist
    while (plot->graphCount() < 2) plot->addGraph();
    plot->graph(0)->setData(xvals, ch1);
    plot->graph(0)->setPen(QPen(Qt::blue));
    if (!ch2.isEmpty()) {
        plot->graph(1)->setData(xvals, ch2);
        plot->graph(1)->setPen(QPen(Qt::red));
        plot->graph(1)->setVisible(true);
    } else {
        plot->graph(1)->setVisible(false);
    }
    plot->xAxis->setLabel(xAxisTitle);
    plot->yAxis->setLabel("Voltage (V)");
    
    // No need to shift data - voltage conversion already maps to correct range
    QVector<double> ch1_shifted = ch1;
    QVector<double> ch2_shifted = ch2;
    
    // Set fixed x-axis range to prevent fluctuations
    double fixedXRange = 1000.0; // Fixed 1000 μs range
    plot->xAxis->setRange(0, fixedXRange);
    qDebug() << "[PlotManager] plotData: Set X range to 0 to" << fixedXRange << "Y range to" << plot->yAxis->range().lower << "to" << plot->yAxis->range().upper;
    plot->replot();
}

QVector<QVector<double>> PlotManager::getData() const {
    return {lastX, lastCh1, lastCh2};
}

double PlotManager::getYAxisRangeFromGain(double gain) const {
    // Convert gain to voltage range based on UI gain settings
    // Gain values: 0.5=±20V, 1.0=±10V, 2.0=±5V, 4.0=±2.5V, 8.0=±1.25V, 16.0=±0.625V
    if (gain <= 0.5) return 20.0;      // ±20V
    else if (gain <= 1.0) return 10.0; // ±10V
    else if (gain <= 2.0) return 5.0;  // ±5V
    else if (gain <= 4.0) return 2.5;  // ±2.5V
    else if (gain <= 8.0) return 1.25; // ±1.25V
    else return 0.625;                 // ±0.625V
}

void PlotManager::updateWaveformWithMultipleTraces(const QVector<QVector<double>>& ch1Traces, const QVector<QVector<double>>& ch2Traces) {
    // Clear existing graphs
    plot->clearGraphs();
    
    // Setup plot appearance
    plot->legend->setVisible(true);
    plot->xAxis->grid()->setVisible(true);
    plot->yAxis->grid()->setVisible(true);
    plot->yAxis2->grid()->setVisible(true);
    QLinearGradient bg(0,0,0,400);
    bg.setColorAt(0, Qt::white);
    bg.setColorAt(1, QColor(220,220,220));
    plot->setBackground(bg);
    
    // Ensure dual-axis setup is properly configured
    plot->yAxis2->setVisible(true);
    plot->yAxis2->setTickLabels(true);
    plot->yAxis2->setSubTicks(true);
    plot->yAxis2->setTickLabelFont(QFont("Arial", 8));
    plot->yAxis2->setLabelFont(QFont("Arial", 10));
    
    // Set axis colors
    plot->yAxis->setTickLabelColor(Qt::blue);
    plot->yAxis->setLabelColor(Qt::blue);
    plot->yAxis->setBasePen(QPen(Qt::blue));
    plot->yAxis->setTickPen(QPen(Qt::blue));
    plot->yAxis->setSubTickPen(QPen(Qt::blue));
    
    plot->yAxis2->setTickLabelColor(Qt::red);
    plot->yAxis2->setLabelColor(Qt::red);
    plot->yAxis2->setBasePen(QPen(Qt::red));
    plot->yAxis2->setTickPen(QPen(Qt::red));
    plot->yAxis2->setSubTickPen(QPen(Qt::red));
    
    // Define colors for different traces
    QVector<QColor> colors = {
        Qt::blue, Qt::red, Qt::green, Qt::magenta, Qt::cyan, 
        Qt::darkBlue, Qt::darkRed, Qt::darkGreen, Qt::darkMagenta, Qt::darkCyan
    };
    
    int maxSize = 0;
    double ch1Min = 0, ch1Max = 0, ch2Min = 0, ch2Max = 0;
    bool ch1DataFound = false, ch2DataFound = false;
    
    // Add CH1 traces
    for (int i = 0; i < ch1Traces.size(); ++i) {
        plot->addGraph();
        int graphIndex = plot->graphCount() - 1;
        
        QVector<double> x(ch1Traces[i].size());
        QVector<double> ch1_shifted(ch1Traces[i].size());
        for (int j = 0; j < x.size(); ++j) {
            x[j] = j * multiplier;
            ch1_shifted[j] = ch1Traces[i][j] * ch1Gain;
        }
        
        plot->graph(graphIndex)->setData(x, ch1_shifted);
        plot->graph(graphIndex)->setPen(QPen(colors[i % colors.size()], 2));
        plot->graph(graphIndex)->setVisible(true);
        plot->graph(graphIndex)->setValueAxis(plot->yAxis);
        plot->graph(graphIndex)->setName(QString("CH1 Trace %1").arg(i + 1));
        
        // Track min/max for CH1 axis scaling
        if (!ch1DataFound) {
            ch1Min = ch1Max = ch1_shifted[0];
            ch1DataFound = true;
        }
        for (double val : ch1_shifted) {
            ch1Min = qMin(ch1Min, val);
            ch1Max = qMax(ch1Max, val);
        }
        
        maxSize = qMax(maxSize, x.size());
    }
    
    // Add CH2 traces
    for (int i = 0; i < ch2Traces.size(); ++i) {
        plot->addGraph();
        int graphIndex = plot->graphCount() - 1;
        
        QVector<double> x(ch2Traces[i].size());
        QVector<double> ch2_shifted(ch2Traces[i].size());
        for (int j = 0; j < x.size(); ++j) {
            x[j] = j * multiplier;
            ch2_shifted[j] = ch2Traces[i][j] * ch2Gain; // Use CH2 gain
        }
        
        plot->graph(graphIndex)->setData(x, ch2_shifted);
        plot->graph(graphIndex)->setPen(QPen(colors[(i + ch1Traces.size()) % colors.size()], 2, Qt::DashLine));
        plot->graph(graphIndex)->setVisible(true);
        plot->graph(graphIndex)->setValueAxis(plot->yAxis2); // Use CH2 axis (yAxis2)
        plot->graph(graphIndex)->setName(QString("CH2 Trace %1").arg(i + 1));
        
        // Track min/max for CH2 axis scaling
        if (!ch2DataFound) {
            ch2Min = ch2Max = ch2_shifted[0];
            ch2DataFound = true;
        }
        for (double val : ch2_shifted) {
            ch2Min = qMin(ch2Min, val);
            ch2Max = qMax(ch2Max, val);
        }
        
        maxSize = qMax(maxSize, x.size());
    }
    
    // Set axis labels and ranges
    plot->xAxis->setLabel("Time (μs)");
    plot->yAxis->setLabel("CH1 Volts");
    plot->yAxis2->setLabel("CH2 Volts");
    
    // Set x-axis range to 0-100μs (fixed range for comparison)
    plot->xAxis->setRange(0, 100);
    
    // Set Y-axis ranges based on CH1's gain (as requested by user)
    double ch1Range = getYAxisRangeFromGain(ch1Gain);
    plot->yAxis->setRange(-ch1Range, ch1Range);
    plot->yAxis2->setRange(-ch1Range, ch1Range);
    
    // Always show trigger level line
    plotTriggerLine();
    plot->replot();
} 