#ifndef BALANCER_H
#define BALANCER_H

#include <Arduino.h>

#include "bq76920Driver.h"
#include "register.h"

class Balancer {
public:
    bool update(Bq76920Driver& driver, const PackSnapshot& snapshot, bool allowed);
    bool stop(Bq76920Driver& driver);
    bool active() const;
    uint8_t mask() const;

private:
    uint8_t chooseCellToBalance(const PackSnapshot& snapshot) const;

    uint8_t activeMask = 0;
    unsigned long startedAtMs = 0;
};

#endif
