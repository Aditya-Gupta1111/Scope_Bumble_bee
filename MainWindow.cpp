#include "MainWindow.h"
#include "SerialHandler.h"
#include "PlotManager.h"
#include "DDSGenerator.h"
#include "DigitalIO.h"
#include "WaveformExporter.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QDebug>
#include <QThread>
#include <cmath>
#include <complex>
#include <valarray>
#include <algorithm>
#include <numeric>
#include <QToolButton>
#include <QDialog>
#include <QMouseEvent>
#include <QHBoxLayout>
#include "qcustomplot.h"
#include <QSharedPointer>

// Define static constants
const int MainWindow::MAX_DATA_LENGTH;
const int MainWindow::MAX_DUAL_CHANNEL_LENGTH;

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

// DraggableWidget implementation
DraggableWidget::DraggableWidget(QWidget* parent) : QWidget(parent), m_dragging(false) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, false); // Remove transparent background
    setAttribute(Qt::WA_DeleteOnClose, false);

    // Set up styling for translucent appearance
    setStyleSheet(
        "QWidget {"
        "   background-color: rgba(240, 240, 240, 220);" // Translucent light gray
        "   border: 2px solid #666666;"
        "   border-radius: 8px;"
        "   padding: 5px;"
        "}"
        "QLabel {"
        "   background-color: transparent;"
        "   color: #333333;"
        "   font-size: 10px;"
        "}"
    );
}

void DraggableWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
    }
}

void DraggableWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
}

void DraggableWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      DDS_Waveform(256, 0),
      DDS_Table(512, 0),
      arb_data(256, 0),
      SetPeriodCmd(3, 0),
      SamplesCmd(3, 0),
      DDS_OutCmd(),
      RunDDSCmd(3, 0),
      Frequency(1000),
      serialHandler(new SerialHandler(this)),
      plotManager(new PlotManager(this)),
      ddsGenerator(nullptr),      // These will be implemented later
      digitalIO(nullptr),        // or integrated directly if simple
      waveformExporter(new WaveformExporter(this)), //
      portScanTimer(nullptr),
      lastConnectedPort("")
{
    // Initialize all UI pointers to nullptr
    serialPortCombo = nullptr;
    connectButton = nullptr;
    statusLabel = nullptr;
    runBtn = nullptr;
    stopBtn = nullptr;
    abortBtn = nullptr;
    exportBtn = nullptr;
    modeCombo = nullptr;
    sampleRateCombo = nullptr;
    bothChRadio = nullptr;
    ch1Radio = nullptr;
    ch2Radio = nullptr;
    xyRadio = nullptr;
    fftCh1Radio = nullptr;
    fftCh2Radio = nullptr;
    continuousRadio = nullptr;
    overwriteRadio = nullptr;
    addRadio = nullptr;
    ch1GainCombo = nullptr;
    ch2GainCombo = nullptr;
    ch1OffsetSlider = nullptr;
    ch2OffsetSlider = nullptr;
    trigLevelSlider = nullptr;
    ch1OffsetEdit = nullptr;
    ch2OffsetEdit = nullptr;
    trigLevelEdit = nullptr;
    autoTrigRadio = nullptr;
    ch1TrigRadio = nullptr;
    ch2TrigRadio = nullptr;
    extTrigRadio = nullptr;
    lhTrigRadio = nullptr;
    hlTrigRadio = nullptr;
    ddsWaveformCombo = nullptr;
    ddsFreqSpin = nullptr;
    ddsStartStopBtn = nullptr;
    ddsLoadArbBtn = nullptr;
    ddsWaveformList = nullptr;
    for (int i = 0; i < 4; ++i) {
        digitalOutButtons[i] = nullptr;
        digitalInLabels[i] = nullptr;
    }
    readDigitalBtn = nullptr;
    digFreqSpin = nullptr;
    digDividerCombo = nullptr;
    digFreqStartBtn = nullptr;
    sweepStartSpin = nullptr;
    sweepEndSpin = nullptr;
    sweepSamplesSpin = nullptr;
    sweepDelaySpin = nullptr;
    sweepStartBtn = nullptr;
    sweepProgress = nullptr;
    clearBodeBtn = nullptr;
    exportBodeBtn = nullptr;
    studentNameEdit = nullptr;
    signatureEdit = nullptr;
    plot = nullptr;
    bodePlot = nullptr;
    tabWidget = nullptr;
    oscTab = nullptr;
    settingsTab = nullptr;

    // Initialize timers
    plotTimer = new QTimer(this);
    dataRequestTimer = new QTimer(this);
    sweepTimer = new QTimer(this);

    portScanTimer = new QTimer(this);
    connect(portScanTimer, &QTimer::timeout, this, &MainWindow::autoDetectAndConnectBoard);
    portScanTimer->start(1000); // Check every second

    plotRateLimitTimer = new QTimer(this);
    plotRateLimitTimer->setSingleShot(true);
    plotRateLimitTimer->setInterval(200); // Limit plotting to max 5 times per second

    // Initialize accumulated data variables
    overwriteAcquisitionCount = 0;
    addModeAcquisitionCount = 0;

    initializeWaveformTables();
    setupUi();
    setupConnections();

    // Initialize PlotManager with default ±20V gain settings
    if (plotManager) {
        plotManager->setGains(1.0, 1.0); // ±20V for both channels
    }

    // Initialize gain values by triggering the change handlers
    qDebug() << "[MainWindow] Initializing gains - CH1 combo index:" << (ch1GainCombo ? ch1GainCombo->currentIndex() : -1)
             << ", CH2 combo index:" << (ch2GainCombo ? ch2GainCombo->currentIndex() : -1);

    // Force set both gains to ±20V (0.5) regardless of combo box state
    ch1Gain = 1;
    ch2Gain = 1;

    // Update PlotManager with correct gains and X-axis title
    if (plotManager) {
        plotManager->setGains(ch1Gain, ch2Gain);
        plotManager->setXAxisTitle(heading1); // Set initial X-axis title
    }

    // Update SerialHandler with correct gains
    if (serialHandler) {
    }

    qDebug() << "[MainWindow] Initialized CH1 gain to:" << ch1Gain << ", CH2 gain to:" << ch2Gain;

    // Verify gains are set correctly in SerialHandler
    if (serialHandler) {
        qDebug() << "[MainWindow] SerialHandler gains - CH1:" << static_cast<int>(ch1Gain) << "CH2:" << static_cast<int>(ch2Gain);
    }

    updateUiState();
    updateSerialPortList();

    // plotTimer->start(33); // ~30 FPS
    autoDetectAndConnectBoard();
}

MainWindow::~MainWindow()
{
    // Qt's parent-child mechanism handles deletion of UI elements
}

void MainWindow::setupUi()
{
    setWindowTitle("Advanced Oscilloscope");
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // --- Top Control Bar (Serial, Run/Stop) ---
    QHBoxLayout *topBarLayout = new QHBoxLayout();

    QGroupBox *serialGroup = new QGroupBox("Connection");
    QHBoxLayout *serialLayout = new QHBoxLayout(serialGroup);
    serialPortCombo = new QComboBox();
    connectButton = new QPushButton("Connect");
    serialLayout->addWidget(new QLabel("Port:"));
    serialLayout->addWidget(serialPortCombo);
    serialLayout->addWidget(connectButton);
    topBarLayout->addWidget(serialGroup);

    QGroupBox *runGroup = new QGroupBox("Acquisition");
    QHBoxLayout *runLayout = new QHBoxLayout(runGroup);
    runBtn = new QPushButton("Run");
    stopBtn = new QPushButton("Stop");
    abortBtn = new QPushButton("Abort");
    runLayout->addWidget(runBtn);
    runLayout->addWidget(stopBtn);
    runLayout->addWidget(abortBtn);
    topBarLayout->addWidget(runGroup);

    // --- Add Low-pass Filter Checkbox ---
    QCheckBox *lpfCheckBox = new QCheckBox("Low-pass filter (no ripples)");
    lpfCheckBox->setToolTip("Enable low-pass filtering to remove ripples. First 10 points will be ignored.");
    topBarLayout->addWidget(lpfCheckBox);
    // Store pointer for later use
    this->lpfCheckBox = lpfCheckBox;

    topBarLayout->addStretch();

    mainLayout->addLayout(topBarLayout);

    // --- Main Area (Plot and Right Panel) ---
    mainAreaLayout = new QHBoxLayout();
    plot = static_cast<QCustomPlot*>(plotManager->plotWidget());
    mainAreaLayout->addWidget(plot, 1); // Always add oscilloscope plot at startup
    rightPanelTabs = new QTabWidget();
    rightPanelTabs->setFixedWidth(350);
    QWidget *scopeTab = new QWidget();
    QWidget *ddsTab = new QWidget();
    QWidget *bodeTab = new QWidget();
    QWidget *digiTab = new QWidget();

    rightPanelTabs->addTab(scopeTab, "Scope");
    rightPanelTabs->addTab(ddsTab, "DDS Gen");
    rightPanelTabs->addTab(bodeTab, "Bode Plot");
    rightPanelTabs->addTab(digiTab, "Digital");

    // --- Scope Tab ---
    QVBoxLayout *scopeTabLayout = new QVBoxLayout(scopeTab);

    // Display Mode
    QGroupBox *modeGroup = new QGroupBox("Display Mode");
    QGridLayout *modeLayout = new QGridLayout(modeGroup);
    bothChRadio = new QRadioButton("Both Channels");
    ch1Radio = new QRadioButton("Channel 1");
    ch2Radio = new QRadioButton("Channel 2");
    xyRadio = new QRadioButton("XY Mode");
    fftCh1Radio = new QRadioButton("FFT CH1");
    fftCh2Radio = new QRadioButton("FFT CH2");
    QRadioButton* fftBothRadio = new QRadioButton("FFT Both CH1 & CH2"); // NEW
    modeLayout->addWidget(bothChRadio, 0, 0);
    modeLayout->addWidget(ch1Radio, 1, 0);
    modeLayout->addWidget(ch2Radio, 2, 0);
    modeLayout->addWidget(xyRadio, 0, 1);
    modeLayout->addWidget(fftCh1Radio, 1, 1);
    modeLayout->addWidget(fftCh2Radio, 2, 1);
    modeLayout->addWidget(fftBothRadio, 3, 1); // NEW
    bothChRadio->setChecked(true);
    scopeTabLayout->addWidget(modeGroup);

    // Sample Rate
    QGroupBox *sampleRateGroup = new QGroupBox("Sample Rate");
    QHBoxLayout *sampleRateLayout = new QHBoxLayout(sampleRateGroup);
    sampleRateCombo = new QComboBox();
    sampleRateCombo->addItems({
        "2Mbps  0.50us/sample", "1Mbps   1.0us/sample", "500kbps 2.0us/sample",
        "200kbps 5.0us/sample", "100kbps  10us/sample", "50kbps   20us/sample",
        "20kbps   50us/sample", "10kbps  100us/sample", "5kbps   200us/sample",
        "2kbps   500us/sample", "1kbps   1.0ms/sample", "500Hz   2.0ms/sample",
        "200Hz   5.0ms/sample", "100Hz    10ms/sample "
    });
    sampleRateCombo->setCurrentIndex(3);
    sampleRateLayout->addWidget(sampleRateCombo);
    scopeTabLayout->addWidget(sampleRateGroup);

    // Run Mode
    QGroupBox *runModeGroup = new QGroupBox("Run Mode");
    QHBoxLayout *runModeLayout = new QHBoxLayout(runModeGroup);
    continuousRadio = new QRadioButton("Continuous");
    overwriteRadio = new QRadioButton("Overwrite");
    addRadio = new QRadioButton("ADD");
    runModeLayout->addWidget(continuousRadio);
    runModeLayout->addWidget(overwriteRadio);
    runModeLayout->addWidget(addRadio);
    // Default to Overwrite mode for proper trigger functionality
    overwriteRadio->setChecked(true);
    continuousRadio->setChecked(false);
    scopeTabLayout->addWidget(runModeGroup);

    // Channel 1 & 2 Controls
    QGroupBox *chGroup = new QGroupBox("Channels");
    QGridLayout *chLayout = new QGridLayout(chGroup);
    if (!ch1GainCombo) {
        ch1GainCombo = new QComboBox(this);
        ch1GainCombo->addItem("±20V", 1.0);
        ch1GainCombo->addItem("±10V", 2.0);
        ch1GainCombo->addItem("±5V", 4.0);
        ch1GainCombo->addItem("±2.5V", 8.0);
        ch1GainCombo->addItem("±1.25V", 16.0);
        ch1GainCombo->addItem("±0.625V", 32.0);
        ch1GainCombo->setCurrentIndex(0); // Set to ±20V by default
        connect(ch1GainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onCh1GainChanged);
    }
    chLayout->addWidget(new QLabel("CH1 Gain:"), 0, 0);
    chLayout->addWidget(ch1GainCombo, 0, 1);
    ch1OffsetSlider = new QSlider(Qt::Horizontal);
    ch1OffsetSlider->setRange(-1694, 1695); // Range from -16.94 to +16.95 (scaled by 100)
    ch1OffsetSlider->setValue(0); // Center at 0V
    ch1OffsetEdit = new QLineEdit("0.00V");
    ch1OffsetEdit->setMinimumWidth(60);
    ch1OffsetEdit->setAlignment(Qt::AlignCenter);
    chLayout->addWidget(new QLabel("CH1 Offset:"), 1, 0);
    chLayout->addWidget(ch1OffsetSlider, 1, 1);
    chLayout->addWidget(ch1OffsetEdit, 1, 2);

    if (!ch2GainCombo) {
        ch2GainCombo = new QComboBox(this);
        ch2GainCombo->addItem("±20V", 1.0);
        ch2GainCombo->addItem("±10V", 2.0);
        ch2GainCombo->addItem("±5V", 4.0);
        ch2GainCombo->addItem("±2.5V", 8.0);
        ch2GainCombo->addItem("±1.25V", 16.0);
        ch2GainCombo->addItem("±0.625V", 32.0);
        ch2GainCombo->setCurrentIndex(0); // Set to ±20V by default
        connect(ch2GainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onCh2GainChanged);
    }
    chLayout->addWidget(new QLabel("CH2 Gain:"), 2, 0);
    chLayout->addWidget(ch2GainCombo, 2, 1);
    ch2OffsetSlider = new QSlider(Qt::Horizontal);
    ch2OffsetSlider->setRange(-1694, 1695); // Range from -16.94 to +16.95 (scaled by 100)
    ch2OffsetSlider->setValue(0); // Center at 0V
    ch2OffsetEdit = new QLineEdit("0.00V");
    ch2OffsetEdit->setMinimumWidth(60);
    ch2OffsetEdit->setAlignment(Qt::AlignCenter);
    chLayout->addWidget(new QLabel("CH2 Offset:"), 3, 0);
    chLayout->addWidget(ch2OffsetSlider, 3, 1);
    chLayout->addWidget(ch2OffsetEdit, 3, 2);
    scopeTabLayout->addWidget(chGroup);

    // Trigger Controls
    QGroupBox *trigGroup = new QGroupBox("Trigger");
    QGridLayout *trigLayout = new QGridLayout(trigGroup);
    autoTrigRadio = new QRadioButton("Auto");
    ch1TrigRadio = new QRadioButton("CH1");
    ch2TrigRadio = new QRadioButton("CH2");
    extTrigRadio = new QRadioButton("External");
    trigLayout->addWidget(autoTrigRadio, 0, 0);
    trigLayout->addWidget(ch1TrigRadio, 0, 1);
    trigLayout->addWidget(ch2TrigRadio, 1, 0);
    trigLayout->addWidget(extTrigRadio, 1, 1);
    autoTrigRadio->setChecked(true);

    lhTrigRadio = new QRadioButton("L->H");
    hlTrigRadio = new QRadioButton("H->L");
    trigLayout->addWidget(lhTrigRadio, 2, 0);
    trigLayout->addWidget(hlTrigRadio, 2, 1);
    lhTrigRadio->setChecked(true);

    trigLevelSlider = new QSlider(Qt::Horizontal);
    trigLevelSlider->setRange(0, 4095); trigLevelSlider->setValue(2048);
    trigLevelEdit = new QLineEdit("0.00V");
    trigLevelEdit->setMinimumWidth(60);
    trigLevelEdit->setAlignment(Qt::AlignCenter);
    trigLayout->addWidget(new QLabel("Level:"), 3, 0);
    trigLayout->addWidget(trigLevelSlider, 3, 1);
    trigLayout->addWidget(trigLevelEdit, 3, 2);
    scopeTabLayout->addWidget(trigGroup);

    exportBtn = new QPushButton("Export to CSV");
    scopeTabLayout->addWidget(exportBtn);

    // Show Raw ADC Values Checkbox and Debug Terminal
    // showRawAdcCheckBox = new QCheckBox("Show Raw ADC Values");
    // scopeTabLayout->addWidget(showRawAdcCheckBox);
    // rawAdcTerminal = new QTextEdit();
    // rawAdcTerminal->setReadOnly(true);
    // rawAdcTerminal->setMinimumHeight(100);
    // rawAdcTerminal->setVisible(false);
    // scopeTabLayout->addWidget(rawAdcTerminal);
    // connect(showRawAdcCheckBox, &QCheckBox::toggled, this, [this](bool checked){
    //     rawAdcTerminal->setVisible(checked);
    //     if (!checked) rawAdcTerminal->clear();
    // });



    // Connect run mode radio buttons
    if (overwriteRadio) {
        connect(overwriteRadio, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                // Overwrite mode: reset trace collection and run count
                qDebug() << "[MainWindow] Overwrite mode selected - resetting trace collection and run count";
                resetTraceCollection();
                runCount = 0;
                targetTraceCount = 2;
                // Stop plot timer for trigger mode
                plotTimer->stop();
            }
        });
    }

    if (addRadio) {
        connect(addRadio, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                // ADD mode: reset trace collection and run count
                qDebug() << "[MainWindow] ADD mode selected - resetting trace collection and run count";
                resetTraceCollection();
                runCount = 0;
                targetTraceCount = 2;
                // Stop plot timer for trigger mode
                plotTimer->stop();
            }
        });
    }

    if (continuousRadio) {
        connect(continuousRadio, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked) {
                qDebug() << "[MainWindow] Continuous mode selected";
                // Reset trace collection for continuous mode
                resetTraceCollection();
                // Start plot timer for continuous mode if running
                if (isRunning) {
                    plotTimer->start(33); // ~30 FPS for smooth real-time updates
                }
            }
        });
    }

    scopeTabLayout->addStretch();

    // --- DDS Gen Tab ---
    QVBoxLayout *ddsTabLayout = new QVBoxLayout(ddsTab);
    QGroupBox *ddsWaveGroup = new QGroupBox("DDS Waveform");
    QVBoxLayout *ddsWaveLayout = new QVBoxLayout(ddsWaveGroup);
    ddsWaveformCombo = new QComboBox();
    ddsWaveformCombo->addItems({
        "DDS Sin (1-50 kHz)",
        "DDS Sqare(1-50 kHz)",
        "DDS Tri (1-50 kHz)",
        "DDS RampUp (1-50 kHz)",
        "DDS RampDn (1-50 kHz)",
        "DDS Arb (1-50 kHz)"
    });
    ddsWaveLayout->addWidget(ddsWaveformCombo);
    // Add Generate Signal button below waveform selection
    QPushButton *generateSignalBtn = new QPushButton("Generate Signal");
    ddsWaveLayout->addWidget(generateSignalBtn);
    connect(generateSignalBtn, &QPushButton::clicked, this, &MainWindow::onRunDDSButtonClicked);
    ddsLoadArbBtn = new QPushButton("Load Arbitrary Waveform");
    ddsWaveLayout->addWidget(ddsLoadArbBtn);
    ddsWaveGroup->setLayout(ddsWaveLayout);
    ddsTabLayout->addWidget(ddsWaveGroup);

    QGroupBox *ddsControlGroup = new QGroupBox("DDS Control");
    QGridLayout *ddsControlLayout = new QGridLayout(ddsControlGroup);
    ddsFreqSpin = new QDoubleSpinBox();
    ddsFreqSpin->setRange(1, 50000); ddsFreqSpin->setValue(1000); ddsFreqSpin->setSuffix(" Hz");
    ddsControlLayout->addWidget(new QLabel("Frequency:"), 0, 0);
    ddsControlLayout->addWidget(ddsFreqSpin, 0, 1);
    ddsStartStopBtn = new QPushButton("Start DDS");
    ddsControlLayout->addWidget(ddsStartStopBtn, 1, 0, 1, 2);
    ddsTabLayout->addWidget(ddsControlGroup);
    ddsTabLayout->addStretch();

    // --- Bode Plot Tab ---
    QVBoxLayout *bodeTabLayout = new QVBoxLayout(bodeTab);

    // Bode Plot Display Area
    QGroupBox *bodePlotGroup = new QGroupBox("Bode Plot Display");
    QVBoxLayout *bodePlotLayout = new QVBoxLayout(bodePlotGroup);

    // Create a separate plot for Bode plot
    bodePlot = new QCustomPlot();
    bodePlot->setMinimumHeight(300); // Increased from 200 to 300 for better visibility
    bodePlot->xAxis->setLabel("Frequency (Hz)");
    bodePlot->xAxis->setScaleType(QCPAxis::stLogarithmic);

    // Setup left Y-axis for magnitude
    bodePlot->yAxis->setLabel("Magnitude (dB)");
    bodePlot->yAxis->setLabelColor(Qt::blue);
    bodePlot->yAxis->setTickLabelColor(Qt::blue);
    bodePlot->yAxis->setBasePen(QPen(Qt::blue));
    bodePlot->yAxis->setTickPen(QPen(Qt::blue));
    bodePlot->yAxis->setSubTickPen(QPen(Qt::blue));

    // Setup right Y-axis for phase (when we add phase data later)
    bodePlot->yAxis2->setVisible(true);
    bodePlot->yAxis2->setLabel("Phase (degrees)");
    bodePlot->yAxis2->setLabelColor(Qt::red);
    bodePlot->yAxis2->setTickLabelColor(Qt::red);
    bodePlot->yAxis2->setBasePen(QPen(Qt::red));
    bodePlot->yAxis2->setTickPen(QPen(Qt::red));
    bodePlot->yAxis2->setSubTickPen(QPen(Qt::red));
            // Ensure phase axis shows tick values
        bodePlot->yAxis2->setTickLabelFont(QFont("Arial", 8));
        bodePlot->yAxis2->setTickLength(5, 3); // Main tick length, sub-tick length
        bodePlot->yAxis2->setNumberFormat("f"); // Show decimal format
        bodePlot->yAxis2->setNumberPrecision(1); // Precision for tick labels
    bodePlot->legend->setVisible(true);

    bodePlot->axisRect()->setupFullAxesBox(true);
    bodePlot->axisRect()->setMargins(QMargins(60, 20, 20, 60)); // Left, Top, Right, Bottom
    bodePlotLayout->addWidget(bodePlot);

    // Bode plot controls
    QHBoxLayout *bodeControlLayout = new QHBoxLayout();
    clearBodeBtn = new QPushButton("Clear Plot");
    exportBodeBtn = new QPushButton("Export Bode Data");
    bodeControlLayout->addWidget(clearBodeBtn);
    bodeControlLayout->addWidget(exportBodeBtn);
    bodePlotLayout->addLayout(bodeControlLayout);

    bodeTabLayout->addWidget(bodePlotGroup);

    // DDS Sweep Controls
    QGroupBox *ddsSweepGroup = new QGroupBox("DDS Sine Sweep for Bode Plot");
    QGridLayout *ddsSweepLayout = new QGridLayout(ddsSweepGroup);

    // Frequency range controls
    sweepStartSpin = new QDoubleSpinBox();
    sweepStartSpin->setRange(10, 20000);
    sweepStartSpin->setValue(100);
    sweepStartSpin->setSuffix(" Hz");
    sweepStartSpin->setDecimals(0);

    sweepEndSpin = new QDoubleSpinBox();
    sweepEndSpin->setRange(10, 20000);
    sweepEndSpin->setValue(10000);
    sweepEndSpin->setSuffix(" Hz");
    sweepEndSpin->setDecimals(0);

    sweepSamplesSpin = new QSpinBox();
    sweepSamplesSpin->setRange(10, 1000);
    sweepSamplesSpin->setValue(100);
    sweepSamplesSpin->setSuffix(" points");

    sweepDelaySpin = new QSpinBox();
    sweepDelaySpin->setRange(100, 1000);
    sweepDelaySpin->setValue(100);
    sweepDelaySpin->setSuffix(" ms");

    // Layout for automatic sweep controls
    ddsSweepLayout->addWidget(new QLabel("Start Frequency:"), 0, 0);
    ddsSweepLayout->addWidget(sweepStartSpin, 0, 1);
    ddsSweepLayout->addWidget(new QLabel("End Frequency:"), 1, 0);
    ddsSweepLayout->addWidget(sweepEndSpin, 1, 1);
    ddsSweepLayout->addWidget(new QLabel("Number of Steps:"), 2, 0);
    ddsSweepLayout->addWidget(sweepSamplesSpin, 2, 1);
    ddsSweepLayout->addWidget(new QLabel("Delay per Step:"), 3, 0);
    ddsSweepLayout->addWidget(sweepDelaySpin, 3, 1);

    // Sweep control buttons
    sweepStartBtn = new QPushButton("Start Sweep");
    sweepStartBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }");

    stopSweepBtn = new QPushButton("Stop Sweep");
    stopSweepBtn->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; }");

    QHBoxLayout *sweepBtnLayout = new QHBoxLayout();
    sweepBtnLayout->addWidget(sweepStartBtn);
    sweepBtnLayout->addWidget(stopSweepBtn);
    ddsSweepLayout->addLayout(sweepBtnLayout, 4, 0, 1, 2);

    // Progress bar
    sweepProgress = new QProgressBar();
    sweepProgress->setVisible(false);
    ddsSweepLayout->addWidget(sweepProgress, 5, 0, 1, 2);

    bodeTabLayout->addWidget(ddsSweepGroup);
    bodeTabLayout->addStretch();

    // --- Digital Tab ---
    QVBoxLayout *digiTabLayout = new QVBoxLayout(digiTab);
    QGroupBox *digFreqGroup = new QGroupBox("Digital Frequency Generator");
    QGridLayout *digFreqLayout = new QGridLayout(digFreqGroup);
    digFreqSpin = new QDoubleSpinBox(); digFreqSpin->setRange(1, 100000); digFreqSpin->setValue(10000);
    digFreqLayout->addWidget(new QLabel("Frequency:"), 0, 0); digFreqLayout->addWidget(digFreqSpin, 0, 1);
    digFreqStartBtn = new QPushButton("Start Freq Gen");
    digFreqLayout->addWidget(digFreqStartBtn, 1, 0, 1, 2);
    digiTabLayout->addWidget(digFreqGroup);

    QGroupBox *digIoGroup = new QGroupBox("Digital I/O");
    QGridLayout *digIoLayout = new QGridLayout(digIoGroup);
    digIoLayout->addWidget(new QLabel("Out:"), 0, 0);
    for(int i=0; i<4; ++i) {
        digitalOutButtons[i] = new QPushButton(QString("D%1 L").arg(i));
        digitalOutButtons[i]->setCheckable(true);
        digIoLayout->addWidget(digitalOutButtons[i], 0, i+1);
    }
    digIoLayout->addWidget(new QLabel("In:"), 1, 0);
    for(int i=0; i<4; ++i) {
        digitalInLabels[i] = new QLabel("L");
        digitalInLabels[i]->setAlignment(Qt::AlignCenter);
        digitalInLabels[i]->setFrameShape(QFrame::Panel);
        digitalInLabels[i]->setFrameShadow(QFrame::Sunken);
        digIoLayout->addWidget(digitalInLabels[i], 1, i+1);
    }
    readDigitalBtn = new QPushButton("Read Inputs");
    digIoLayout->addWidget(readDigitalBtn, 2, 0, 1, 5);
    digiTabLayout->addWidget(digIoGroup);

    QGroupBox *studentGroup = new QGroupBox("Student Info");
    QGridLayout *studentLayout = new QGridLayout(studentGroup);
    studentNameEdit = new QLineEdit("Student Name");
    signatureEdit = new QLineEdit();
    signatureEdit->setReadOnly(true);
    studentLayout->addWidget(new QLabel("Name:"), 0, 0); studentLayout->addWidget(studentNameEdit, 0, 1);
    studentLayout->addWidget(new QLabel("Signature:"), 1, 0); studentLayout->addWidget(signatureEdit, 1, 1);
    digiTabLayout->addWidget(studentGroup);
    digiTabLayout->addStretch();

    mainAreaLayout->addWidget(rightPanelTabs);
    mainLayout->addLayout(mainAreaLayout);

    // Status Bar
    statusLabel = new QLabel("Disconnected");
    statusBar()->addWidget(statusLabel);
    // Add permanent label for company name
    QLabel* companyLabel = new QLabel("Bumbee Instruments ");
    statusBar()->addPermanentWidget(companyLabel);

    // Initialize trigger line in plot
    if (plotManager) {
        // Calculate initial trigger level voltage (0V for default slider position 2048)
        double initialVoltage = (trigLevel * 10.0 / 2048.0 - 10.0) / 1.0; // Default gain = 1.0
        initialVoltage = qRound(initialVoltage * 100.0) / 100.0;
        plotManager->updateTriggerLevel(initialVoltage, false); // Default to CH1
    }

    // Add RUN DDS button
    QPushButton *runDDSBtn = new QPushButton("RUN DDS");
    ddsTabLayout->addWidget(runDDSBtn);
    connect(runDDSBtn, &QPushButton::clicked, this, &MainWindow::onRunDDSButtonClicked);

    // --- Remove Measurements Box from Scope Tab ---
    // (Removed QGroupBox *measGroup1 and its labels)
    // (Removed floatingMeasBox and measurement edit button from right panel)
    // (No changes to measurement calculation logic)

    // --- Connect tab change to main area plot switch ---
    if (rightPanelTabs) {
        connect(rightPanelTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    }
}

void MainWindow::setupConnections()
{
    // Serial
    if (connectButton)
        connect(connectButton, &QPushButton::clicked, this, &MainWindow::onConnectButtonClicked);
    if (serialHandler) {
        connect(serialHandler, &SerialHandler::connectionStatus, this, &MainWindow::handleSerialConnectionStatus);
        connect(serialHandler, &SerialHandler::portError, this, &MainWindow::handleSerialPortError);
    }
    // connect(serialHandler, &SerialHandler::dataReceived, this, &MainWindow::handleSerialData); // Disabled legacy data handling

    // Scope Controls
    if (runBtn)
        connect(runBtn, &QPushButton::clicked, this, &MainWindow::onRunClicked);
    if (stopBtn)
        connect(stopBtn, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    if (abortBtn)
        connect(abortBtn, &QPushButton::clicked, this, &MainWindow::onAbortClicked);
    if (exportBtn)
        connect(exportBtn, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    if (sampleRateCombo)
        connect(sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSampleRateChanged);

    // Channel Controls
    if (ch1GainCombo)
        connect(ch1GainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onCh1GainChanged);
    if (ch2GainCombo)
        connect(ch2GainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onCh2GainChanged);
    if (ch1OffsetSlider)
        connect(ch1OffsetSlider, &QSlider::valueChanged, this, &MainWindow::onCh1OffsetChanged);
    if (ch2OffsetSlider)
        connect(ch2OffsetSlider, &QSlider::valueChanged, this, &MainWindow::onCh2OffsetChanged);

    // Trigger
    auto trigSourceGroup = new QButtonGroup(this);
    if (autoTrigRadio) trigSourceGroup->addButton(autoTrigRadio, 0);
    if (ch1TrigRadio) trigSourceGroup->addButton(ch1TrigRadio, 1);
    if (ch2TrigRadio) trigSourceGroup->addButton(ch2TrigRadio, 2);
    if (extTrigRadio) trigSourceGroup->addButton(extTrigRadio, 3);
    connect(trigSourceGroup, &QButtonGroup::idClicked, this, &MainWindow::onTrigSourceChanged);

    auto trigPolGroup = new QButtonGroup(this);
    if (lhTrigRadio) trigPolGroup->addButton(lhTrigRadio, 0);
    if (hlTrigRadio) trigPolGroup->addButton(hlTrigRadio, 1);
    connect(trigPolGroup, &QButtonGroup::idClicked, this, &MainWindow::onTrigPolarityChanged);
    if (trigLevelSlider)
        connect(trigLevelSlider, &QSlider::valueChanged, this, &MainWindow::onTrigLevelChanged);

    // Add connection for trigger level edit field
    if (trigLevelEdit)
        connect(trigLevelEdit, &QLineEdit::editingFinished, this, [this]() {
            QString text = trigLevelEdit->text();
            text.remove("V"); // Remove "V" suffix if present
            bool ok;
            double voltage = text.toDouble(&ok);
            if (ok) {
                // Convert voltage back to slider value using VB.NET logic
                double gain = 1.0;
                if (ch1TrigRadio && ch1TrigRadio->isChecked()) {
                    gain = ch1Gain;
                } else if (ch2TrigRadio && ch2TrigRadio->isChecked()) {
                    gain = ch2Gain;
                }
                // Reverse VB.NET calculation: value = ((voltage * gain + 10.0) * 2048.0) / 10.0
                int sliderValue = static_cast<int>(((voltage * gain + 10.0) * 2048.0) / 10.0);
                sliderValue = qBound(0, sliderValue, 4095); // Clamp to slider range
                trigLevelSlider->setValue(sliderValue);
            }
        });

    // Display Mode
    auto modeGroup = new QButtonGroup(this);
    if (bothChRadio) modeGroup->addButton(bothChRadio, 0);
    if (ch1Radio) modeGroup->addButton(ch1Radio, 1);
    if (ch2Radio) modeGroup->addButton(ch2Radio, 2);
    if (xyRadio) modeGroup->addButton(xyRadio, 3);
    if (fftCh1Radio) modeGroup->addButton(fftCh1Radio, 4);
    if (fftCh2Radio) modeGroup->addButton(fftCh2Radio, 5);
    if (fftBothRadio) modeGroup->addButton(fftBothRadio, 6); // NEW
    connect(modeGroup, &QButtonGroup::idClicked, this, &MainWindow::onModeChanged);

    // DDS
    if (ddsStartStopBtn)
        connect(ddsStartStopBtn, &QPushButton::clicked, this, &MainWindow::onDDSStartStopClicked);
    if (ddsLoadArbBtn)
        connect(ddsLoadArbBtn, &QPushButton::clicked, this, &MainWindow::onDDSLoadArbClicked);
    if (ddsWaveformCombo)
        connect(ddsWaveformCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDDSWaveformChanged);
    if (ddsFreqSpin)
        connect(ddsFreqSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::onDDSFreqChanged);

    // Sweep
    if (sweepStartBtn)
        connect(sweepStartBtn, &QPushButton::clicked, this, &MainWindow::onSweepStartStopClicked);
    if (stopSweepBtn)
        connect(stopSweepBtn, &QPushButton::clicked, this, &MainWindow::onStopSweepClicked);
    if (sweepStartSpin)
        connect(sweepStartSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::onSweepStartFreqChanged);
    if (sweepEndSpin)
        connect(sweepEndSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::onSweepEndFreqChanged);
    if (sweepSamplesSpin)
        connect(sweepSamplesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onSweepSamplesChanged);
    if (sweepDelaySpin)
        connect(sweepDelaySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onSweepDelayChanged);

    // Bode Plot Controls
    if (clearBodeBtn)
        connect(clearBodeBtn, &QPushButton::clicked, this, [this]() {
            if (bodePlot) {
                bodePlot->clearGraphs();
                bodePlot->replot();
                qDebug() << "[MainWindow] Bode plot cleared";
            }
        });
    if (exportBodeBtn)
        connect(exportBodeBtn, &QPushButton::clicked, this, [this]() {
            if (!sweepFrequencies.isEmpty() && !sweepMagnitudes.isEmpty()) {
                QString fileName = QFileDialog::getSaveFileName(this, "Export Bode Data", "", "CSV Files (*.csv)");
                if (!fileName.isEmpty()) {
                    QFile file(fileName);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream out(&file);
                        out << "Frequency(Hz),Magnitude(dB),Phase(degrees)\n";
                        for (int i = 0; i < sweepFrequencies.size(); ++i) {
                            double phase = (i < sweepPhases.size()) ? sweepPhases[i] : 0.0;
                            out << sweepFrequencies[i] << "," << sweepMagnitudes[i] << "," << phase << "\n";
                        }
                        file.close();
                        showStatus("Bode data exported to " + fileName);
                        qDebug() << "[MainWindow] Bode data exported to" << fileName;
                    }
                }
            } else {
                qDebug() << "[MainWindow] No Bode data to export";
                showStatus("No Bode data available for export");
            }
        });

    // Digital
    if (digFreqStartBtn)
        connect(digFreqStartBtn, &QPushButton::clicked, this, &MainWindow::onDigFreqStartClicked);
    if (readDigitalBtn)
        connect(readDigitalBtn, &QPushButton::clicked, this, &MainWindow::onReadDigitalClicked);
    for(int i=0; i<4; ++i) {
        if (digitalOutButtons[i])
            connect(digitalOutButtons[i], &QPushButton::clicked, this, [this, i](){ onDigitalOutToggled(i); });
    }
    // Student Info
    if (studentNameEdit)
        connect(studentNameEdit, &QLineEdit::editingFinished, this, &MainWindow::onStudentNameChanged);
    // Timers
    if (plotTimer)
        connect(plotTimer, &QTimer::timeout, this, &MainWindow::updatePlot);
    if (dataRequestTimer)
        connect(dataRequestTimer, &QTimer::timeout, this, &MainWindow::requestOscilloscopeData);
    if (sweepTimer)
        connect(sweepTimer, &QTimer::timeout, this, &MainWindow::onSweepStartStopClicked);
    // Bidirectional connections for offset text boxes
    if (ch1OffsetEdit)
        connect(ch1OffsetEdit, &QLineEdit::editingFinished, this, [this]() {
            QString text = ch1OffsetEdit->text();
            text.remove("V"); // Remove "V" suffix if present
            bool ok;
            double voltage = text.toDouble(&ok);
            if (ok) {
                int sliderValue = static_cast<int>(voltage * 100.0); // Convert voltage to slider range
                sliderValue = qBound(-1694, sliderValue, 1695); // Clamp to slider range
                ch1OffsetSlider->setValue(sliderValue);
            }
        });
    if (ch2OffsetEdit)
        connect(ch2OffsetEdit, &QLineEdit::editingFinished, this, [this]() {
            QString text = ch2OffsetEdit->text();
            text.remove("V"); // Remove "V" suffix if present
            bool ok;
            double voltage = text.toDouble(&ok);
            if (ok) {
                int sliderValue = static_cast<int>(voltage * 100.0); // Convert voltage to slider range
                sliderValue = qBound(-1694, sliderValue, 1695); // Clamp to slider range
                ch2OffsetSlider->setValue(sliderValue);
            }
        });
    if (digFreqStartBtn && digFreqSpin) {
        connect(digFreqStartBtn, &QPushButton::clicked, this, [this]() {
            int freq = digFreqSpin->value();
            setDigitalFrequency(freq);
        });
    }
}

void MainWindow::updateUiState()
{
    bool connected = isConnected;
    bool running = isRunning;

    // Connection controls
    if (serialPortCombo) serialPortCombo->setEnabled(!connected);
    if (connectButton) connectButton->setText(connected ? "Disconnect" : "Connect");

    // Run/Stop controls
    if (runBtn) runBtn->setEnabled(connected && !running);
    if (stopBtn) stopBtn->setEnabled(connected && running);
    if (abortBtn) abortBtn->setEnabled(connected && running);

    // Keep all oscilloscope controls enabled even when running for real-time adjustments
    if (bothChRadio) bothChRadio->setEnabled(connected);
    if (ch1Radio) ch1Radio->setEnabled(connected);
    if (ch2Radio) ch2Radio->setEnabled(connected);
    if (xyRadio) xyRadio->setEnabled(connected);
    if (fftCh1Radio) fftCh1Radio->setEnabled(connected);
    if (fftCh2Radio) fftCh2Radio->setEnabled(connected);

    if (sampleRateCombo) sampleRateCombo->setEnabled(connected);
    if (ch1GainCombo) ch1GainCombo->setEnabled(connected);
    if (ch2GainCombo) ch2GainCombo->setEnabled(connected);
    if (ch1OffsetSlider) ch1OffsetSlider->setEnabled(connected);
    if (ch2OffsetSlider) ch2OffsetSlider->setEnabled(connected);
    if (trigLevelSlider) trigLevelSlider->setEnabled(connected);

    if (autoTrigRadio) autoTrigRadio->setEnabled(connected);
    if (ch1TrigRadio) ch1TrigRadio->setEnabled(connected);
    if (ch2TrigRadio) ch2TrigRadio->setEnabled(connected);
    if (extTrigRadio) extTrigRadio->setEnabled(connected);
    if (lhTrigRadio) lhTrigRadio->setEnabled(connected);
    if (hlTrigRadio) hlTrigRadio->setEnabled(connected);

    if (continuousRadio) continuousRadio->setEnabled(connected);
    if (overwriteRadio) overwriteRadio->setEnabled(connected);



    if (exportBtn) exportBtn->setEnabled(connected);

    // DDS controls
    if (ddsWaveformCombo) ddsWaveformCombo->setEnabled(connected);
    if (ddsFreqSpin) ddsFreqSpin->setEnabled(connected);
    if (ddsStartStopBtn) ddsStartStopBtn->setEnabled(connected);
    if (ddsLoadArbBtn) ddsLoadArbBtn->setEnabled(connected);

    // Digital controls
    for (int i = 0; i < 4; ++i) {
        if (digitalOutButtons[i]) digitalOutButtons[i]->setEnabled(connected);
    }
    if (readDigitalBtn) readDigitalBtn->setEnabled(connected);
    if (digFreqSpin) digFreqSpin->setEnabled(connected);
    if (digFreqStartBtn) digFreqStartBtn->setEnabled(connected);

    // Update status
    QString status = connected ? (running ? "Running" : "Connected") : "Disconnected";
    if (statusLabel) statusLabel->setText(status);
}

void MainWindow::showStatus(const QString &msg)
{
    statusLabel->setText(msg);
    statusBar()->showMessage(msg, 3000);
}

// --- SLOT IMPLEMENTATIONS ---

void MainWindow::onConnectButtonClicked()
{
    if (isConnected) {
        serialHandler->closePort();
    } else {
        QString portName = serialPortCombo->currentText();
        if (portName.isEmpty()) {
            QMessageBox::warning(this, "Connection Error", "No serial port selected.");
            return;
        }
        serialHandler->openPort(portName);
    }
}

void MainWindow::handleSerialConnectionStatus(bool connected)
{
    isConnected = connected;
    updateUiState();
    showStatus(connected ? "Connected" : "Disconnected");
    if(connected) {
        onStudentNameChanged(); // Send name on connect
    } else {
        lastConnectedPort.clear(); // Reset lastConnectedPort on disconnect
    }
}

void MainWindow::handleSerialPortError(const QString &error)
{
    QMessageBox::critical(this, "Serial Error", error);
    isConnected = false;
    updateUiState();
    showStatus("Serial Port Error");
}

void MainWindow::handleSerialData(const QByteArray &data)
{
    // Simple protocol handler: 'D' for data, 'S' for signature, 'I' for digital in
    if (data.isEmpty()) return;

    if (data.startsWith('D')) {
        processOscilloscopeData(data.mid(1));
    } else if (data.startsWith('S')) {
        deviceSignature = QString::fromLatin1(data.mid(1));
        signatureEdit->setText(deviceSignature);
    } else if (data.startsWith('I')) {
        if(data.length() > 1) {
            quint8 in_byte = data[1];
            for(int i=0; i<4; ++i) {
                if(digitalInLabels[i]) {
                    digitalInLabels[i]->setText((in_byte >> i) & 1 ? "H" : "L");
                }
            }
        }
    } else if (data.startsWith("Done")) {
        // Capture is done, now we can request data
        QByteArray readCmd;
        readCmd.append('D');
        int readMode = 1; // Default CH1+CH2
        if (ch1Radio->isChecked() || fftCh1Radio->isChecked()) readMode = 2; // CH1 only
        if (ch2Radio->isChecked() || fftCh2Radio->isChecked()) readMode = 3; // CH2 only
        readCmd.append((char)readMode);
        readCmd.append((char)0); // 3rd byte for protocol compatibility
        serialHandler->sendCommand(readCmd);
    }
}

void MainWindow::updateSerialPortList()
{
    serialPortCombo->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        serialPortCombo->addItem(port.portName());
    }
}

void MainWindow::onRunClicked()
{
    if (!isConnected) return;
    isRunning = true;
    updateUiState();
    // --- PATCH: Restore working dataLength logic ---
    if (acquisitionMode == 0) {
        dataLength = 200;
    } else {
        dataLength = 400;
    }
    // --- END PATCH ---
    qDebug() << "[MainWindow] Starting oscilloscope mode";
    qDebug() << "[MainWindow] Radio button states:";
    qDebug() << "  overwriteRadio checked:" << (overwriteRadio ? overwriteRadio->isChecked() : false);
    qDebug() << "  addRadio checked:" << (addRadio ? addRadio->isChecked() : false);
    qDebug() << "  continuousRadio checked:" << (continuousRadio ? continuousRadio->isChecked() : false);
    testTriggerFunctionality();
    int mode = (acquisitionMode == 0) ? 1 : (acquisitionMode + 1);
    int len = dataLength;
    bool dualChannel = (acquisitionMode == 0);
    qDebug() << "[MainWindow] onRunClicked: acquisitionMode=" << acquisitionMode
             << "-> serialMode=" << mode << "dataLength=" << len << "dualChannel=" << dualChannel;
    serialHandler->setProtocolParams(
        ch1Offset,
        ch2Offset,
        trigLevel,
        trigSource,
        trigPolarity,
        sampleRateCombo ? sampleRateCombo->currentIndex() : 3
    );
    // Always configure trigger and start acquisition for all modes
    setTriggerMode();
    QThread::msleep(100);
    connect(serialHandler, &SerialHandler::oscilloscopeRawDataReady, this, &MainWindow::onOscilloscopeRawDataReady, Qt::UniqueConnection);
    serialHandler->startOscilloscopeAcquisition(mode, len, dualChannel);
    if (continuousRadio && continuousRadio->isChecked()) {
        plotTimer->start(33);
    }
}

void MainWindow::onOscilloscopeRawDataReady(const QByteArray &ch1, const QByteArray &ch2, int dataLength, bool dualChannel)
{
    qDebug() << "[MainWindow] Received oscilloscope data: CH1=" << ch1.size() << "bytes, CH2=" << ch2.size() << "bytes";
    qDebug() << "[DEBUG] isRunning=" << isRunning << ", isConnected=" << isConnected;
    // Only proceed if we have all required data for the current mode
    if (dualChannel) {
        if (ch1.isEmpty() || ch2.isEmpty()) {
            qDebug() << "[MainWindow] Waiting for both channels to be ready before plotting.";
            return;
        }
    } else {
        // For single channel modes, check which channel data we expect
        // acquisitionMode: 1=CH1, 2=CH2 (from onModeChanged)
        if (acquisitionMode == 1 && ch1.isEmpty()) {
            qDebug() << "[MainWindow] Waiting for CH1 data.";
            return;
        }
        if (acquisitionMode == 2 && ch2.isEmpty()) {
            qDebug() << "[MainWindow] Waiting for CH2 data.";
            return;
        }

        // For single channel modes, ensure we don't have data from the wrong channel
        if (acquisitionMode == 1 && !ch2.isEmpty()) {
            qDebug() << "[MainWindow] CH1 mode: ignoring CH2 data.";
            // Don't return, just ignore CH2 data
        }
        if (acquisitionMode == 2 && !ch1.isEmpty()) {
            qDebug() << "[MainWindow] CH2 mode: ignoring CH1 data.";
            // Don't return, just ignore CH1 data
        }
    }
    // Convert raw bytes to voltage values
    QVector<double> ch1Volts, ch2Volts;
    QVector<double> timeValues;
    // Determine the number of points to plot (for x-axis)
    int N = 0;
    if (!ch1.isEmpty() && (acquisitionMode == 1 || dftMode || (acquisitionMode == 0 && !ch2.isEmpty()))) {
        N = ch1.size();
    } else if (!ch2.isEmpty() && acquisitionMode == 2) {
        N = ch2.size();
    } else if (!ch1.isEmpty() && !ch2.isEmpty()) {
        N = std::min(ch1.size(), ch2.size());
    }
    timeValues.resize(N);
    for (int i = 0; i < N; ++i) {
        timeValues[i] = i * multiplier;
    }
    // --- ADC to Voltage Conversion (Legacy/Calibrated Equations) ---
    /*
     * For CH1 (Channel 1):
     *   read_temp = (((ADC_CH1 * 10.0 / 128.0) - 10.0 + OC1) * scaleFactor / ch1Gain) + OC1 + 3.78V + UI_offset
     * For CH2 (Channel 2):
     *   read_temp = (((ADC_CH2 * 10.0 / 128.0) - 10.0) * scaleFactor / ch2Gain) + 3.78V + UI_offset
     * Where:
     *   ADC_CH1, ADC_CH2: Raw ADC value (0–255)
     *   OC1: Offset correction for CH1 (set to 0.0 if not used)
     *   scaleFactor: Calibration factor (5.0 / 4.8)
     *   ch1Gain, ch2Gain: Gain for each channel
     *   3.78V: Fixed baseline offset
     *   UI_offset: User-adjustable offset from slider (ch1Offset/ch2Offset converted to voltage)
     */
    const double scaleFactor = 5.0 / 4.8;
    const double OC1 = 0.0; // Set to nonzero if you want to apply offset correction
    const double fixedOffset = 4.00; // Fixed baseline offset
    double ch1UIOffset = (ch1Offset / 100.0) / 2.0; // Reduce offset effect by half
    double ch2UIOffset = (ch2Offset / 100.0) / 2.0; // Reduce offset effect by half
    // Convert CH1 data
    if (!ch1.isEmpty()) {
        ch1Volts.resize(ch1.size());
        qDebug() << "[MainWindow] Converting CH1 data with gain:" << ch1Gain;
        for (int i = 0; i < ch1.size(); ++i) {
            double adcValue = static_cast<unsigned char>(ch1[i]);
            double voltage = (((adcValue * 10.0 / 128.0) - 10.0 + OC1) * scaleFactor / ch1Gain) + OC1 + fixedOffset + ch1UIOffset;
            voltage -= 3.78; // Apply fixed -8.89V offset
            ch1Volts[i] = voltage;
            if (i < 5) {
                qDebug() << "[MainWindow] CH1[" << i << "]: ADC=" << adcValue << "->" << voltage << "V (gain=" << ch1Gain << ", UI_offset=" << ch1UIOffset << ")";
            }
        }
    }
    // Convert CH2 data
    if (!ch2.isEmpty()) {
        ch2Volts.resize(ch2.size());
        qDebug() << "[MainWindow] Converting CH2 data with gain:" << ch2Gain;
        for (int i = 0; i < ch2.size(); ++i) {
            double adcValue = static_cast<unsigned char>(ch2[i]);
            double voltage = (((adcValue * 10.0 / 128.0) - 10.0) * scaleFactor / ch2Gain) + fixedOffset + ch2UIOffset;
            voltage -= 3.78; // Apply fixed -8.89V offset
            ch2Volts[i] = voltage;
            if (i < 5) {
                qDebug() << "[MainWindow] CH2[" << i << "]: ADC=" << adcValue << "->" << voltage << "V (gain=" << ch2Gain << ", UI_offset=" << ch2UIOffset << ")";
            }
        }
    }

    // Apply averaging for 2Mbps rate (like VB.NET)
    if (sampleRateCombo && sampleRateCombo->currentIndex() == 0) { // 2Mbps is index 0
        qDebug() << "[MainWindow] 2Mbps detected, applying averaging";

        // Store original data for averaging
        QVector<double> originalCh1 = ch1Volts;
        QVector<double> originalCh2 = ch2Volts;

        // Apply averaging to the voltage data
        if (!ch1Volts.isEmpty()) {
            QVector<double> tempCh1 = ch1Volts;
            ch1Volts.resize(dataLength);

            // First data point stays the same
            ch1Volts[0] = tempCh1[0];

            // Average every 2 points and interpolate
            for (int i = 1; i < dataLength; i += 2) {
                if (i/2 + 1 < tempCh1.size()) {
                    ch1Volts[i] = (tempCh1[i/2] + tempCh1[i/2 + 1]) / 2.0;
                } else {
                    ch1Volts[i] = tempCh1[i/2];
                }

                if (i + 1 < dataLength && i/2 + 1 < tempCh1.size()) {
                    ch1Volts[i + 1] = tempCh1[i/2 + 1];
                }
            }
        }

        if (!ch2Volts.isEmpty()) {
            QVector<double> tempCh2 = ch2Volts;
            ch2Volts.resize(dataLength);

            // First data point stays the same
            ch2Volts[0] = tempCh2[0];

            // Average every 2 points and interpolate
            for (int i = 1; i < dataLength; i += 2) {
                if (i/2 + 1 < tempCh2.size()) {
                    ch2Volts[i] = (tempCh2[i/2] + tempCh2[i/2 + 1]) / 2.0;
                } else {
                    ch2Volts[i] = tempCh2[i/2];
                }

                if (i + 1 < dataLength && i/2 + 1 < tempCh2.size()) {
                    ch2Volts[i + 1] = tempCh2[i/2 + 1];
                }
            }
        }

        qDebug() << "[MainWindow] Averaging applied for 2Mbps rate";
    }

    // If we're in sweep mode, collect amplitude data for Bode plot
    if (sweepRunning && sweepIndex < sweepFrequencies.size()) {
        qDebug() << "[MainWindow] Processing sweep data for frequency" << sweepFrequencies[sweepIndex] << "Hz";

        // Calculate RMS amplitude of the signal (use CH1 as input, CH2 as output for transfer function)
        double inputAmplitude = 0.0;
        double outputAmplitude = 0.0;

        // Calculate input amplitude (CH1 - DDS signal)
        if (!ch1Volts.isEmpty()) {
            double sumSquares = 0.0;
            for (double voltage : ch1Volts) {
                sumSquares += voltage * voltage;
            }
            inputAmplitude = sqrt(sumSquares / ch1Volts.size());
        }

        // Calculate output amplitude (CH2 - system response)
        if (!ch2Volts.isEmpty()) {
            double sumSquares = 0.0;
            for (double voltage : ch2Volts) {
                sumSquares += voltage * voltage;
            }
            outputAmplitude = sqrt(sumSquares / ch2Volts.size());
        }

        // Use output amplitude for Bode plot (or input if output is not available)
        double amplitude = outputAmplitude > 0 ? outputAmplitude : inputAmplitude;

        sweepAmplitudes.append(amplitude);
        // Store input/output waveforms for Bode plot
        sweepInputWaves.append(ch1Volts);
        sweepOutputWaves.append(ch2Volts);
        qDebug() << "[MainWindow] Sweep frequency" << sweepFrequencies[sweepIndex] << "Hz:";
        qDebug() << "  Input amplitude (CH1):" << inputAmplitude << "V RMS";
        qDebug() << "  Output amplitude (CH2):" << outputAmplitude << "V RMS";
        qDebug() << "  Using amplitude:" << amplitude << "V RMS";
        qDebug() << "[MainWindow] Sweep progress:" << sweepIndex + 1 << "/" << sweepFrequencies.size() << "(" << ((sweepIndex + 1) * 100 / sweepFrequencies.size()) << "% )";
        // Move to next frequency after a short delay
        sweepIndex++;
        // If this was the last frequency, stop sweep and return
        if (sweepIndex >= sweepFrequencies.size()) {
            stopSweep();
            return;
        }
        // Stop continuous oscilloscope mode during sweep to prevent infinite loop
        if (isRunning) {
            qDebug() << "[MainWindow] Stopping oscilloscope for sweep progression";
            onStopClicked();
        }
        // Continue to next frequency
        QTimer::singleShot(sweepDelay, this, &MainWindow::setDDSForSweep);
    }

    // --- UNIVERSAL TRIGGER LOGIC (ALL MODES) ---
    double gain = 1.0;
    if (ch1TrigRadio && ch1TrigRadio->isChecked()) gain = ch1Gain;
    else if (ch2TrigRadio && ch2TrigRadio->isChecked()) gain = ch2Gain;
    double trigLine = (trigLevel * 10.0 / 2048.0 - 10.0) / gain;
    trigLine = std::round(trigLine * 100.0) / 100.0;
    double waveformMin = 0, waveformMax = 0;
    const QVector<double>* signalData = nullptr;
    if (ch1TrigRadio && ch1TrigRadio->isChecked() && !ch1Volts.isEmpty()) signalData = &ch1Volts;
    else if (ch2TrigRadio && ch2TrigRadio->isChecked() && !ch2Volts.isEmpty()) signalData = &ch2Volts;
    if (signalData) {
        waveformMin = *std::min_element(signalData->begin(), signalData->end());
        waveformMax = *std::max_element(signalData->begin(), signalData->end());
        if (trigLine > waveformMax || trigLine < waveformMin) {
            qDebug() << "[DEBUG] Trigger level outside signal range: trigLine=" << trigLine << ", min=" << waveformMin << ", max=" << waveformMax;
            QMessageBox::warning(this, "Error", "Trigger level is outside signal range. Turning OFF Trigger.");
            if (autoTrigRadio) autoTrigRadio->setChecked(true);
            return;
        }
    }
    // Draw trigger line
    if (plotManager) {
        bool triggerOnCh2 = (ch2TrigRadio && ch2TrigRadio->isChecked());
        plotManager->updateTriggerLevel(trigLine, triggerOnCh2);
    }
    // --- TRIGGER CONDITION ---
    bool triggered = checkTriggerCondition(ch1Volts, ch2Volts);
    qDebug() << "[DEBUG] Trigger condition result:" << triggered;
    if (triggered) {
        // Always overwrite the buffers with the latest data
        ch1Buffer = ch1Volts;
        ch2Buffer = ch2Volts;
        timeBuffer = timeValues;
        qDebug() << "[DEBUG] Updating plot with new data (triggered).";
        // Directly update the plot regardless of isRunning
        if (plotManager) {
            plotManager->updateWaveform(ch1Buffer, ch2Buffer);
            qDebug() << "[DEBUG] Plot updated directly in onOscilloscopeRawDataReady.";
        }
        // Start next acquisition if still running and connected
        if (isRunning && isConnected) {
            qDebug() << "[DEBUG] Re-arming acquisition (isRunning && isConnected).";
            int serialMode = (acquisitionMode == 0) ? 1 : (acquisitionMode + 1);
            bool serialDualChannel = (acquisitionMode == 0);
            serialHandler->startOscilloscopeAcquisition(serialMode, dataLength, serialDualChannel);
        } else {
            qDebug() << "[DEBUG] Not re-arming acquisition (isRunning=" << isRunning << ", isConnected=" << isConnected << ")";
        }
    } else {
        qDebug() << "[DEBUG] Not updating plot (not triggered).";
    }
    // Always start the next acquisition, even if not triggered
    if (isRunning && isConnected) {
        qDebug() << "[DEBUG] Re-arming acquisition (isRunning && isConnected).";
        serialHandler->resetAcquisitionState();
        int serialMode = (acquisitionMode == 0) ? 1 : (acquisitionMode + 1);
        bool serialDualChannel = (acquisitionMode == 0);
        serialHandler->startOscilloscopeAcquisition(serialMode, dataLength, serialDualChannel);
    } else {
        qDebug() << "[DEBUG] Not re-arming acquisition (isRunning=" << isRunning << ", isConnected=" << isConnected << ")";
    }
}

void MainWindow::onStopClicked()
{
    qDebug() << "[DEBUG] onStopClicked() called. Setting isRunning = false.";
    isRunning = false;
    dataRequestTimer->stop();
    updateUiState();
    plotTimer->stop();
    blinkTestLED();
    if (isCollectingTraces) {
        qDebug() << "[MainWindow] Stopping trace collection";
        resetTraceCollection();
        runCount = 0;
        targetTraceCount = 2;
        qDebug() << "[MainWindow] Reset run count to 0, target trace count to 2";
    }
    // Only clear buffers if not auto-cycling
    if (!autoCyclingActive) {
        if (!ch1Buffer.isEmpty() || !ch2Buffer.isEmpty()) {
            plotManager->updateWaveform(ch1Buffer, ch2Buffer);
            qDebug() << "[DEBUG] Plot updated after stop.";
        }
        ch1Buffer.clear();
        ch2Buffer.clear();
        timeBuffer.clear();
    } else {
        qDebug() << "[DEBUG] Skipping buffer clear due to auto-cycling.";
    }
    // Reset autoCyclingActive if user manually stops
    if (!isRunning) {
        autoCyclingActive = false;
    }
}

void MainWindow::onAbortClicked()
{
    serialHandler->sendCommand(QByteArray(1, 'A'));
    onStopClicked();
    showStatus("Abort sent");
    blinkTestLED();
}

void MainWindow::onExportCSV()
{
    if (!waveformExporter) {
        qDebug() << "[MainWindow] WaveformExporter not initialized";
        return;
    }

    if (ch1Buffer.isEmpty() && ch2Buffer.isEmpty()) {
        qDebug() << "[MainWindow] No data to export";
        return;
    }

    qDebug() << "[MainWindow] Exporting CSV - CH1 points:" << ch1Buffer.size()
             << "CH2 points:" << ch2Buffer.size()
             << "Time points:" << timeBuffer.size()
             << "CH1 FFT points:" << ch1FFT.size()
             << "CH2 FFT points:" << ch2FFT.size()
             << "Freq points:" << freqBuffer.size();

    // Export the data using the waveformExporter
    // Include FFT data if available
    waveformExporter->exportToCSV(ch1Buffer, ch2Buffer, timeBuffer, ch1FFT, ch2FFT, freqBuffer);
}

void MainWindow::processOscilloscopeData(const QByteArray &data)
{
    // LEGACY METHOD - DISABLED
    // This method used the old VB.NET-style voltage conversion formula with -10V offset
    // Now replaced by onOscilloscopeRawDataReady which uses correct voltage conversion
    qDebug() << "[MainWindow] processOscilloscopeData called but disabled - using onOscilloscopeRawDataReady instead";
    return;

    /*
    lastDataReceived.restart();
    int len = dataLength;
    bool dualChannel = (bothChRadio && bothChRadio->isChecked()) || (xyRadio && xyRadio->isChecked());
    int requiredSize = dualChannel ? 2 * len : len;
    if (data.size() < requiredSize) return;

    // VB.NET gain values: 0.5, 1, 2, 4, 8, 16
    const double gainValues[] = {0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
    double ch1GainValue = (ch1GainCombo && ch1GainCombo->currentIndex() < 6) ? gainValues[ch1GainCombo->currentIndex()] : 1.0;
    double ch2GainValue = (ch2GainCombo && ch2GainCombo->currentIndex() < 6) ? gainValues[ch2GainCombo->currentIndex()] : 1.0;

    // Correct voltage conversion: map 0-255 to -20V to +20V for 0.5x gain
    // Skip first 10 points and start from index 10
    int startIndex = 10;
    int actualLen = len - startIndex;
    int len2 = dualChannel ? len : 0; // Calculate len2 for dual channel

    ch1Buffer.resize(actualLen);
    ch2Buffer.resize(actualLen);
    timeBuffer.resize(actualLen);

    for (int i = 0; i < actualLen; ++i) {
        int dataIndex = i + startIndex; // Skip first 10 points
        double raw1 = static_cast<quint8>(data[dataIndex]);
        // Convert ADC value (0-255) to voltage using VB.NET formula: raw * 10 / 128 - 10
        // This gives ±10V range, then scale to ±8V by multiplying by 0.8
        double val1 = (raw1 * 10.0 / 128.0 - 10.0) * 0.8; // Scale to ±8V range
        ch1Buffer[i] = std::round(val1 * 100.0) / 100.0; // Round to 2 decimal places

        // Debug first 5 conversions to verify voltage calculation
        if (i < 5) {
            qDebug() << "[MainWindow] Sample" << i << "CH1: raw=" << raw1 << "voltage=" << val1 << "VB.NET-style=" << (raw1 * 10.0 / 128.0 - 10.0);
        }

        if (dualChannel && dataIndex < len2) {
            double raw2 = static_cast<quint8>(data[dataIndex + len]);
            // Convert ADC value (0-255) to voltage using VB.NET formula: raw * 10 / 128 - 10
            // This gives ±10V range, then scale to ±8V by multiplying by 0.8
            double val2 = (raw2 * 10.0 / 128.0 - 10.0) * 0.8; // Scale to ±8V range
            ch2Buffer[i] = std::round(val2 * 100.0) / 100.0; // Round to 2 decimal places
        } else {
            ch2Buffer[i] = 0.0;
        }
        timeBuffer[i] = i * multiplier;
    }
    // Run mode logic handled in onOscilloscopeRawDataReady
    */
}

void MainWindow::updatePlot()
{
    qDebug() << "[DEBUG] updatePlot() called. isRunning=" << isRunning << ", isConnected=" << isConnected << ", ch1Buffer size=" << ch1Buffer.size() << ", ch2Buffer size=" << ch2Buffer.size();
    if (isRunning && isConnected && (!ch1Buffer.isEmpty() || !ch2Buffer.isEmpty())) {
        if (continuousRadio && continuousRadio->isChecked()) {
            plotManager->updateWaveform(ch1Buffer, ch2Buffer);
            qDebug() << "[DEBUG] Plot updated (continuous mode).";
        } else {
            plotManager->updateWaveform(ch1Buffer, ch2Buffer);
            qDebug() << "[DEBUG] Plot updated (triggered mode).";
        }
    } else {
        qDebug() << "[DEBUG] Plot not updated (not running/connected or empty buffer).";
    }
}

void MainWindow::onModeChanged(int index)
{
    qDebug() << "[MainWindow] onModeChanged: Switching from mode" << currentMode << "to" << index;

    // index: 0=Both, 1=CH1, 2=CH2, 3=XY, 4=DFT CH1, 5=DFT CH2, 6=DFT Both
    dftMode = false;
    dftChannel = 0;
    switch (index) {
        case 0: // Both Channels
        case 3: // XY
            dataLength = 200;
            acquisitionMode = 0;
            break;
        case 1: // CH1
            dataLength = 400;
            acquisitionMode = 1;
            break;
        case 2: // CH2
            dataLength = 400;
            acquisitionMode = 2;
            break;
        case 4: // DFT CH1
            dataLength = 400;
            acquisitionMode = 1;
            dftMode = true;
            dftChannel = 1;
            break;
        case 5: // DFT CH2
            dataLength = 400;
            acquisitionMode = 2;
            dftMode = true;
            dftChannel = 2;
            break;
        case 6: // DFT Both CH1 & CH2
            dataLength = 400;
            acquisitionMode = 0;
            dftMode = true;
            dftChannel = 3; // NEW: 3 means both
            break;
    }
    currentMode = index; // Store the current mode
    qDebug() << "[MainWindow] Mode changed to:" << index << "dataLength:" << dataLength << "acquisitionMode:" << acquisitionMode << "dftMode:" << dftMode << "dftChannel:" << dftChannel;

    // Update PlotManager with new mode
    if (plotManager) {
        plotManager->setDisplayMode(index);
        plotManager->setDataLength(dataLength);
        plotManager->setMultiplier(multiplier);
        plotManager->setMaxFrequency(maxFrequency);
    }

    // Send mode command to device if running
    if (isRunning && isConnected) {
        QByteArray cmd;
        cmd.append('F');
        cmd.append((char)acquisitionMode);
        serialHandler->sendCommand(cmd);
        qDebug() << "[MainWindow] Applied mode change instantly";
    }
    // Update plot with new data length
    timeBuffer.resize(dataLength);
    for (int i = 0; i < dataLength; ++i) {
        timeBuffer[i] = i * multiplier;
    }
    // Clear buffers to avoid plotting mismatched data
    ch1Buffer.clear();
    ch2Buffer.clear();
    timeBuffer.clear();

    // Request new data from the device if running and connected
    if (isRunning && isConnected) {
        // Convert acquisitionMode to SerialHandler mode format
        // acquisitionMode: 0=both, 1=CH1, 2=CH2
        // SerialHandler expects: 1=both, 2=CH1, 3=CH2
        int serialMode = (acquisitionMode == 0) ? 1 : (acquisitionMode + 1);
        bool dualChannel = (acquisitionMode == 0);

        qDebug() << "[MainWindow] Mode change: requesting new acquisition with mode" << serialMode
                 << "dataLength" << dataLength << "dualChannel" << dualChannel;

        // Use a small delay to ensure the mode command is processed before starting acquisition
        QTimer::singleShot(50, this, [this, serialMode, dualChannel]() {
            if (isRunning && isConnected) {
                serialHandler->startOscilloscopeAcquisition(serialMode, dataLength, dualChannel);
            }
        });
    }
    // Do not call plotScope() here; wait for new data to arrive
}

void MainWindow::onSampleRateChanged(int index)
{
    // Convert UI index (0-13) to VB.NET style Sample_Rate_Selection (1-14)
    int sampleRateSelection = index + 1;

    // Use VB.NET style multipliers and parameters
    double multiplier = 0.5;
    double maxFrequency = 1000000;
    QString heading1 = "Time(uSec)";

    switch (sampleRateSelection) {
        case 1: // 2Mbps  0.50us/sample
            multiplier = 0.5;
            maxFrequency = 1000000;
            heading1 = "Time(uSec)";
            break;
        case 2: // 1Mbps   1.0us/sample
            multiplier = 1.0;
            maxFrequency = 500000;
            heading1 = "Time(uSec)";
            break;
        case 3: // 500kbps 2.0us/sample
            multiplier = 2.0;
            maxFrequency = 250000;
            heading1 = "Time(uSec)";
            break;
        case 4: // 200kbps 5.0us/sample
            multiplier = 5.0;
            maxFrequency = 100000;
            heading1 = "Time(uSec)";
            break;
        case 5: // 100kbps  10us/sample
            multiplier = 10.0;
            maxFrequency = 50000;
            heading1 = "Time(uSec)";
            break;
        case 6: // 50kbps   20us/sample
            multiplier = 20.0;
            maxFrequency = 25000;
            heading1 = "Time(uSec)";
            break;
        case 7: // 20kbps   50us/sample
            multiplier = 50.0;
            maxFrequency = 10000;
            heading1 = "Time(uSec)";
            break;
        case 8: // 10kbps  100us/sample
            multiplier = 100.0;
            maxFrequency = 5000;
            heading1 = "Time(uSec)";
            break;
        case 9: // 5kbps   200us/sample
            multiplier = 200.0;
            maxFrequency = 2500;
            heading1 = "Time(uSec)";
            break;
        case 10: // 2kbps   500us/sample
            multiplier = 500.0;
            maxFrequency = 1000;
            heading1 = "Time(uSec)";
            break;
        case 11: // 1kbps   1.0ms/sample
            multiplier = 1000.0;
            maxFrequency = 500;
            heading1 = "Time(mSec)";
            break;
        case 12: // 500Hz   2.0ms/sample
            multiplier = 2000.0;
            maxFrequency = 250;
            heading1 = "Time(mSec)";
            break;
        case 13: // 200Hz   5.0ms/sample
            multiplier = 5000.0;
            maxFrequency = 100;
            heading1 = "Time(mSec)";
            break;
        case 14: // 100Hz    10ms/sample
            multiplier = 10000.0;
            maxFrequency = 50;
            heading1 = "Time(mSec)";
            break;
        default:
            multiplier = 5.0;
            maxFrequency = 100000;
            heading1 = "Time(uSec)";
            break;
    }

    this->multiplier = multiplier;
    this->maxFrequency = maxFrequency;
    this->heading1 = heading1;

    // Update plot manager's multiplier for x-axis scaling
    plotManager->setMultiplier(multiplier);
    // Update plot manager's X-axis title with correct time unit
    plotManager->setXAxisTitle(heading1);
    // Regenerate timeBuffer with new multiplier
    timeBuffer.resize(dataLength);
    for (int i = 0; i < dataLength; ++i) {
        timeBuffer[i] = i * multiplier;
    }
    // Refresh the plot with new x-axis scaling
    if (!ch1Buffer.isEmpty() || !ch2Buffer.isEmpty()) {
        plotManager->updateWaveform(ch1Buffer, ch2Buffer);
    }

    qDebug() << "[MainWindow] Sample rate changed to index:" << index << "selection:" << sampleRateSelection << "multiplier:" << multiplier << "device index:" << sampleRateSelection;

    // Update SerialHandler protocol parameters with new sample rate
    serialHandler->setProtocolParams(
        ch1Offset,
        ch2Offset,
        trigLevel,
        trigSource,
        trigPolarity,
        sampleRateSelection  // Use 1-based indexing like VB.NET
    );

    // Apply sample rate change instantly if oscilloscope is running
    if (isRunning && isConnected) {
        // Use 1-based indexing like VB.NET
        int deviceIndex = sampleRateSelection;

        QByteArray cmd(3, 0);
        cmd[0] = 0x53; // 'S'
        cmd[1] = static_cast<char>(deviceIndex);
        cmd[2] = 0x00;
        serialHandler->sendCommand(cmd);
        qDebug() << "[MainWindow] Applied sample rate change instantly - UI index:" << index << "device index:" << deviceIndex;
    }
}

void MainWindow::requestOscilloscopeData()
{
    // Setup and then capture
    // Add delays between commands for reliability (VB.NET uses 20ms)
    sendGainCommand();
    QThread::msleep(20);
    sendOffsetCommand();
    QThread::msleep(20);
    sendTriggerCommand();
    QThread::msleep(20);
    sendModeCommand();
    QThread::msleep(20);
    sendSampleRateCommand();
    QThread::msleep(20);
    // Send 3-byte capture command (VB.NET sends 3 bytes)
    QByteArray captureCmd;
    captureCmd.append('C');
    captureCmd.append((char)0);
    captureCmd.append((char)0);
    serialHandler->sendCommand(captureCmd);
}

// ... Implementations for all other slots ...
// onCh1GainChanged, onCh1OffsetChanged, onTrigLevelChanged, etc.

void MainWindow::onCh1GainChanged(int idx) {
    ch1Gain = ch1GainCombo->itemData(idx).toDouble();
    qDebug() << "[MainWindow] CH1 Gain changed to:" << ch1Gain;

    // Update PlotManager with new gain values
    plotManager->setGains(ch1Gain, ch2Gain);

    // Apply gain change instantly if oscilloscope is running
    if (isRunning && isConnected) {
        serialHandler->setProtocolParams(
            ch1Offset,
            ch2Offset,
            trigLevel,
            trigSource,
            trigPolarity,
            sampleRateCombo ? sampleRateCombo->currentIndex() : 0
        );
        qDebug() << "[MainWindow] Applied CH1 gain change instantly";
    }
    // Only update plot if there's data to plot
    if (!ch1Buffer.isEmpty() || !ch2Buffer.isEmpty()) {
        plotManager->updateWaveform(ch1Buffer, ch2Buffer);
    }
}

void MainWindow::onCh2GainChanged(int idx) {
    ch2Gain = ch2GainCombo->itemData(idx).toDouble();
    qDebug() << "[MainWindow] CH2 Gain changed to:" << ch2Gain;

    // Update PlotManager with new gain values
    plotManager->setGains(ch1Gain, ch2Gain);

    // Apply gain change instantly if oscilloscope is running
    if (isRunning && isConnected) {
        serialHandler->setProtocolParams(
            ch1Offset,
            ch2Offset,
            trigLevel,
            trigSource,
            trigPolarity,
            sampleRateCombo ? sampleRateCombo->currentIndex() : 0
        );
        qDebug() << "[MainWindow] Applied CH2 gain change instantly";
    }
    // Only update plot if there's data to plot
    if (!ch1Buffer.isEmpty() || !ch2Buffer.isEmpty()) {
        plotManager->updateWaveform(ch1Buffer, ch2Buffer);
    }
}

void MainWindow::onCh1OffsetChanged(int value) {
    ch1Offset = value;

    // Convert slider value to voltage and display in edit field
    double voltage = value / 100.0; // Convert from -1694 to +1695 range to -16.94 to +16.95V
    ch1OffsetEdit->setText(QString("%1V").arg(voltage, 0, 'f', 2));

    qDebug() << "[MainWindow] CH1 Offset changed to:" << ch1Offset << "(" << voltage << "V)";

    // Apply offset change instantly if oscilloscope is running
    if (isRunning && isConnected) {
        serialHandler->setProtocolParams(
            ch1Offset,
            ch2Offset,
            trigLevel,
            trigSource,
            trigPolarity,
            sampleRateCombo ? sampleRateCombo->currentIndex() : 0
        );
        qDebug() << "[MainWindow] Applied CH1 offset change instantly";
    }
}

void MainWindow::onCh2OffsetChanged(int value) {
    ch2Offset = value;

    // Convert slider value to voltage and display in edit field
    double voltage = value / 100.0; // Convert from -1694 to +1695 range to -16.94 to +16.95V
    ch2OffsetEdit->setText(QString("%1V").arg(voltage, 0, 'f', 2));

    qDebug() << "[MainWindow] CH2 Offset changed to:" << ch2Offset << "(" << voltage << "V)";

    // Apply offset change instantly if oscilloscope is running
    if (isRunning && isConnected) {
        serialHandler->setProtocolParams(
            ch1Offset,
            ch2Offset,
            trigLevel,
            trigSource,
            trigPolarity,
            sampleRateCombo ? sampleRateCombo->currentIndex() : 3
        );
        qDebug() << "[MainWindow] Applied CH2 offset change instantly";
    }
}

void MainWindow::onTrigLevelChanged(int value) {
    trigLevel = value;
    if (isRunning) {
        onStopClicked();
        QTimer::singleShot(100, this, [this]() {
            setTriggerMode();
            onRunClicked();
        });
    } else {
        setTriggerMode();
    }
}

void MainWindow::onTrigSourceChanged(int idx) {
    trigSource = idx;
    if (isRunning) {
        onStopClicked();
        QTimer::singleShot(100, this, [this]() {
            setTriggerMode();
            onRunClicked();
        });
    } else {
        setTriggerMode();
    }
}

void MainWindow::onTrigPolarityChanged(int idx) {
    trigPolarity = idx;
    if (isRunning) {
        onStopClicked();
        QTimer::singleShot(100, this, [this]() {
            setTriggerMode();
            onRunClicked();
        });
    } else {
        setTriggerMode();
    }
}

void MainWindow::sendTriggerCommand() {
    QByteArray trigSourceCmd;
    trigSourceCmd.append((char)0x54);
    trigSourceCmd.append((char)trigSource);
    trigSourceCmd.append((char)0x00);
    QByteArray trigPolarityCmd;
    trigPolarityCmd.append((char)0x50);
    trigPolarityCmd.append((char)trigPolarity);
    trigPolarityCmd.append((char)0x00);
    QByteArray trigLevelCmd;
    trigLevelCmd.append((char)0x4C);
    trigLevelCmd.append((char)(trigLevel >> 8));
    trigLevelCmd.append((char)(trigLevel & 0xFF));
   }

// Debug function to test trigger functionality
void MainWindow::testTriggerFunctionality() {
    qDebug() << "[MainWindow] === TRIGGER TEST ===";
    qDebug() << "[MainWindow] Current trigger settings:";
    qDebug() << "  Source:" << trigSource << "(0=Auto, 1=CH1, 2=CH2, 3=Ext)";
    qDebug() << "  Polarity:" << trigPolarity << "(0=L->H, 1=H->L)";
    qDebug() << "  Level:" << trigLevel << "(0-4095)";
    qDebug() << "  CH1 Gain:" << ch1Gain;
    qDebug() << "  CH2 Gain:" << ch2Gain;

    // Calculate trigger voltage
    double triggerVoltage = 0.0;
    double gain = 1.0;

    if (ch1TrigRadio && ch1TrigRadio->isChecked()) {
        gain = ch1Gain;
        qDebug() << "  Trigger on CH1 with gain:" << gain;
    } else if (ch2TrigRadio && ch2TrigRadio->isChecked()) {
        gain = ch2Gain;
        qDebug() << "  Trigger on CH2 with gain:" << gain;
    } else {
        gain = 1.0;
        qDebug() << "  Auto trigger with gain:" << gain;
    }

    triggerVoltage = (trigLevel * 10.0 / 2048.0 - 10.0) / gain;
    qDebug() << "  Calculated trigger voltage:" << triggerVoltage << "V";

    // Use the same radio button detection logic as onRunClicked
    QString runMode = "Unknown";
    if (overwriteRadio && overwriteRadio->isChecked()) {
        runMode = "Overwrite";
    } else if (addRadio && addRadio->isChecked()) {
        runMode = "Add";
    } else if (continuousRadio && continuousRadio->isChecked()) {
        runMode = "Continuous";
    }
    qDebug() << "  Run mode:" << runMode;
    qDebug() << "  Display mode:" << acquisitionMode << "(0=Both, 1=CH1, 2=CH2)";

    // Provide guidance on trigger setup
    if (continuousRadio && continuousRadio->isChecked()) {
        qDebug() << "[MainWindow] *** TRIGGER SETUP GUIDE ***";
        qDebug() << "[MainWindow] You are in Continuous mode. For trigger to work:";
        qDebug() << "[MainWindow] 1. Select 'Overwrite' or 'Add' mode (not Continuous)";
        qDebug() << "[MainWindow] 2. Set trigger level within your signal range";
        qDebug() << "[MainWindow] 3. Ensure signal crosses trigger level in correct direction";
        qDebug() << "[MainWindow] 4. Click Run to start trigger acquisition";
    } else {
        qDebug() << "[MainWindow] Trigger mode detected - trigger should work when signal crosses level";
    }

    qDebug() << "[MainWindow] === END TRIGGER TEST ===";
}

void MainWindow::sendGainCommand() {
    QByteArray cmd;
    cmd.append('G'); cmd.append((char)0); cmd.append((char)ch1GainCombo->currentIndex());
    Q_ASSERT(cmd.size() >= 3);
    serialHandler->sendCommand(cmd);
    cmd[1] = (char)1; cmd[2] = (char)ch2GainCombo->currentIndex();
    Q_ASSERT(cmd.size() >= 3);
    serialHandler->sendCommand(cmd);
}

void MainWindow::sendOffsetCommand() {
    QByteArray cmd;
    cmd.append('O'); cmd.append((char)(ch1Offset >> 8)); cmd.append((char)(ch1Offset & 0xFF));
    Q_ASSERT(cmd.size() >= 3);
    serialHandler->sendCommand(cmd);
    cmd[0] = 'o'; cmd[1] = (char)(ch2Offset >> 8); cmd[2] = (char)(ch2Offset & 0xFF);
    Q_ASSERT(cmd.size() >= 3);
    serialHandler->sendCommand(cmd);
}

void MainWindow::sendModeCommand() { onModeChanged(0); } // Resend based on radio state
void MainWindow::sendSampleRateCommand() {
    int index = sampleRateCombo->currentIndex();

    // Use 1-based indexing like VB.NET
    int deviceIndex = index + 1;

    QByteArray cmd(3, 0);
    cmd[0] = 0x53; // 'S'
    cmd[1] = static_cast<char>(deviceIndex);
    cmd[2] = 0x00;
    serialHandler->sendCommand(cmd);
    qDebug() << "[MainWindow] Sent sample rate command:" << cmd.toHex() << "UI index:" << index << "device index:" << deviceIndex;
}

// --- Other features to be implemented ---
void MainWindow::onDDSStartStopClicked() {
    if (!isConnected) return;
    runDDS();
    showStatus("DDS command sent");
}
void MainWindow::onDDSWaveformChanged(int index) { /* ... */ }
void MainWindow::onDDSFreqChanged(double freq) { /* ... */ }
void MainWindow::onDigitalOutToggled(int bit) {
    digitalOutState |= (1 << bit); // Set bit (turn ON)
    QByteArray cmd;
    cmd.append('h'); cmd.append(digitalOutState);
    serialHandler->sendCommand(cmd);
    // Start a timer to turn OFF the LED after 200ms
    QTimer::singleShot(200, this, [this, bit]() {
        digitalOutState &= ~(1 << bit); // Clear bit (turn OFF)
        QByteArray offCmd;
        offCmd.append('h'); offCmd.append(digitalOutState);
        serialHandler->sendCommand(offCmd);
    });
}
void MainWindow::refreshDigitalInputs() { serialHandler->sendCommand(QByteArray(1, 'i')); }
void MainWindow::onReadDigitalClicked() { refreshDigitalInputs(); }
void MainWindow::onDigFreqStartClicked() { /* ... */ }
void MainWindow::onDigFreqChanged(double freq) { /* ... */ }
void MainWindow::onSweepStartStopClicked() {
    if (!sweepRunning) {
        // Start sweep
        if (!isConnected) {
            qDebug() << "[MainWindow] Cannot start sweep - not connected";
            return;
        }

        // Create frequency array for sweep
        createSweepFrequencyArray();

        if (sweepFrequencies.isEmpty()) {
            qDebug() << "[MainWindow] No frequencies to sweep";
            return;
        }

        sweepRunning = true;
        sweepIndex = 0;

        // Clear previous sweep data
        sweepAmplitudes.clear();
        sweepPhases.clear();
        sweepMagnitudes.clear();
        // Clear input/output waveform storage
        sweepInputWaves.clear();
        sweepOutputWaves.clear();

        // Update UI
        if (sweepStartBtn) sweepStartBtn->setText("Stop Sweep");
        if (sweepProgress) {
            sweepProgress->setVisible(true);
            sweepProgress->setMaximum(sweepFrequencies.size());
            sweepProgress->setValue(0);
        }

        qDebug() << "[MainWindow] Starting sweep with" << sweepFrequencies.size() << "frequencies";

        // Set DDS to sine wave and first frequency
        setDDSForSweep();

    } else {
        // Stop sweep
        stopSweep();
    }
}
void MainWindow::createSweepFrequencyArray() {
    sweepFrequencies.clear();

    // Generate logarithmic sweep frequencies based on start, end, and number of steps
    double startFreq = sweepStartFreq;
    double endFreq = sweepEndFreq;
    int samples = sweepSamples;

    double logStart = log10(startFreq);
    double logEnd = log10(endFreq);
    double step = (logEnd - logStart) / (samples - 1);
    for (int i = 0; i < samples; ++i) {
        double freq = pow(10, logStart + i * step);
        sweepFrequencies.append(freq);
    }

    qDebug() << "[MainWindow] Created sweep array with" << sweepFrequencies.size() << "frequencies";
    qDebug() << "[MainWindow] Start:" << startFreq << "Hz, End:" << endFreq << "Hz, Steps:" << samples;
    for (int i = 0; i < qMin(10, sweepFrequencies.size()); ++i) {
        qDebug() << "  Freq" << i << ":" << sweepFrequencies[i] << "Hz";
    }
}
void MainWindow::setDDSForSweep() {
    if (sweepIndex >= sweepFrequencies.size()) {
        // Sweep complete
        stopSweep();
        return;
    }

    double currentFreq = sweepFrequencies[sweepIndex];

    // Set DDS to sine wave
    if (ddsWaveformCombo) {
        ddsWaveformCombo->setCurrentText("DDS Sin (1-50 kHz)");
    }

    // Set frequency
    if (ddsFreqSpin) {
        ddsFreqSpin->setValue(currentFreq);
    }

    // Set sample rate to lowest > 15x the frequency
    int srIdx = findSampleRateIndex(currentFreq);
    static const double sampleRates[] = {
        2000000, 1000000, 500000, 200000, 100000, 50000, 20000, 10000, 5000, 2000, 1000, 500, 200, 100
    };
    double chosenRate = sampleRates[srIdx];
    qDebug() << "[Bode Sweep] Frequency:" << currentFreq << "Hz, Chosen Sample Rate:" << chosenRate << "Hz (Index:" << srIdx << ")";
    if (sampleRateCombo) {
        sampleRateCombo->setCurrentIndex(srIdx);
        onSampleRateChanged(srIdx);
    }

    // Start DDS
    if (ddsStartStopBtn) {
        ddsStartStopBtn->click();
    }

    qDebug() << "[MainWindow] Sweep step" << sweepIndex + 1 << "/" << sweepFrequencies.size() << "Frequency:" << currentFreq << "Hz, SampleRateIdx:" << srIdx;

    // Update progress
    if (sweepProgress) {
        sweepProgress->setValue(sweepIndex + 1);
    }

    // --- PATCH: Force Continuous mode for sweep (Bode plot) ---
    if (continuousRadio) continuousRadio->setChecked(true);
    if (overwriteRadio) overwriteRadio->setChecked(false);
    if (addRadio) addRadio->setChecked(false);
    // -------------------------------------------------------------

    // Start oscilloscope to capture response (single shot mode with fixed data length)
    if (isConnected) {
        dataLength = 200;
        if (bothChRadio) {
            bothChRadio->setChecked(true);
        }
        // Add settling delay: at least 3 cycles, min 50 ms
        int settleTimeMs = qMax(50, int(3 * 1000.0 / currentFreq));
        QTimer::singleShot(settleTimeMs, this, [this]() {
            onRunClicked();
        });
    }
}
void MainWindow::stopSweep() {
    sweepRunning = false;
    sweepTimer->stop();

    // Update UI
    if (sweepStartBtn) sweepStartBtn->setText("Start Sweep");
    if (sweepProgress) sweepProgress->setVisible(false);

    // Stop DDS
    if (ddsStartStopBtn && ddsStartStopBtn->text().contains("Stop")) {
        ddsStartStopBtn->click();
    }

    qDebug() << "[MainWindow] Sweep stopped. Collected" << sweepAmplitudes.size() << "data points";

    // Plot Bode plot if we have data
    if (!sweepFrequencies.isEmpty() && !sweepAmplitudes.isEmpty()) {
        plotBodePlot();
    }
}
void MainWindow::plotBodePlot() {
    qDebug() << "[MainWindow] Creating Bode plot...";
    if (sweepFrequencies.isEmpty() || sweepInputWaves.isEmpty() || sweepOutputWaves.isEmpty()) {
        qDebug() << "[MainWindow] Missing sweep data for Bode plot";
        return;
    }
    if (sweepFrequencies.size() != sweepInputWaves.size() || sweepFrequencies.size() != sweepOutputWaves.size()) {
        qDebug() << "[MainWindow] Data size mismatch for Bode plot";
        return;
    }
    sweepMagnitudes.clear();
    sweepPhases.clear();
    QVector<double> invalidFreqs, invalidYs; // For marking invalid points
    for (int i = 0; i < sweepFrequencies.size(); ++i) {
        const QVector<double>& inWaveFull = sweepInputWaves[i];
        const QVector<double>& outWaveFull = sweepOutputWaves[i];
        int N = std::min(inWaveFull.size(), outWaveFull.size());
        if (N <= 20) {
            sweepMagnitudes.append(0.0);
            sweepPhases.append(0.0);
            invalidFreqs.append(sweepFrequencies[i]);
            invalidYs.append(0.0); // Mark at 0 dB
            continue;
        }
        // Ignore first 20 points
        QVector<double> inWave = inWaveFull.mid(20);
        QVector<double> outWave = outWaveFull.mid(20);
        int M = std::min(inWave.size(), outWave.size());
        // --- Zero-crossing phase calculation ---
        double dt = multiplier * 1e-6;
        // Find first upward zero-crossing for both input and output
        auto findFirstZeroCrossing = [](const QVector<double>& data) -> int {
            for (int i = 1; i < data.size(); ++i) {
                if (data[i-1] < 0 && data[i] >= 0) {
                    return i;
                }
            }
            return -1;
        };
        int inZeroIdx = findFirstZeroCrossing(inWave);
        int outZeroIdx = findFirstZeroCrossing(outWave);
        // Find period from input zero-crossings
        QVector<int> inZeroIndices;
        for (int j = 1; j < inWave.size(); ++j) {
            if (inWave[j-1] < 0 && inWave[j] >= 0) {
                inZeroIndices.append(j);
            }
        }
        double avgPeriod = 0.0;
        if (inZeroIndices.size() >= 2) {
            double sumPeriods = 0.0;
            for (int k = 1; k < inZeroIndices.size(); ++k) {
                sumPeriods += (inZeroIndices[k] - inZeroIndices[k-1]) * dt;
            }
            avgPeriod = sumPeriods / (inZeroIndices.size() - 1);
        }
        // Amplitude/gain calculation (unchanged)
        double inAvgMax = 0, inAvgMin = 0, outAvgMax = 0, outAvgMin = 0;
        auto findLocalExtrema = [](const QVector<double>& data, double& avgMax, double& avgMin) {
            QVector<double> maxs, mins;
            int N = data.size();
            for (int i = 1; i < N - 1; ++i) {
                if (data[i] > data[i-1] && data[i] > data[i+1]) maxs.append(data[i]);
                if (data[i] < data[i-1] && data[i] < data[i+1]) mins.append(data[i]);
            }
            avgMax = maxs.isEmpty() ? 0.0 : std::accumulate(maxs.begin(), maxs.end(), 0.0) / maxs.size();
            avgMin = mins.isEmpty() ? 0.0 : std::accumulate(mins.begin(), mins.end(), 0.0) / mins.size();
        };
        findLocalExtrema(inWave, inAvgMax, inAvgMin);
        findLocalExtrema(outWave, outAvgMax, outAvgMin);
        double inAmp = (inAvgMax - inAvgMin) / 2.0;
        double outAmp = (outAvgMax - outAvgMin) / 2.0;
        double gain = (inAmp > 1e-9) ? 20.0 * log10(outAmp / inAmp) : 0.0;
        // Phase calculation using zero-crossing
        double phase = 0.0;
        if (inZeroIdx >= 0 && outZeroIdx >= 0 && avgPeriod > 0.0) {
            double deltaT = (outZeroIdx - inZeroIdx) * dt;
            phase = (deltaT / avgPeriod) * 360.0;
            // Wrap to [-180, 180]
            while (phase > 180) phase -= 360;
            while (phase < -180) phase += 360;
        }
        if (inAmp <= 1e-9 || outAmp <= 1e-9) {
            // Mark as invalid
            invalidFreqs.append(sweepFrequencies[i]);
            invalidYs.append(0.0);
        }
        sweepMagnitudes.append(gain);
        sweepPhases.append(phase);
        qDebug() << "[MainWindow] Frequency" << sweepFrequencies[i] << "Hz: gain=" << gain << "dB, phase=" << phase << "degrees (zero-crossing)";
    }
    // --- Smooth Bode plot with multiple layers of moving average ---
    auto movingAverage = [](const QVector<double>& data) {
        QVector<double> result = data;
        int N = data.size();
        if (N < 3) return result;
        for (int i = 1; i < N - 1; ++i) {
            result[i] = (data[i - 1] + data[i] + data[i + 1]) / 3.0;
        }
        return result;
    };
    QVector<double> smoothedMagnitudes = sweepMagnitudes;
    QVector<double> smoothedPhases = sweepPhases;
    // Apply multiple layers (e.g., 3 times)
    for (int filterPass = 0; filterPass < 3; ++filterPass) {
        smoothedMagnitudes = movingAverage(smoothedMagnitudes);
        smoothedPhases = movingAverage(smoothedPhases);
    }
    if (bodePlot) {
        bodePlot->clearGraphs();
        bodePlot->addGraph();
        bodePlot->graph(0)->setData(sweepFrequencies, smoothedMagnitudes);
        bodePlot->graph(0)->setPen(QPen(Qt::blue, 2));
        bodePlot->graph(0)->setName("Magnitude Response");
        bodePlot->graph(0)->setValueAxis(bodePlot->yAxis);
        bodePlot->graph(0)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 4));
        bodePlot->graph(0)->rescaleValueAxis(true); // Auto-scale y-axis for magnitude
        bodePlot->addGraph();
        bodePlot->graph(1)->setData(sweepFrequencies, smoothedPhases);
        bodePlot->graph(1)->setPen(QPen(Qt::red, 2));
        bodePlot->graph(1)->setName("Phase Response");
        bodePlot->graph(1)->setValueAxis(bodePlot->yAxis2);
        bodePlot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssSquare, 4));
        // Add a third graph for invalid/missing points
        if (!invalidFreqs.isEmpty()) {
            bodePlot->addGraph();
            bodePlot->graph(2)->setData(invalidFreqs, invalidYs);
            bodePlot->graph(2)->setPen(QPen(Qt::red, 2, Qt::DashLine));
            bodePlot->graph(2)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCross, Qt::red, 8));
            bodePlot->graph(2)->setLineStyle(QCPGraph::lsNone);
            bodePlot->graph(2)->setName("Invalid/Missing");
        }
        bodePlot->xAxis->setLabel("Frequency (Hz)");
        bodePlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
        bodePlot->xAxis->setRange(sweepFrequencies.first(), sweepFrequencies.last());
        bodePlot->yAxis->setLabel("Magnitude (dB)");
        bodePlot->yAxis->setLabelColor(Qt::blue);
        bodePlot->yAxis->setTickLabelColor(Qt::blue);
        bodePlot->yAxis->setBasePen(QPen(Qt::blue));
        bodePlot->yAxis->setTickPen(QPen(Qt::blue));
        bodePlot->yAxis->setSubTickPen(QPen(Qt::blue));
        bodePlot->yAxis2->setVisible(true);
        bodePlot->yAxis2->setLabel("Phase (degrees)");
        bodePlot->yAxis2->setLabelColor(Qt::red);
        bodePlot->yAxis2->setTickLabelColor(Qt::red);
        bodePlot->yAxis2->setBasePen(QPen(Qt::red));
        bodePlot->yAxis2->setTickPen(QPen(Qt::red));
        bodePlot->yAxis2->setSubTickPen(QPen(Qt::red));
        bodePlot->yAxis2->setTickLabelFont(QFont("Arial", 8));
        bodePlot->yAxis2->setTickLength(5, 3);
        bodePlot->yAxis2->setNumberFormat("f");
        bodePlot->yAxis2->setNumberPrecision(1);
        bodePlot->legend->setVisible(true);
        bodePlot->legend->setBrush(QBrush(Qt::white));
        bodePlot->legend->setBorderPen(QPen(Qt::black));
        double minMag = *std::min_element(smoothedMagnitudes.begin(), smoothedMagnitudes.end());
        double maxMag = *std::max_element(smoothedMagnitudes.begin(), smoothedMagnitudes.end());
        bodePlot->yAxis->setRange(minMag - 5, maxMag + 5);
        double minPhase = *std::min_element(smoothedPhases.begin(), smoothedPhases.end());
        double maxPhase = *std::max_element(smoothedPhases.begin(), smoothedPhases.end());
        if (abs(maxPhase - minPhase) < 1.0) {
            minPhase = -180.0;
            maxPhase = 180.0;
        } else {
            minPhase = qMax(minPhase - 10, -180.0);
            maxPhase = qMin(maxPhase + 10, 180.0);
        }
        bodePlot->yAxis2->setRange(minPhase, maxPhase);
        bodePlot->yAxis2->setVisible(true);
        bodePlot->yAxis2->setTickLabels(true);
        bodePlot->yAxis2->setTicks(true);
        bodePlot->yAxis2->setSubTicks(true);
        bodePlot->replot();
        qDebug() << "[MainWindow] Bode plot created successfully";
        qDebug() << "[MainWindow] Magnitude range:" << minMag << "to" << maxMag << "dB";
        qDebug() << "[MainWindow] Phase range:" << minPhase << "to" << maxPhase << "degrees";
        qDebug() << "[MainWindow] Phase data points:" << smoothedPhases.size();
        for (int i = 0; i < qMin(10, smoothedPhases.size()); ++i) {
            qDebug() << "  Phase[" << i << "]:" << smoothedPhases[i] << "degrees";
        }
    } else {
        qDebug() << "[MainWindow] Bode plot widget is null!";
    }
    QSharedPointer<QCPAxisTickerFixed> phaseTicker(new QCPAxisTickerFixed);
    phaseTicker->setTickStep(45.0);
    phaseTicker->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    bodePlot->yAxis2->setTicker(phaseTicker);
}

void MainWindow::createTestBodePlot() {
    qDebug() << "[MainWindow] Creating test Bode plot...";

    // Create test frequency data (logarithmic sweep from 100Hz to 10kHz)
    sweepFrequencies.clear();
    sweepAmplitudes.clear();

    double startFreq = 100.0;
    double endFreq = 10000.0;
    int numPoints = 50;

    for (int i = 0; i < numPoints; ++i) {
        double freq = startFreq * pow(endFreq / startFreq, (double)i / (numPoints - 1));
        sweepFrequencies.append(freq);

        // Create a simple low-pass filter response for testing
        // H(f) = 1 / (1 + j*f/f0) where f0 = 1000 Hz
        double f0 = 1000.0;
        double magnitude = 1.0 / sqrt(1.0 + pow(freq / f0, 2));
        sweepAmplitudes.append(magnitude);
    }

    qDebug() << "[MainWindow] Created test data with" << sweepFrequencies.size() << "points";
    qDebug() << "[MainWindow] Frequency range:" << sweepFrequencies.first() << "to" << sweepFrequencies.last() << "Hz";

    // Plot the test Bode plot
    plotBodePlot();

    showStatus("Test Bode plot created");
}
void MainWindow::onStopSweepClicked() {
    stopSweep();
}
void MainWindow::onSweepStartFreqChanged(double freq) {
    sweepStartFreq = freq;
    qDebug() << "[MainWindow] Sweep start frequency changed to" << freq << "Hz";
}
void MainWindow::onSweepEndFreqChanged(double freq) {
    sweepEndFreq = freq;
    qDebug() << "[MainWindow] Sweep end frequency changed to" << freq << "Hz";
}
void MainWindow::onSweepSamplesChanged(int samples) {
    sweepSamples = samples;
    qDebug() << "[MainWindow] Sweep samples changed to" << samples;
}
void MainWindow::onSweepDelayChanged(int delay) {
    sweepDelay = delay;
    qDebug() << "[MainWindow] Sweep delay changed to" << delay << "ms";
}
void MainWindow::onStudentNameChanged() {
    studentName = studentNameEdit->text();
    readDeviceSignature();
}
void MainWindow::readDeviceSignature() { serialHandler->sendCommand(QByteArray(1, 'e')); }
void MainWindow::onOscilloscopeData(const QVector<double>& ch1, const QVector<double>& ch2, const QVector<double>& xvals) {
    // --- Low-pass filter logic ---
    QVector<double> filteredCh1 = ch1;
    QVector<double> filteredCh2 = ch2;
    QVector<double> filteredX = xvals;
    if (lpfCheckBox && lpfCheckBox->isChecked()) {
        // Simple moving average low-pass filter (window size 5)
        auto lowPass = [](const QVector<double>& data) {
            QVector<double> out;
            int N = data.size();
            int w = 5;
            for (int i = 0; i < N; ++i) {
                double sum = 0;
                int count = 0;
                for (int j = i - w/2; j <= i + w/2; ++j) {
                    if (j >= 0 && j < N) {
                        sum += data[j];
                        count++;
                    }
                }
                out.append(sum / count);
            }
            return out;
        };
        filteredCh1 = lowPass(filteredCh1);
        filteredCh2 = lowPass(filteredCh2);
        filteredX = lowPass(filteredX); // Optional: smooth x if needed
        // Remove first 10 points from all vectors if possible
        if (filteredCh1.size() > 10) filteredCh1 = filteredCh1.mid(10);
        if (filteredCh2.size() > 10) filteredCh2 = filteredCh2.mid(10);
        if (filteredX.size() > 10) filteredX = filteredX.mid(10);
    }
    // Pass filtered (or original) data to PlotManager
    plotManager->updateWaveform(filteredCh1, filteredCh2);
    ch1Buffer = filteredCh1;
    ch2Buffer = filteredCh2;
    timeBuffer = filteredX;
}
void MainWindow::onSerialError(const QString &msg) { showStatus(msg); }
void MainWindow::onStatusMessage(const QString &msg) { showStatus(msg); }
void MainWindow::onTabChanged(int idx) {
    // Remove the first widget (main plot) if present
    if (mainAreaLayout->count() > 0) {
        QLayoutItem* item = mainAreaLayout->takeAt(0);
        if (item) {
            QWidget* w = item->widget();
            if (w) w->setParent(nullptr);
            delete item;
        }
    }
    if (idx == 2) {
        // Show Bode plot only for Bode tab
        mainAreaLayout->insertWidget(0, bodePlot, 1);
    } else {
        // Always show oscilloscope plot for all other tabs
        mainAreaLayout->insertWidget(0, plot, 1);
    }
}

void MainWindow::performFFT(const QVector<double>& input, QVector<double>& output) {
    int n = input.size();
    if (n == 0) return;

    std::valarray<std::complex<double>> fft_data(n);
    for(int i=0; i<n; ++i) {
        fft_data[i] = std::complex<double>(input[i], 0);
    }

    // Basic DFT - for a real implementation, use a proper FFT library or algorithm
    for (int k = 0; k < n; ++k) {
        std::complex<double> sum(0.0, 0.0);
        for (int t = 0; t < n; ++t) {
            double angle = 2 * PI * t * k / n;
            sum += fft_data[t] * std::exp(std::complex<double>(0, -angle));
        }
        fft_data[k] = sum;
    }

    output.resize(n / 2);
    for (int i = 0; i < n / 2; ++i) {
        output[i] = std::abs(fft_data[i]) / n;
    }
}

void MainWindow::initializeWaveformTables()
{
    // Sin Table
    sinTable = {122, 124, 127, 130, 133, 136, 139, 142, 144, 147, 150, 153, 155, 158, 161, 164, 166, 169, 172, 174,
    177, 179, 182, 184, 187, 189, 191, 193, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 215, 217,
    219, 220, 222, 223, 225, 226, 227, 228, 230, 231, 232, 233, 233, 234, 235, 236, 236, 237, 237, 238,
    238, 238, 238, 238, 239, 238, 238, 238, 238, 238, 237, 237, 236, 236, 235, 234, 233, 233, 232, 231,
    230, 228, 227, 226, 225, 223, 222, 220, 219, 217, 215, 214, 212, 210, 208, 206, 204, 202, 200, 198,
    196, 193, 191, 189, 187, 184, 182, 179, 177, 174, 172, 169, 166, 164, 161, 158, 155, 153, 150, 147,
    144, 142, 139, 136, 133, 130, 127, 124, 122, 120, 117, 114, 111, 108, 105, 102, 100, 97, 94, 91,
    89, 86, 83, 80, 78, 75, 72, 70, 67, 65, 62, 60, 57, 55, 53, 51, 48, 46, 44, 42,
    40, 38, 36, 34, 32, 30, 29, 27, 25, 24, 22, 21, 19, 18, 17, 16, 14, 13, 12, 11,
    11, 10, 9, 8, 8, 7, 7, 6, 6, 6, 6, 6, 5, 6, 6, 6, 6, 6, 7, 7,
    8, 8, 9, 10, 11, 11, 12, 13, 14, 16, 17, 18, 19, 21, 22, 24, 25, 27, 29, 30,
    32, 34, 36, 38, 40, 42, 44, 46, 48, 51, 53, 55, 57, 60, 62, 65, 67, 70, 72, 75,
    78, 80, 83, 86, 89, 91, 94, 97, 100, 102, 105, 108, 111, 114, 117, 120};
    // Ramp Up Table
    rampUpTable = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
    29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
    53, 54, 55, 56, 57, 58, 59, 60, 61, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
    77, 78, 79, 80, 81, 82, 83, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
    101, 102, 103, 104, 105, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
    139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 150, 151, 152, 153, 154, 155, 156, 157,
    158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 172, 173, 174, 175, 176,
    177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 194, 195,
    196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215,
    216, 217, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234,
    235, 236, 237, 238, 239, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249};
    // Ramp Down Table
    rampDownTable = {254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240, 239, 238, 237, 236, 235,
    234, 234, 233, 232, 231, 230, 229, 228, 227, 226, 225, 224, 223, 222, 221, 220, 219, 218, 217, 216,
    215, 214, 213, 212, 211, 210, 209, 208, 207, 206, 205, 204, 203, 202, 201, 200, 199, 198, 197, 196,
    195, 194, 193, 193, 192, 191, 190, 189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178, 177,
    176, 175, 174, 173, 172, 171, 170, 169, 168, 167, 166, 165, 164, 163, 162, 161, 160, 159, 158, 157,
    156, 155, 154, 153, 152, 151, 151, 150, 149, 148, 147, 146, 145, 144, 143, 142, 141, 140, 139, 138,
    137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124, 123, 122, 121, 120, 119, 118,
    117, 116, 115, 114, 113, 112, 111, 110, 109, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100, 99,
    98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75,
    74, 73, 72, 71, 70, 69, 68, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51,
    50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26,
    26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5};
    // Triangle Table
    triangleTable = {5, 7, 9, 11, 13, 15, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 39,
    41, 43, 45, 47, 49, 51, 53, 55, 57, 59, 61, 62, 64, 66, 68, 70, 72, 74, 76,
    78, 80, 82, 83, 85, 87, 89, 91, 93, 95, 97, 99, 101, 103, 105, 106, 108, 110, 112,
    114, 116, 118, 120, 122, 124, 126, 128, 129, 131, 133, 135, 137, 139, 141, 143, 145, 147, 149,
    150, 152, 154, 156, 158, 160, 162, 164, 166, 168, 170, 172, 173, 175, 177, 179, 181, 183, 185,
    187, 189, 191, 193, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216, 217, 219, 221, 223,
    225, 227, 229, 231, 233, 235, 237, 239, 240, 242, 244, 246, 248, 250, 248, 246, 244, 242, 240, 239,
    237, 235, 233, 231, 229, 227, 225, 223, 221, 219, 217, 216, 214, 212, 210, 208, 206, 204, 202,
    200, 198, 196, 194, 193, 191, 189, 187, 185, 183, 181, 179, 177, 175, 173, 172, 170, 168, 166,
    164, 162, 160, 158, 156, 154, 152, 150, 149, 147, 145, 143, 141, 139, 137, 135, 133, 131, 129,
    128, 126, 124, 122, 120, 118, 116, 114, 112, 110, 108, 106, 105, 103, 101, 99, 97, 95, 93,
    91, 89, 87, 85, 83, 82, 80, 78, 76, 74, 72, 70, 68, 66, 64, 62, 61, 59, 57,
    55, 53, 51, 49, 47, 45, 43, 41, 39, 38, 36, 34, 32, 30, 28, 26, 24, 22, 20,
    18, 16, 15, 13, 11, 9, 7, 5};
    // Square Table
    squareTable = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250,
    250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250};
}

// DDS Command logic
void MainWindow::runDDS() {
    // Ensure waveform table is filled from selection
    onWaveformSelectionChanged(ddsWaveformCombo ? ddsWaveformCombo->currentIndex() : 0);
    setFrequency();
    if (!serialHandler) return;
    // Defensive: Ensure SetPeriodCmd and SamplesCmd are at least 3 bytes
    if (SetPeriodCmd.size() < 3) SetPeriodCmd.resize(3);
    if (SamplesCmd.size() < 3) SamplesCmd.resize(3);
    // Set command bytes
    SetPeriodCmd[0] = 0x70; // 'p'
    SamplesCmd[0] = 0x4E;   // 'N'
    // --- Debug output ---
    qDebug() << "[DDS] SetPeriodCmd:" << SetPeriodCmd.toHex();
    qDebug() << "[DDS] SamplesCmd:" << SamplesCmd.toHex();
    // Send SetPeriodCmd
    serialHandler->sendCommand(SetPeriodCmd);
    QThread::msleep(20);
    // Send SamplesCmd
    serialHandler->sendCommand(SamplesCmd);
    QThread::msleep(20);
    // Build DDS_OutCmd
    int safe_samples = std::min(no_of_samples, static_cast<int>(DDS_Table.size()));
    if (safe_samples <= 0 || safe_samples > 512) {
        qWarning() << "[DDS] Invalid sample count:" << safe_samples;
        return;
    }
    DDS_OutCmd.resize(safe_samples + 3);
    // Set SendWaveformCmd: 'r', 0x00, 0x00
    DDS_OutCmd[0] = 0x72; // 'r'
    DDS_OutCmd[1] = 0x00;
    DDS_OutCmd[2] = 0x00;
    for (int i = 0; i < safe_samples; ++i) {
        DDS_OutCmd[i + 3] = DDS_Table[i];
    }
    // Debug output for waveform data
    QByteArray wfBytes;
    for (int i = 0; i < std::min(16, safe_samples); ++i) wfBytes.append((char)DDS_Table[i]);
    qDebug() << "[DDS] DDS_Table (first 16):" << wfBytes.toHex();
    qDebug() << "[DDS] DDS_OutCmd (first 16 bytes):" << DDS_OutCmd.left(16).toHex() << "... size:" << DDS_OutCmd.size();
    serialHandler->sendCommand(DDS_OutCmd);
    QThread::msleep(20);
    // Defensive: Ensure RunDDSCmd is at least 3 bytes
    if (RunDDSCmd.size() < 3) RunDDSCmd.resize(3);
    // Set RunDDSCmd: 'f', 0x00, 0x00
    RunDDSCmd[0] = 0x66; // 'f'
    RunDDSCmd[1] = 0x00;
    RunDDSCmd[2] = 0x00;
    qDebug() << "[DDS] RunDDSCmd:" << RunDDSCmd.toHex();
    serialHandler->sendCommand(RunDDSCmd);
}

// Stubs for remaining functions
void MainWindow::createSweepArray() {}
void MainWindow::sendDigitalCommand() {}
void MainWindow::averageData() {
    // Implement averaging for 2Mbps rate like VB.NET
    // This function interpolates/averages the data for 2Mbps rate

    if (ch1Buffer.isEmpty() && ch2Buffer.isEmpty()) {
        return;
    }

    qDebug() << "[MainWindow] Applying averaging for 2Mbps rate";

    // Average CH1 data
    if (!ch1Buffer.isEmpty()) {
        QVector<double> tempCh1 = ch1Buffer;
        ch1Buffer.resize(dataLength);

        // First data point stays the same
        ch1Buffer[0] = tempCh1[0];

        // Average every 2 points and interpolate
        for (int i = 1; i < dataLength; i += 2) {
            if (i/2 + 1 < tempCh1.size()) {
                ch1Buffer[i] = (tempCh1[i/2] + tempCh1[i/2 + 1]) / 2.0;
            } else {
                ch1Buffer[i] = tempCh1[i/2];
            }

            if (i + 1 < dataLength && i/2 + 1 < tempCh1.size()) {
                ch1Buffer[i + 1] = tempCh1[i/2 + 1];
            }
        }
    }

    // Average CH2 data
    if (!ch2Buffer.isEmpty()) {
        QVector<double> tempCh2 = ch2Buffer;
        ch2Buffer.resize(dataLength);

        // First data point stays the same
        ch2Buffer[0] = tempCh2[0];

        // Average every 2 points and interpolate
        for (int i = 1; i < dataLength; i += 2) {
            if (i/2 + 1 < tempCh2.size()) {
                ch2Buffer[i] = (tempCh2[i/2] + tempCh2[i/2 + 1]) / 2.0;
            } else {
                ch2Buffer[i] = tempCh2[i/2];
            }

            if (i + 1 < dataLength && i/2 + 1 < tempCh2.size()) {
                ch2Buffer[i + 1] = tempCh2[i/2 + 1];
            }
        }
    }

    qDebug() << "[MainWindow] Averaging completed for 2Mbps rate";
}

// DDS Signal Output Helper Functions
void MainWindow::onFrequencyTextChanged(const QString &text) {
    bool ok;
    int freq = text.toInt(&ok);
    if (!ok) {
        QMessageBox::warning(this, "Invalid Input", "Not A Valid Number - Setting 1000");
        if (ddsFreqSpin) ddsFreqSpin->setValue(1000);
        Frequency = 1000;
    } else {
        Frequency = freq;
        if (Frequency > 50000) {
            QMessageBox::warning(this, "Out of Limit", "Out of Limit - Setting 50000");
            if (ddsFreqSpin) ddsFreqSpin->setValue(50000);
            Frequency = 50000;
        } else if (Frequency < 1) {
            QMessageBox::warning(this, "Out of Limit", "Out of Limit - Setting 1");
            if (ddsFreqSpin) ddsFreqSpin->setValue(1);
            Frequency = 1;
        }
    }
}

void MainWindow::onWaveformSelectionChanged(int index) {
    if (!ddsWaveformCombo) return;
    QString inputText = ddsWaveformCombo->currentText();
    if (inputText == "DDS Sin (1-50 kHz)") {
        DDS_Waveform = sinTable;
    } else if (inputText == "DDS Sqare(1-50 kHz)") {
        DDS_Waveform = squareTable;
    } else if (inputText == "DDS Tri (1-50 kHz)") {
        DDS_Waveform = triangleTable;
    } else if (inputText == "DDS RampUp (1-50 kHz)") {
        DDS_Waveform = rampUpTable;
    } else if (inputText == "DDS RampDn (1-50 kHz)") {
        DDS_Waveform = rampDownTable;
    } else if (inputText == "DDS Arb (1-50 kHz)") {
        openDDSFile();
        readCSVFileToArray();
        DDS_Waveform = arb_data;
    }
}

int MainWindow::get_phase_step(int frequency, int fclock) {
    double phase_step_temp = (frequency * pow(2, 16)) / fclock;
    int phase_step_int = static_cast<int>(std::floor(phase_step_temp));
    int step = nextPowerOf2(phase_step_int);
    if (step < 1) step = 1; // Never return 0
    return step;
}

int MainWindow::nextPowerOf2(int n) {
    if (n == 0) return 1;
    int count = 0;
    while (n != 0) {
        n = n >> 1;
        count++;
    }
    return 1 << count;
}

void MainWindow::GetCount_GetIndex(int phase_accumulator, int phase_step, int dds_index, int dds_count) {
    // Fill DDS_Table until dds_index == 256 or max 512 samples
    int table_size = DDS_Table.size();
    int waveform_size = DDS_Waveform.size();
    int unique_indices = 0;
    int last_index = 0;
    if (table_size > 0) {
        if (waveform_size > 0)
            DDS_Table[0] = DDS_Waveform[0];
        else
            DDS_Table[0] = 0;
    }
    dds_count = 1;
    dds_index = 0;
    std::vector<bool> seen(256, false);
    seen[0] = true;
    unique_indices = 1;
    while (dds_count < 512 && unique_indices < 256 && dds_count < table_size) {
        phase_accumulator += phase_step;
        dds_index = phase_accumulator >> 8;
        if (dds_index >= 256) dds_index = 255;
        if (waveform_size > 0 && dds_index < waveform_size)
            DDS_Table[dds_count] = DDS_Waveform[dds_index];
        else if (waveform_size > 0)
            DDS_Table[dds_count] = DDS_Waveform.back();
        else
            DDS_Table[dds_count] = 0;
        if (!seen[dds_index]) {
            seen[dds_index] = true;
            unique_indices++;
        }
        last_index = dds_index;
        dds_count++;
    }
    // If we exited because of sample limit, but never reached a full cycle, forcibly complete the table with the last value
    for (int i = dds_count; i < 512 && i < table_size; ++i) {
        if (waveform_size > 0)
            DDS_Table[i] = DDS_Waveform[last_index];
        else
            DDS_Table[i] = 0;
    }
    intermediate_dds_count = dds_count;
    debug_dds_index = dds_index;
}

void MainWindow::setFrequency() {
    // Ensure DDS_Waveform is at least 256 samples
    if (DDS_Waveform.size() < 256)
        DDS_Waveform.resize(256, 0);
    // Ensure DDS_Table is at least 512 samples
    if (DDS_Table.size() < 512)
        DDS_Table.resize(512, 0);
    DDS_Table.fill(0);
    divider = 32; // initial value of timer period
    timer_clock = 32 * 1000000; // fclk
    fclock = timer_clock / divider; // event clock
    Frequency = ddsFreqSpin ? static_cast<int>(ddsFreqSpin->value()) : 1000;
    // --- Hybrid method: perfect full cycle at low freq for all waveforms ---
    bool useCycleStretch = (Frequency < 1000);
    if (useCycleStretch) {
        int N = 512;
        int waveform_size = DDS_Waveform.size();
        for (int i = 0; i < N; ++i) {
            int table_index = static_cast<int>(round(i * (waveform_size - 1) / double(N - 1)));
            DDS_Table[i] = DDS_Waveform[table_index];
        }
        timer_period = static_cast<int>(timer_clock / (Frequency * N));
        no_of_samples = N;
        // Clamp as needed
        if (timer_period > 65535) timer_period = 65535;
        if (no_of_samples > 512) no_of_samples = 512;
        SetPeriodCmd[1] = static_cast<char>(timer_period / 256);
        SetPeriodCmd[2] = static_cast<char>(timer_period % 256);
        SamplesCmd[1] = static_cast<char>(no_of_samples / 256);
        SamplesCmd[2] = static_cast<char>(no_of_samples % 256);
        dds_array_length = N - 1;
        return;
    }
    // --- Standard DDS for all other cases ---
    int Ph_step_Powof2 = get_phase_step(Frequency, fclock);
    GetCount_GetIndex(0, Ph_step_Powof2, 0, 0);
    double freq_resolution = fclock * 1.0 / pow(2, 16);
    double fout = (fclock * 1.0) / pow(2, 16);
    fout = (Ph_step_Powof2 * fout);
    no_of_samples = static_cast<int>(fclock / fout);
    divider_corrected = static_cast<int>(round(divider * (fout * 1.0 / Frequency)));
    if (divider_corrected > 65535) divider_corrected = 65535;
    double clock_corrected = (timer_clock / divider_corrected) * 1.0;
    double frequency_corrected = ((Ph_step_Powof2 * clock_corrected) / pow(2, 16)) * 1.0;
    phase_step_final = static_cast<int>((fout * pow(2, 16)) / fclock);
    timer_period = divider_corrected;
    dds_array_length = intermediate_dds_count - 1;
    if (DDS_Table[dds_array_length] == 0 && dds_array_length > 0) {
        DDS_Table[dds_array_length] = DDS_Table[dds_array_length - 1];
    }
    const int MAX_TIMER_PERIOD = 65535;
    const int MAX_SAMPLES = 512;
    if (timer_period > MAX_TIMER_PERIOD) {
        timer_period = MAX_TIMER_PERIOD;
    }
    if (no_of_samples > MAX_SAMPLES) {
        no_of_samples = MAX_SAMPLES;
    }
    SetPeriodCmd[1] = static_cast<char>(timer_period / 256);
    SetPeriodCmd[2] = static_cast<char>(timer_period % 256);
    SamplesCmd[1] = static_cast<char>(no_of_samples / 256);
    SamplesCmd[2] = static_cast<char>(no_of_samples % 256);
}

void MainWindow::openDDSFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Open CSV File", "C:/", "CSV files (*.csv)");
    if (!fileName.isEmpty()) {
        strFileName = fileName;
    }
}

void MainWindow::readCSVFileToArray() {
    if (strFileName.isEmpty()) return;
    QFile file(strFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    int x = 0;
    while (!in.atEnd() && x < 256) {
        QString line = in.readLine();
        QStringList values = line.split(",");
        if (!values.isEmpty()) {
            arb_data[x] = static_cast<uint8_t>(values[0].toInt());
            x++;
        }
    }
    file.close();
}

void MainWindow::autoDetectAndConnectBoard() {
    if (isConnected) {
        // Stop the timer once connected
        if (portScanTimer) {
            portScanTimer->stop();
        }
        return;
    }

    const quint16 TARGET_VID = 0x03EB;
    const quint16 TARGET_PID = 0x2404;
    const auto ports = QSerialPortInfo::availablePorts();
    QStringList portInfoList;

    for (const QSerialPortInfo &port : ports) {
        QString info = QString("%1 (VID: %2, PID: %3)")
            .arg(port.portName())
            .arg(port.vendorIdentifier(), 4, 16, QChar('0'))
            .arg(port.productIdentifier(), 4, 16, QChar('0'));
        portInfoList << info;
        if (port.vendorIdentifier() == TARGET_VID && port.productIdentifier() == TARGET_PID) {
            qDebug() << "Attempting to connect to port:" << port.portName()
                     << "VID:" << QString::number(port.vendorIdentifier(), 16)
                     << "PID:" << QString::number(port.productIdentifier(), 16);
            showStatus(QString("Board detected, connecting to %1...").arg(port.portName()));
            serialHandler->openPort(port.portName());
            return;
        }
    }

    // Only show available ports message once, not repeatedly
    static bool portsMessageShown = false;
    if (!portsMessageShown && !portInfoList.isEmpty()) {
        showStatus(QString("Available ports: %1").arg(portInfoList.join(", ")));
        portsMessageShown = true;
    } else if (portInfoList.isEmpty() && !portsMessageShown) {
        showStatus("No serial ports available");
        portsMessageShown = true;
    }
}

void MainWindow::onRunDDSButtonClicked() {
    runDDS();
}

// Oscilloscope parameter setup and state logic
void MainWindow::getGain() {
    // Implement gain selection logic as in VB.NET
    // ...
}
void MainWindow::writeScrollbarOffsets() {
    // Implement offset/trigger scrollbar logic as in VB.NET
    // ...
}
void MainWindow::setGainBits() {
    // Implement gain command logic as in VB.NET
    // ...
}
void MainWindow::selectMode() {
    // Implement mode selection logic as in VB.NET
    // ...
}
void MainWindow::setSampleRate() {
    // Implement sample rate logic as in VB.NET
    // ...
}
void MainWindow::computeOffsetTrigger() {
    // Implement offset/trigger computation as in VB.NET
    // ...
}
void MainWindow::runScope() {
    // Implement run logic as in VB.NET (call setup, send capture, read, plot, handle BUSY, DoEvents, etc.)
    // ...
}
void MainWindow::readScope() {
    // Implement read logic as in VB.NET (wait for done, read data, scale, handle timeout/error)
    // ...
}

bool MainWindow::checkTriggerCondition(const QVector<double>& ch1Data, const QVector<double>& ch2Data)
{
    // For Add/Overwrite modes, we want to trigger on any significant signal activity
    // This makes the modes more practical and usable

    // Determine which channel to check for trigger
    const QVector<double>* triggerData = nullptr;
    if (ch1TrigRadio && ch1TrigRadio->isChecked()) {
        triggerData = &ch1Data;
    } else if (ch2TrigRadio && ch2TrigRadio->isChecked()) {
        triggerData = &ch2Data;
    } else {
        // Auto trigger - check both channels, use the one with more activity
        triggerData = &ch1Data; // Default to CH1 for auto
    }

    if (!triggerData || triggerData->isEmpty()) {
        qDebug() << "[MainWindow] Trigger check: No data available";
        return false;
    }

    // For Add/Overwrite modes, use a simpler trigger condition:
    // Trigger if there's significant signal variation (not just noise)
    double minVoltage = *std::min_element(triggerData->begin(), triggerData->end());
    double maxVoltage = *std::max_element(triggerData->begin(), triggerData->end());
    double signalRange = maxVoltage - minVoltage;

    qDebug() << "[MainWindow] Trigger check - Signal range:" << minVoltage << "to" << maxVoltage << "V (range:" << signalRange << "V)";

    // Trigger if signal has significant variation (more than 0.1V peak-to-peak)
    // This ensures we capture meaningful signals, not just noise
    if (signalRange > 0.1) {
        qDebug() << "[MainWindow] Trigger condition met - significant signal variation detected";
        return true;
    } else {
        qDebug() << "[MainWindow] Trigger condition not met - signal variation too small (" << signalRange << "V)";
        return false;
    }
}

// Stub for measurement update
void MainWindow::updateMeasurements(const QVector<double>& buffer, double sampleInterval,
    QLabel* pkpkLabel, QLabel* freqLabel, QLabel* meanLabel, QLabel* ampLabel, QLabel* periodLabel, QLabel* maxLabel, QLabel* minLabel) {
    if (!pkpkLabel || !freqLabel || !meanLabel || !ampLabel || !periodLabel || !maxLabel || !minLabel) return;
    if (buffer.isEmpty()) {
        pkpkLabel->setText("-");
        freqLabel->setText("-");
        meanLabel->setText("-");
        ampLabel->setText("-");
        periodLabel->setText("-");
        maxLabel->setText("-");
        minLabel->setText("-");
        return;
    }
    // Pk-Pk
    double minV = *std::min_element(buffer.begin(), buffer.end());
    double maxV = *std::max_element(buffer.begin(), buffer.end());
    double pkpk = maxV - minV;
    pkpkLabel->setText(QString::number(pkpk, 'f', 3));
    // Mean
    double mean = std::accumulate(buffer.begin(), buffer.end(), 0.0) / buffer.size();
    meanLabel->setText(QString::number(mean, 'f', 3));
    // Amplitude (half of Pk-Pk)
    double amplitude = pkpk / 2.0;
    ampLabel->setText(QString::number(amplitude, 'f', 3));
    // Max/Min
    maxLabel->setText(QString::number(maxV, 'f', 3));
    minLabel->setText(QString::number(minV, 'f', 3));
    // Frequency/Period (zero-crossing method, fallback to '-')
    int n = buffer.size();
    int lastCross = -1;
    int crossings = 0;
    double periodSum = 0.0;
    for (int i = 1; i < n; ++i) {
        if ((buffer[i-1] < mean && buffer[i] >= mean) || (buffer[i-1] > mean && buffer[i] <= mean)) {
            if (lastCross >= 0) {
                double period = (i - lastCross) * sampleInterval;
                periodSum += period;
                ++crossings;
            }
            lastCross = i;
        }
    }
    if (crossings > 0) {
        double avgPeriod = periodSum / crossings;
        double freq = avgPeriod > 0 ? 1.0 / avgPeriod : 0.0;
        periodLabel->setText(QString::number(avgPeriod, 'g', 6));
        freqLabel->setText(QString::number(freq, 'g', 6));
    } else {
        periodLabel->setText("-");
        freqLabel->setText("-");
    }
}

// Function to set maximum data length for longer x-axis
void MainWindow::setMaxDataLength(int length) {
    dataLength = length;
    timeBuffer.resize(dataLength);
    for (int i = 0; i < dataLength; ++i) {
        timeBuffer[i] = i * multiplier;
    }
}

double MainWindow::getYAxisRangeFromGain(double gain) const {
    // Convert gain to voltage range based on UI gain settings
    // Gain values: 0.5=±20V, 1.0=±10V, 2.0=±5V, 4.0=±2.5V, 8.0=±1.25V, 16.0=±0.625V
    if (gain <= 1.0) return 20.0;      // ±20V
    else if (gain <= 2.0) return 10.0; // ±10V
    else if (gain <= 4.0) return 5.0;  // ±5V
    else if (gain <= 8.0) return 2.5;  // ±2.5V
    else if (gain <= 16.0) return 1.25; // ±1.25V
    else return 0.625;                 // ±0.625V
}

void MainWindow::setupScope() {
    setTriggerMode();
    QThread::msleep(20);
    selectMode();
    QThread::msleep(20);
    computeOffsetTrigger();
    QThread::msleep(20);
    setGainBits();
    QThread::msleep(20);
    setSampleRate();
    firstRun = false;
}

void MainWindow::loadArbitraryWaveform() {
    QString fileName = QFileDialog::getOpenFileName(this, tr("Load Arbitrary Waveform"), "", tr("CSV Files (*.csv)"));
    if (!fileName.isEmpty()) {
        // Load CSV and send to device
        openDDSFile();
        readCSVFileToArray();
        showStatus("Arbitrary waveform loaded: " + fileName);
    }
}

void MainWindow::onDDSLoadArbClicked() {
    loadArbitraryWaveform();
}

void MainWindow::onMeasEditClicked() {
    showMeasurementEditDialog();
}

void MainWindow::showMeasurementEditDialog() {
    if (!measEditDialog) {
        measEditDialog = new QDialog(this);
        measEditDialog->setWindowTitle("Measurement Settings");
        measEditDialog->setModal(true);
        measEditDialog->resize(400, 300);

        QVBoxLayout* mainLayout = new QVBoxLayout(measEditDialog);

        // Channel 1 settings
        QGroupBox* ch1Group = new QGroupBox("Channel 1 Measurements");
        QVBoxLayout* ch1Layout = new QVBoxLayout(ch1Group);

        QStringList ch1Measurements = {"Peak-to-Peak", "Frequency", "Mean", "Amplitude", "Period", "Maximum", "Minimum"};
        QVector<QCheckBox*> ch1CheckBoxes;

        for (int i = 0; i < ch1Measurements.size(); ++i) {
            QCheckBox* cb = new QCheckBox(ch1Measurements[i]);
            cb->setChecked(false); // Default to none visible
            ch1CheckBoxes.append(cb);
            ch1Layout->addWidget(cb);
        }

        mainLayout->addWidget(ch1Group);

        // Channel 2 settings
        QGroupBox* ch2Group = new QGroupBox("Channel 2 Measurements");
        QVBoxLayout* ch2Layout = new QVBoxLayout(ch2Group);

        QStringList ch2Measurements = {"Peak-to-Peak", "Frequency", "Mean", "Amplitude", "Period", "Maximum", "Minimum"};
        QVector<QCheckBox*> ch2CheckBoxes;

        for (int i = 0; i < ch2Measurements.size(); ++i) {
            QCheckBox* cb = new QCheckBox(ch2Measurements[i]);
            cb->setChecked(false); // Default to none visible
            ch2CheckBoxes.append(cb);
            ch2Layout->addWidget(cb);
        }

        mainLayout->addWidget(ch2Group);

        // Buttons
        QHBoxLayout* buttonLayout = new QHBoxLayout();
        QPushButton* okButton = new QPushButton("OK");
        QPushButton* cancelButton = new QPushButton("Cancel");
        buttonLayout->addWidget(okButton);
        buttonLayout->addWidget(cancelButton);
        mainLayout->addLayout(buttonLayout);

        // Connect buttons
        connect(okButton, &QPushButton::clicked, measEditDialog, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, measEditDialog, &QDialog::reject);

        // Store checkboxes for later access
        measEditDialog->setProperty("ch1CheckBoxes", QVariant::fromValue(ch1CheckBoxes));
        measEditDialog->setProperty("ch2CheckBoxes", QVariant::fromValue(ch2CheckBoxes));
    }

    // Show current settings
    QVector<QCheckBox*> ch1CheckBoxes = measEditDialog->property("ch1CheckBoxes").value<QVector<QCheckBox*>>();
    QVector<QCheckBox*> ch2CheckBoxes = measEditDialog->property("ch2CheckBoxes").value<QVector<QCheckBox*>>();

    // Initialize with current visibility settings
    if (ch1MeasVisible.isEmpty()) {
        ch1MeasVisible.resize(7, false); // Default none visible
    }
    if (ch2MeasVisible.isEmpty()) {
        ch2MeasVisible.resize(7, false); // Default none visible
    }

    for (int i = 0; i < ch1CheckBoxes.size() && i < ch1MeasVisible.size(); ++i) {
        ch1CheckBoxes[i]->setChecked(ch1MeasVisible[i]);
    }
    for (int i = 0; i < ch2CheckBoxes.size() && i < ch2MeasVisible.size(); ++i) {
        ch2CheckBoxes[i]->setChecked(ch2MeasVisible[i]);
    }

    if (measEditDialog->exec() == QDialog::Accepted) {
        // Save settings
        for (int i = 0; i < ch1CheckBoxes.size() && i < ch1MeasVisible.size(); ++i) {
            ch1MeasVisible[i] = ch1CheckBoxes[i]->isChecked();
        }
        for (int i = 0; i < ch2CheckBoxes.size() && i < ch2MeasVisible.size(); ++i) {
            ch2MeasVisible[i] = ch2CheckBoxes[i]->isChecked();
        }

        // Apply visibility settings to measurement boxes
        applyMeasurementVisibility();
    }
}

void MainWindow::applyMeasurementVisibility() {
    // Determine which channel's visibility settings to use based on current display
    QVector<bool>& currentMeasVisible = (currentDisplayChannel == 1) ? ch1MeasVisible : ch2MeasVisible;

    // Check if any measurements are visible for the current channel
    bool anyVisible = false;
    for (bool visible : currentMeasVisible) {
        if (visible) {
            anyVisible = true;
            break;
        }
    }

    // Hide the entire box if no measurements are selected
    if (floatingMeasBox) {
        floatingMeasBox->setVisible(anyVisible);
    }

    // Apply visibility to individual measurement labels
    if (floatingPkpkLabel) floatingPkpkLabel->setVisible(currentMeasVisible.value(0, true));
    if (floatingFreqLabel) floatingFreqLabel->setVisible(currentMeasVisible.value(1, true));
    if (floatingMeanLabel) floatingMeanLabel->setVisible(currentMeasVisible.value(2, true));
    if (floatingAmpLabel) floatingAmpLabel->setVisible(currentMeasVisible.value(3, true));
    if (floatingPeriodLabel) floatingPeriodLabel->setVisible(currentMeasVisible.value(4, true));
    if (floatingMaxLabel) floatingMaxLabel->setVisible(currentMeasVisible.value(5, true));
    if (floatingMinLabel) floatingMinLabel->setVisible(currentMeasVisible.value(6, true));
}

void MainWindow::updateFloatingMeasurements(const QVector<double>& buffer, double sampleInterval) {
    if (!floatingPkpkLabel || !floatingFreqLabel || !floatingMeanLabel || !floatingAmpLabel ||
        !floatingPeriodLabel || !floatingMaxLabel || !floatingMinLabel) return;

    if (buffer.isEmpty()) {
        floatingPkpkLabel->setText("-");
        floatingFreqLabel->setText("-");
        floatingMeanLabel->setText("-");
        floatingAmpLabel->setText("-");
        floatingPeriodLabel->setText("-");
        floatingMaxLabel->setText("-");
        floatingMinLabel->setText("-");
        return;
    }

    // Update channel label and color based on current display channel
    if (floatingMeasBox) {
        QWidget* header = floatingMeasBox->findChild<QWidget*>();
        if (header) {
            QLabel* colorRect = header->findChild<QLabel*>();
            QLabel* channelLabel = header->findChild<QLabel*>();
            if (colorRect && channelLabel) {
                if (currentDisplayChannel == 1) {
                    colorRect->setStyleSheet("background: #00c800; border: 1px solid #222; border-radius: 3px;");
                    channelLabel->setText("Channel 1");
                    channelLabel->setStyleSheet("font-weight: bold; margin-left: 6px; color: #00c800; font-size: 12px;");
                    floatingMeasBox->setStyleSheet(
                        "QWidget {"
                        "   background-color: rgba(0, 0, 0, 128);"
                        "   border: 2px solid #00c800;"
                        "   border-radius: 8px;"
                        "   padding: 8px;"
                        "}"
                        "QLabel {"
                        "   background-color: transparent;"
                        "   color: #ffffff;"
                        "   font-size: 10px;"
                        "   font-weight: bold;"
                        "}"
                    );
                } else {
                    colorRect->setStyleSheet("background: #ffe100; border: 1px solid #222; border-radius: 3px;");
                    channelLabel->setText("Channel 2");
                    channelLabel->setStyleSheet("font-weight: bold; margin-left: 6px; color: #ffe100; font-size: 12px;");
                    floatingMeasBox->setStyleSheet(
                        "QWidget {"
                        "   background-color: rgba(0, 0, 0, 128);"
                        "   border: 2px solid #ffe100;"
                        "   border-radius: 8px;"
                        "   padding: 8px;"
                        "}"
                        "QLabel {"
                        "   background-color: transparent;"
                        "   color: #ffffff;"
                        "   font-size: 10px;"
                        "   font-weight: bold;"
                        "}"
                    );
                }
            }
        }
    }

    // Calculate measurements
    double minV = *std::min_element(buffer.begin(), buffer.end());
    double maxV = *std::max_element(buffer.begin(), buffer.end());
    double pkpk = maxV - minV;
    double mean = std::accumulate(buffer.begin(), buffer.end(), 0.0) / buffer.size();
    double amplitude = pkpk / 2.0;

    // Update labels
    floatingPkpkLabel->setText(QString::number(pkpk, 'f', 3));
    floatingMeanLabel->setText(QString::number(mean, 'f', 3));
    floatingAmpLabel->setText(QString::number(amplitude, 'f', 3));
    floatingMaxLabel->setText(QString::number(maxV, 'f', 3));
    floatingMinLabel->setText(QString::number(minV, 'f', 3));

    // Frequency/Period calculation
    int n = buffer.size();
    int lastCross = -1;
    int crossings = 0;
    double periodSum = 0.0;
    for (int i = 1; i < n; ++i) {
        if ((buffer[i-1] < mean && buffer[i] >= mean) || (buffer[i-1] > mean && buffer[i] <= mean)) {
            if (lastCross >= 0) {
                double period = (i - lastCross) * sampleInterval;
                periodSum += period;
                ++crossings;
            }
            lastCross = i;
        }
    }
    if (crossings > 0) {
        double avgPeriod = periodSum / crossings;
        double freq = avgPeriod > 0 ? 1.0 / avgPeriod : 0.0;
        floatingPeriodLabel->setText(QString::number(avgPeriod, 'g', 6));
        floatingFreqLabel->setText(QString::number(freq, 'g', 6));
    } else {
        floatingPeriodLabel->setText("-");
        floatingFreqLabel->setText("-");
    }
}

// Digital Frequency Generator logic (VB.NET style)
void MainWindow::setDigitalFrequency(int dig_freq)
{
    int dig_divider = 1;
    int dig_index = 0;
    int dig_count = (32 * 1000000) / dig_freq; // 32 MHz clock

    if (dig_count > 65535) {
        dig_divider = 2;
        dig_count /= 2;
        dig_index = 1;
        if (dig_count > 65535) {
            dig_divider = 4;
            dig_count /= 2;
            dig_index = 2;
            if (dig_count > 65535) {
                dig_divider = 8;
                dig_count /= 2;
                dig_index = 3;
                if (dig_count > 65535) {
                    dig_divider = 64;
                    dig_count /= 8;
                    dig_index = 4;
                    if (dig_count > 65535) {
                        dig_divider = 256;
                        dig_count /= 4;
                        dig_index = 5;
                        if (dig_count > 65535) {
                            dig_divider = 1024;
                            dig_count /= 4;
                            dig_index = 6;
                        }
                    }
                }
            }
        }
    }

    QByteArray setDigCountCmd = QByteArray::fromHex("630000");
    setDigCountCmd[1] = static_cast<char>(dig_count / 256);
    setDigCountCmd[2] = static_cast<char>(dig_count % 256);

    QByteArray setDigIndexCmd = QByteArray::fromHex("640000");
    setDigIndexCmd[1] = static_cast<char>(dig_index);

    if (serialHandler) {
        serialHandler->sendCommand(setDigCountCmd);
        QThread::msleep(30);
        serialHandler->sendCommand(setDigIndexCmd);
    }
}

// --- Add/Overwrite Functionality Implementation ---



void MainWindow::resetTraceCollection() {
    qDebug() << "[MainWindow] Resetting trace collection";

    // Clear collected traces
    collectedTracesCh1.clear();
    collectedTracesCh2.clear();
    currentTraceCount = 0;
    isCollectingTraces = false;

    // Note: runCount is NOT reset here - it should persist across runs
    // Only reset when switching modes or explicitly stopping
}

void MainWindow::updateTraceProgress() {
    // Update status bar with progress
    QString status = QString("Trace %1 of %2 collected").arg(currentTraceCount).arg(targetTraceCount);
    showStatus(status);
    qDebug() << "[MainWindow]" << status;
}

void MainWindow::processCollectedTraces() {
    qDebug() << "[MainWindow] Processing" << collectedTracesCh1.size() << "collected traces for 400-point display";

    if (collectedTracesCh1.isEmpty() || collectedTracesCh2.isEmpty()) {
        qDebug() << "[MainWindow] No traces to process";
        resetTraceCollection();
        return;
    }

    // Calculate total points
    int totalPoints = 0;
    for (int i = 0; i < collectedTracesCh1.size(); ++i) {
        totalPoints += collectedTracesCh1[i].size();
    }
    qDebug() << "[MainWindow] Total points collected:" << totalPoints;

    if (overwriteRadio && overwriteRadio->isChecked()) {
        // Overwrite mode: concatenate all traces into one long trace (400 points)
        qDebug() << "[MainWindow] Processing Overwrite mode - concatenating traces for 400 points";

        QVector<double> ch1Concat, ch2Concat;

        // Concatenate all traces
        for (int i = 0; i < collectedTracesCh1.size(); ++i) {
            ch1Concat += collectedTracesCh1[i];
            ch2Concat += collectedTracesCh2[i];
        }

        // Create time axis for concatenated data (400 points)
        int N = ch1Concat.size();
        QVector<double> x(N);
        for (int i = 0; i < N; ++i) {
            x[i] = i * multiplier;
        }

        // Plot concatenated data
        plotManager->setMode(acquisitionMode);
        plotManager->updateWaveform(ch1Concat, ch2Concat);

        // Update buffers
        ch1Buffer = ch1Concat;
        ch2Buffer = ch2Concat;
        timeBuffer = x;

        qDebug() << "[MainWindow] Overwrite mode: Plotted concatenated trace with" << N << "points";
        showStatus(QString("Overwrite: %1 traces concatenated (%2 points total) - Run #%3").arg(collectedTracesCh1.size()).arg(N).arg(runCount));

    } else if (addRadio && addRadio->isChecked()) {
        // Add mode: overlay all traces for comparison (400 points total)
        qDebug() << "[MainWindow] Processing Add mode - overlaying traces for 400 points";

        // Use PlotManager's multiple trace functionality
        plotManager->updateWaveformWithMultipleTraces(collectedTracesCh1, collectedTracesCh2);

        // Update buffers with concatenated data for measurements (400 points)
        QVector<double> ch1Concat, ch2Concat;
        for (int i = 0; i < collectedTracesCh1.size(); ++i) {
            ch1Concat += collectedTracesCh1[i];
            ch2Concat += collectedTracesCh2[i];
        }

        ch1Buffer = ch1Concat;
        ch2Buffer = ch2Concat;
        timeBuffer.resize(ch1Buffer.size());
        for (int i = 0; i < timeBuffer.size(); ++i) {
            timeBuffer[i] = i * multiplier;
        }

        qDebug() << "[MainWindow] Add mode: Plotted" << collectedTracesCh1.size() << "overlaid traces (" << ch1Buffer.size() << " points total)";
        showStatus(QString("Add: %1 traces overlaid (%2 points total) - Run #%3").arg(collectedTracesCh1.size()).arg(ch1Buffer.size()).arg(runCount));
    }

    // Reset collection state for next acquisition
    resetTraceCollection();

    // For Add/Overwrite modes, continue running to collect more 400-point acquisitions
    if (isRunning && isConnected) {
        qDebug() << "[MainWindow] Continuing Add/Overwrite acquisition for next 400-point set";
        // Start next acquisition cycle
        QTimer::singleShot(100, this, [this]() {
            if (isRunning && isConnected) {
                int serialMode = (acquisitionMode == 0) ? 1 : (acquisitionMode + 1);
                bool serialDualChannel = (acquisitionMode == 0);
                serialHandler->startOscilloscopeAcquisition(serialMode, 200, serialDualChannel);
            }
        });
    }
}

// Helper: Find best sample rate index for a given frequency (at least 20x)
static int findSampleRateIndex(double freq) {
    static const double sampleRates[] = {
        2000000, 1000000, 500000, 200000, 100000, 50000, 20000, 10000, 5000, 2000, 1000, 500, 200, 100
    };
    for (int i = 13; i >= 0; --i) { // Start from slowest
        if (sampleRates[i] > 9 * freq) {
            return i;
        }
    }
    return 0; // If none are > 9x, use the fastest
}

// Helper: Find local maxima/minima and their averages
static void findLocalExtrema(const QVector<double>& data, double& avgMax, double& avgMin) {
    QVector<double> maxs, mins;
    int N = data.size();
    for (int i = 1; i < N - 1; ++i) {
        if (data[i] > data[i-1] && data[i] > data[i+1]) maxs.append(data[i]);
        if (data[i] < data[i-1] && data[i] < data[i+1]) mins.append(data[i]);
    }
    avgMax = maxs.isEmpty() ? 0.0 : std::accumulate(maxs.begin(), maxs.end(), 0.0) / maxs.size();
    avgMin = mins.isEmpty() ? 0.0 : std::accumulate(mins.begin(), mins.end(), 0.0) / mins.size();
}

// --- TRIGGER SYSTEM: VB.NET LOGIC PORT ---
void MainWindow::setTriggerMode() {
    // --- VB.NET LOGIC PORT ---
    // 1. Trigger Mode Selection
    QByteArray trigSourceCmd;
    trigSourceCmd.append((char)0x54); // 'T'
    if (autoTrigRadio && autoTrigRadio->isChecked()) {
        trigSourceCmd.append((char)0x00); // Auto
    } else if (ch1TrigRadio && ch1TrigRadio->isChecked()) {
        trigSourceCmd.append((char)0x01); // CH1
    } else if (ch2TrigRadio && ch2TrigRadio->isChecked()) {
        trigSourceCmd.append((char)0x02); // CH2
    } else if (extTrigRadio && extTrigRadio->isChecked()) {
        trigSourceCmd.append((char)0x03); // Ext
    } else {
        trigSourceCmd.append((char)0x00); // Default to Auto
    }
    trigSourceCmd.append((char)0x00);
    serialHandler->sendCommand(trigSourceCmd);
    QThread::msleep(20);

    // 2. Trigger Polarity Setting
    QByteArray trigPolarityCmd;
    trigPolarityCmd.append((char)0x50); // 'P'
    if (hlTrigRadio && hlTrigRadio->isChecked()) {
        trigPolarityCmd.append((char)0x01); // H->L
    } else if (lhTrigRadio && lhTrigRadio->isChecked()) {
        trigPolarityCmd.append((char)0x00); // L->H
    } else {
        trigPolarityCmd.append((char)0x00); // Default L->H
    }
    trigPolarityCmd.append((char)0x00);
    serialHandler->sendCommand(trigPolarityCmd);
    QThread::msleep(20);

    // 3. Trigger Level Calculation and Command (12-bit DAC, msb/lsb split)
    double gain = 1.0;
    if (ch1TrigRadio && ch1TrigRadio->isChecked()) gain = ch1Gain;
    else if (ch2TrigRadio && ch2TrigRadio->isChecked()) gain = ch2Gain;
    else gain = 1.0;
    int trigLevelValue = trigLevel; // 0-4095 from slider
    // VB.NET: Trig_For_Up = 2048 + ((Ch1_Trig - 2048) / Gain) / (4 / 3)
    int trigForUp = 2048 + static_cast<int>(((trigLevelValue - 2048) / gain) / (4.0 / 3.0));
    if (trigForUp < 0) trigForUp = 0;
    if (trigForUp > 4095) trigForUp = 4095;
    int msb8 = trigForUp / 16;
    int lsb4 = (trigForUp - msb8 * 16) * 16;
    QByteArray setTrigValue;
    setTrigValue.append((char)0x4C); // 'L'
    setTrigValue.append((char)msb8);
    setTrigValue.append((char)lsb4);
    serialHandler->sendCommand(setTrigValue);
    QThread::msleep(20);

    // 4. Trigger Level Validation (show warning if out of range)
    double trigLine = (trigLevelValue * 10.0 / 2048.0 - 10.0) / gain;
    trigLine = std::round(trigLine * 100.0) / 100.0;
    double waveformMin = 0, waveformMax = 0;
    const QVector<double>* signalData = nullptr;
    if (ch1TrigRadio && ch1TrigRadio->isChecked() && !ch1Buffer.isEmpty()) signalData = &ch1Buffer;
    else if (ch2TrigRadio && ch2TrigRadio->isChecked() && !ch2Buffer.isEmpty()) signalData = &ch2Buffer;
    if (signalData) {
        waveformMin = *std::min_element(signalData->begin(), signalData->end());
        waveformMax = *std::max_element(signalData->begin(), signalData->end());
        if (trigLine > waveformMax || trigLine < waveformMin) {
            QMessageBox::warning(this, "Error", "Trigger level is outside signal range. Turning OFF Trigger.");
            if (autoTrigRadio) autoTrigRadio->setChecked(true);
        }
    }

    // 5. Trigger Line Drawing (on plot)
    if (plotManager) {
        bool triggerOnCh2 = (ch2TrigRadio && ch2TrigRadio->isChecked());
        plotManager->updateTriggerLevel(trigLine, triggerOnCh2);
    }
}

// Add this function to blink the LED like VB.NET
void MainWindow::blinkTestLED() {
    QByteArray testLEDcmd;
    testLEDcmd.append((char)0x74);
    testLEDcmd.append((char)0x00);
    testLEDcmd.append((char)0x00);
    if (isRunning) {
        onStopClicked();
        serialHandler->sendCommand(testLEDcmd);
        onRunClicked();
    } else if (serialHandler) {
        serialHandler->sendCommand(testLEDcmd);
    }
}
