#include "DigitalIO.h"
#include <QDebug>

DigitalIO::DigitalIO(QObject *parent) : QObject(parent) {}

void DigitalIO::setDigitalOut(int value) {
    // Send digital out command to device via SerialHandler
    // ...
}
void DigitalIO::readDigitalIn() {
    // Send digital in read command to device via SerialHandler
    // ...
}
void DigitalIO::runDigitalFreq(int frequency) {
    // Send digital frequency generator command to device via SerialHandler
    // ...
}

DigitalIO::~DigitalIO() {} 