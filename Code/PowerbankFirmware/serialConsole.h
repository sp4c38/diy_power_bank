#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

#include <Arduino.h>

#include "bq76920Driver.h"
#include "packMonitor.h"
#include "register.h"
#include "safetyPolicy.h"

class SerialConsole {
public:
    void begin();
    void poll(CommandHandler handler, const PackMonitor& monitor, const ControlState& controls, const Bq76920Driver& driver);
    void printStatus(const PackSnapshot& snapshot, const ControlState& controls, uint8_t socPercent);
    void printFaults(const PackSnapshot& snapshot);
    void printRaw(const PackMonitor& monitor, const Bq76920Driver& driver);

private:
    CommandId commandFromString(String command, bool& confirmed);
    void printHelp();
};

#endif
