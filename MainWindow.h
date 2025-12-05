#pragma once
#include <QMainWindow>
#include <QSerialPortInfo>
#include <QTimer>
#include <QTabWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QTextEdit>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include "qcustomplot.h"
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QListWidget>
#include <QProgressBar>
#include <QButtonGroup>
#include <QDialog>
#include <QToolButton>
#include <QMouseEvent>

// Helper class for draggable floating widget
class DraggableWidget : public QWidget {
    Q_OBJECT
public:
    explicit DraggableWidget(QWidget* parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    bool m_dragging;
    QPoint m_dragPos;
};

class SerialHandler;
class PlotManager;
class DDSGenerator;
class DigitalIO;
class WaveformExporter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void GetCount_GetIndex(int phase_accumulator, int phase_step, int dds_index, int dds_count);
    int get_phase_step(int frequency, int fclock);
    int nextPowerOf2(int n);

private slots:
    // Serial communication
    void onConnectButtonClicked();
    void handleSerialConnectionStatus(bool connected);
    void handleSerialPortError(const QString &error);
    void handleSerialData(const QByteArray &data);
    void updateSerialPortList();
    
    // Oscilloscope controls
    void onRunClicked();
    void onStopClicked();
    void onAbortClicked();
    void onExportCSV();
    void onModeChanged(int index);
    void onSampleRateChanged(int index);
    
    // Channel controls
    void onCh1GainChanged(int idx);
    void onCh2GainChanged(int idx);
    void onCh1OffsetChanged(int value);
    void onCh2OffsetChanged(int value);
    void onTrigLevelChanged(int value);
    void onTrigSourceChanged(int idx);
    void onTrigPolarityChanged(int idx);
    
    // Measurement edit
    void onMeasEditClicked();
    void showMeasurementEditDialog();
    void applyMeasurementVisibility();
    
    // DDS Signal Generator
    void onDDSStartStopClicked();
    void onDDSWaveformChanged(int index);
    void onDDSFreqChanged(double freq);
    void loadArbitraryWaveform();
    void onDDSLoadArbClicked();
    
    // Digital I/O
    void onDigitalOutToggled(int bit);
    void refreshDigitalInputs();
    void onReadDigitalClicked();
    
    // Digital Frequency Generator
    void onDigFreqStartClicked();
    void onDigFreqChanged(double freq);
    
    // Sweep functionality
    void onSweepStartStopClicked();
    void onSweepStartFreqChanged(double freq);
    void onSweepEndFreqChanged(double freq);
    void onSweepSamplesChanged(int samples);
    void onSweepDelayChanged(int delay);
    void onStopSweepClicked();
    
    // Student info and signature
    void onStudentNameChanged();
    void readDeviceSignature();
    
    // Plot and display
    void updatePlot();
    void onOscilloscopeData(const QVector<double>& ch1, const QVector<double>& ch2, const QVector<double>& xvals);
    void requestOscilloscopeData();
    void onOscilloscopeRawDataReady(const QByteArray &ch1, const QByteArray &ch2, int dataLength, bool dualChannel);
    
    // Utility
    void onSerialError(const QString &msg);
    void onStatusMessage(const QString &msg);
    void onTabChanged(int idx);

    // DDS Signal Output Helper Functions
    void onFrequencyTextChanged(const QString &text);
    void onWaveformSelectionChanged(int index);
    void onRunDDSButtonClicked();

    void autoDetectAndConnectBoard();

    void updateMeasurements(const QVector<double>& buffer, double sampleInterval,
        QLabel* pkpkLabel, QLabel* freqLabel, QLabel* meanLabel, QLabel* ampLabel, QLabel* periodLabel, QLabel* maxLabel, QLabel* minLabel);
    void updateFloatingMeasurements(const QVector<double>& buffer, double sampleInterval);

    // Add/Overwrite functionality
    void resetTraceCollection();
    void updateTraceProgress();
    void processCollectedTraces();

private:
    void setupUi();
    void setupConnections();
    void updateUiState();
    void showStatus(const QString &msg);
    void processOscilloscopeData(const QByteArray &data);
    void performFFT(const QVector<double>& input, QVector<double>& output);
    void averageData();
    void createSweepArray();
    void sendDDSCommand();
    void sendDigitalCommand();
    void sendTriggerCommand();
    void testTriggerFunctionality();
    void sendGainCommand();
    void sendOffsetCommand();
    void sendModeCommand();
    void sendSampleRateCommand();
    
    // Sweep helper functions
    void createSweepFrequencyArray();
    void setDDSForSweep();
    void stopSweep();
    void plotBodePlot();
    void createTestBodePlot(); // For testing Bode plot functionality
    
    // Waveform tables (from VB.NET)
    void initializeWaveformTables();
    QVector<uint8_t> sinTable, squareTable, triangleTable, rampUpTable, rampDownTable;
    
    // UI widgets - Serial Connection
    QComboBox *serialPortCombo;
    QPushButton *connectButton;
    QLabel *statusLabel;
    
    // UI widgets - Oscilloscope Controls
    QPushButton *runBtn, *stopBtn, *abortBtn, *exportBtn;
    QComboBox *modeCombo, *sampleRateCombo;
    QRadioButton *bothChRadio, *ch1Radio, *ch2Radio, *xyRadio, *fftCh1Radio, *fftCh2Radio;
    QRadioButton *fftBothRadio; // NEW: for FFT Both CH1 & CH2
    QRadioButton *continuousRadio, *overwriteRadio, *addRadio;
    
    // UI widgets - Channel Controls
    QButtonGroup* ch1GainGroup = nullptr;
    QButtonGroup* ch2GainGroup = nullptr;
    QComboBox* ch1GainCombo = nullptr;
    QComboBox* ch2GainCombo = nullptr;
    QSlider *ch1OffsetSlider, *ch2OffsetSlider, *trigLevelSlider;
    QLineEdit *ch1OffsetEdit, *ch2OffsetEdit, *trigLevelEdit;
    QRadioButton *autoTrigRadio, *ch1TrigRadio, *ch2TrigRadio, *extTrigRadio;
    QRadioButton *lhTrigRadio, *hlTrigRadio;
    
    // UI widgets - DDS Signal Generator
    QComboBox *ddsWaveformCombo;
    QDoubleSpinBox *ddsFreqSpin;
    QPushButton *ddsStartStopBtn, *ddsLoadArbBtn;
    QListWidget *ddsWaveformList;
    
    // UI widgets - Digital I/O
    QPushButton *digitalOutButtons[4];
    QLabel *digitalInLabels[4];
    QPushButton *readDigitalBtn;
    
    // UI widgets - Digital Frequency Generator
    QDoubleSpinBox *digFreqSpin;
    QComboBox *digDividerCombo;
    QPushButton *digFreqStartBtn;
    
    // UI widgets - Sweep
    QDoubleSpinBox *sweepStartSpin, *sweepEndSpin;
    QSpinBox *sweepSamplesSpin, *sweepDelaySpin;
    QPushButton *sweepStartBtn, *stopSweepBtn;
    QProgressBar *sweepProgress;
    
    // UI widgets - Bode Plot
    QPushButton *clearBodeBtn, *exportBodeBtn;
    
    // UI widgets - Student Info
    QLineEdit *studentNameEdit, *signatureEdit;
    
    // UI widgets - Plot
    QTabWidget *tabWidget;
    QWidget *oscTab, *settingsTab;
    
    // Data buffers
    QVector<double> ch1Buffer, ch2Buffer, timeBuffer;
    
    // FFT buffers
    QVector<double> ch1FFT, ch2FFT, freqBuffer;
    
    // Accumulated data for overwrite mode
    QVector<double> accumulatedCh1, accumulatedCh2, accumulatedTime;
    int overwriteAcquisitionCount;
    
    // Store data for ADD mode (multiple acquisitions on same time axis)
    QVector<QVector<double>> storedCh1Data, storedCh2Data;
    int addModeAcquisitionCount;
    
    // State variables
    bool isConnected = false;
    bool isRunning = false;
    bool oscilloscopeRunning = false;
    bool ddsRunning = false;
    bool digFreqRunning = false;
    bool sweepRunning = false;
    bool fftMode = false;
    bool xyMode = false;
    bool overplotMode = false;
    bool autoCyclingActive = false;
    
    // Configuration variables
    int currentMode = 0;
    int dataLength = 400;
    double multiplier = 0.5;
    double maxFrequency = 100000;
    QString heading1 = "Time(uSec)";
    double scaleFactor = 5.0 / 4.8;
    double offsetGainFactor = 1.1774;
    double ch1Gain = 0.5, ch2Gain = 0.5;
    int ch1Offset = 0, ch2Offset = 0, trigLevel = 2048;
    int trigSource = 0, trigPolarity = 0;
    double ddsFrequency = 1000.0;
    double digFrequency = 10000.0;
    double sweepStartFreq = 100.0, sweepEndFreq = 10000.0;
    int sweepSamples = 100, sweepDelay = 100;
    int sweepIndex = 0;
    
    // Sweep data storage for Bode plot
    QVector<double> sweepFrequencies;
    QVector<double> sweepAmplitudes;
    QVector<double> sweepPhases;
    QVector<double> sweepMagnitudes; // in dB
    // Add these for storing input/output waveforms for each sweep frequency
    QVector<QVector<double>> sweepInputWaves;
    QVector<QVector<double>> sweepOutputWaves;
    
    quint8 digitalOutState = 0;
    QString studentName = "Student";
    QString deviceSignature = "12345";
    
    // Timers
    QTimer *plotTimer, *dataRequestTimer, *sweepTimer;
    QElapsedTimer lastDataReceived;
    
    // Managers
    SerialHandler *serialHandler;
    PlotManager *plotManager;
    DDSGenerator *ddsGenerator;
    DigitalIO *digitalIO;
    WaveformExporter *waveformExporter;

    // DDS Signal Output Data Members
    QVector<uint8_t> DDS_Waveform;
    QVector<uint8_t> DDS_Table;
    QVector<uint8_t> arb_data;
    QByteArray SetPeriodCmd;
    QByteArray SamplesCmd;
    QByteArray DDS_OutCmd;
    QByteArray RunDDSCmd;
    int Frequency;
    int divider, timer_clock, fclock, Ph_step_Powof2, intermediate_dds_count, debug_dds_index, dds_array_length, no_of_samples, divider_corrected, phase_step_final, timer_period;
    QString strFileName;
    // DDS Signal Output Helper Functions
    void setFrequency();
    void runDDS();
    void openDDSFile();
    void readCSVFileToArray();

    QTimer *portScanTimer;
    QTimer *plotRateLimitTimer;
    QString lastConnectedPort;

    // Oscilloscope state variables
    double Ch1_Gain = 1.0, Ch2_Gain = 1.0, Gain = 1.0;
    double OC1 = 0.0, Scale_Factor = 1.0417, Offset_Gain_Factor = 1.1774;
    int Ch1_Offset = 0, Ch2_Offset = 0, Ch1_Trig = 0;
    int Ch1_Offset_Scroll_Corrected = 0, Ch2_Offset_Scroll_Corrected = 0, Ch1_Trigger_Scroll_Corrected = 0;
    int Ch1_Offset1 = 0, Ch2_Offset1 = 0, Trig_For_Up = 0;
    int Data_Length = 200, Read_Mode = 1, Sample_Rate_Selection = 1;
    int SetGainCmd[3] = {0x47, 0, 0};
    int SetCH1_Offset[3] = {0x4F, 0, 0};
    int SetCH2_Offset[3] = {0x6F, 0, 0};
    int SetTrigValue[3] = {0x4C, 0, 0};
    int SetModeCmd[3] = {0x46, 0, 0};
    int SetSampleRateCmd[3] = {0x53, 0, 0};
    int CaptureCmd[3] = {0x43, 0, 0};
    int ReadDataCmd[3] = {0x44, 0, 0};
    bool Keep_Running = false, Overplot = false, first_run = true;

    // Methods for oscilloscope logic
    void getGain();
    void writeScrollbarOffsets();
    void setGainBits();
    void setTriggerMode();
    void selectMode();
    void setSampleRate();
    void computeOffsetTrigger();
    void runScope();
    void readScope();

    
    // Function to set maximum data length for longer x-axis
    void setMaxDataLength(int length);
    
    // Helper function to get Y-axis range from gain setting
    double getYAxisRangeFromGain(double gain) const;
    
    // Trigger condition checking
    bool checkTriggerCondition(const QVector<double>& ch1Data, const QVector<double>& ch2Data);

    // State variables for oscilloscope logic
    int ch1OffsetCorrected = 0, ch2OffsetCorrected = 0, ch1TrigCorrected = 0;
    int setGainCmd[3] = {0x47, 0, 0};
    int setCH1Offset[3] = {0x4F, 0, 0};
    int setCH2Offset[3] = {0x6F, 0, 0};
    int setTrigValue[3] = {0x4C, 0, 0};
    int setModeCmd[3] = {0x46, 0, 0};
    int setSampleRateCmd[3] = {0x53, 0, 0};
    int captureCmd[3] = {0x43, 0, 0};
    int readDataCmd[3] = {0x44, 0, 0};
    bool keepRunning = false, overplot = false, firstRun = true;
    int ets = 0, etsError = 0;
    // ... other state as needed ...
    void setupScope();
    // ... UI slots for scrollbars, textboxes, radio buttons ...

    // Maximum data length constants (from VB.NET)
    static const int MAX_DATA_LENGTH = 400; // Maximum points per channel
    static const int MAX_DUAL_CHANNEL_LENGTH = 200; // Maximum points per channel in dual mode

    QCheckBox* autoYRangeCh1CheckBox; // Checkbox for auto y-range for Channel 1
    QCheckBox* autoYRangeCh2CheckBox; // Checkbox for auto y-range for Channel 2

    // UI widgets - Debug/Raw ADC
    QCheckBox* showRawAdcCheckBox = nullptr;
    QTextEdit* rawAdcTerminal = nullptr;

    // Display/DFT mode state
    bool dftMode = false;
    int dftChannel = 0; // 1=CH1, 2=CH2
    int acquisitionMode = 0; // 0=Both, 1=CH1, 2=CH2

    QLabel *pkpkLabel = nullptr;
    QLabel *freqLabel = nullptr;
    QLabel *meanLabel = nullptr;
    QLabel *ampLabel = nullptr;
    QLabel *periodLabel = nullptr;
    QLabel *maxLabel = nullptr;
    QLabel *minLabel = nullptr;

    // Floating measurement box and edit button
    QWidget* floatingMeasBox = nullptr;
    QToolButton* measEditButton = nullptr;
    QDialog* measEditDialog = nullptr;
    // Store which fields are visible for each channel
    QVector<bool> ch1MeasVisible;
    QVector<bool> ch2MeasVisible;
    
    // Measurement labels for floating box (single box for both channels)
    QLabel* floatingPkpkLabel = nullptr;
    QLabel* floatingFreqLabel = nullptr;
    QLabel* floatingMeanLabel = nullptr;
    QLabel* floatingAmpLabel = nullptr;
    QLabel* floatingPeriodLabel = nullptr;
    QLabel* floatingMaxLabel = nullptr;
    QLabel* floatingMinLabel = nullptr;
    
    // Current channel being displayed (1 or 2)
    int currentDisplayChannel = 1;

    void setDigitalFrequency(int dig_freq);

    // Member variables for Add/Overwrite functionality
    int targetTraceCount = 2;  // Number of traces to collect (starts at 2, increments each run)
    int currentTraceCount = 0; // Current number of collected traces
    QVector<QVector<double>> collectedTracesCh1; // Dynamic storage for traces
    QVector<QVector<double>> collectedTracesCh2;
    bool isCollectingTraces = false; // Flag to indicate trace collection mode
    int runCount = 0; // Track number of times Run has been pressed

    QHBoxLayout* mainAreaLayout = nullptr;
    QTabWidget* rightPanelTabs = nullptr;
    QCustomPlot* plot = nullptr;
    QCustomPlot* bodePlot = nullptr;
    QCheckBox* lpfCheckBox = nullptr;

    void blinkTestLED();
}; 
