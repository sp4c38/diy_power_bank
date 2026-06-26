#ifndef PACK_MONITOR_H
#define PACK_MONITOR_H

#include <Arduino.h>

#include "bq76920Driver.h"
#include "register.h"

class PackMonitor {
public:
    bool update(Bq76920Driver& driver);
    void applyPolicy(PackState state, uint16_t faults);
    void markBqOffline();
    const PackSnapshot& snapshot() const;
    const BqRawReadings& raw() const;
    uint8_t socPercent() const;
    uint16_t minCellMv() const;
    uint16_t maxCellMv() const;
    uint16_t cellDeltaMv() const;

private:
    void pushSample(const PackSnapshot& sample);
    PackSnapshot buildFilteredSnapshot(const PackSnapshot& sample);
    uint16_t deriveFaults(const PackSnapshot& sample, bool trusted) const;
    bool evaluateTrust(const PackSnapshot& sample);

    PackSnapshot current;
    PackSnapshot previousSample;
    BqRawReadings lastRaw;
    bool hasPreviousSample = false;
    uint8_t sampleIndex = 0;
    uint8_t sampleCount = 0;
    uint8_t trustedConsecutiveSamples = 0;
    uint8_t instabilityScore = 0;
    uint16_t cell1Samples[5] = {0, 0, 0, 0, 0};
    uint16_t cell2Samples[5] = {0, 0, 0, 0, 0};
    uint16_t cell5Samples[5] = {0, 0, 0, 0, 0};
    uint16_t packSamples[5] = {0, 0, 0, 0, 0};
    int16_t currentSamples[5] = {0, 0, 0, 0, 0};
    int16_t tempSamples[5] = {0, 0, 0, 0, 0};
};

#endif
