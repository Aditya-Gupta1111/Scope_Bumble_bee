#pragma once
#include <QObject>

class DigitalIO : public QObject {
    Q_OBJECT
public:
    explicit DigitalIO(QObject *parent = nullptr);
    ~DigitalIO();
    void setDigitalOut(int value);
    void readDigitalIn();
    void runDigitalFreq(int frequency);
    // TODO: Add digital I/O methods
}; 