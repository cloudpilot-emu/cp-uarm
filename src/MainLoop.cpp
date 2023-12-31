#include "MainLoop.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>

#include "util.h"

using namespace std;

namespace {
    constexpr int AVERAGE_TIMESLICES = 50;
    constexpr uint32_t BIN_SIZE = 1000000;
    constexpr uint32_t SAFETY_MARGIN_PCT = 97;
    constexpr uint32_t SAFETY_MARGIN_PCT_CATCHUP = 75;
    constexpr uint32_t TIMESLICE_SIZE_USEC = 1000000 / 50;
    constexpr uint32_t LAG_THRESHOLD_CATCHUP_USEC = (3 * TIMESLICE_SIZE_USEC / 2);
    constexpr uint32_t LAG_THRESHOLD_SKIP_USEC = 3 * TIMESLICE_SIZE_USEC;
    constexpr int PRESERVE_TIMESLICES_FOR_CATCHUP = 3;
}  // namespace

MainLoop::MainLoop(SoC* soc, uint64_t configuredCyclesPerSecond)
    : soc(soc),
      configuredCyclesPerSecond(configuredCyclesPerSecond),
      lastCyclesPerSecond(configuredCyclesPerSecond),
      cyclesPerSecondAverage(AVERAGE_TIMESLICES) {
    uint64_t now = timestampUsec();

    realTimeUsec = now;
    virtualTimeUsec = now;
    cyclesPerSecondAverage.Add(configuredCyclesPerSecond);
}

void MainLoop::Cycle(uint64_t now) {
    double deltaUsec = now - virtualTimeUsec;

    if (deltaUsec > LAG_THRESHOLD_SKIP_USEC) {
        deltaUsec = TIMESLICE_SIZE_USEC;
        virtualTimeUsec = now - deltaUsec;
        cyclesPerSecondAverage.Reset(1);

        // cerr << "too much lag, skipping forward" << endl << flush;
    } else if (lastDeltaUsec < LAG_THRESHOLD_CATCHUP_USEC &&
               deltaUsec >= LAG_THRESHOLD_CATCHUP_USEC) {
        cyclesPerSecondAverage.Reset(PRESERVE_TIMESLICES_FOR_CATCHUP);
    }

    lastDeltaUsec = deltaUsec;
    realTimeUsec = now;

    double cyclesPerSecond = CalculateCyclesPerSecond(
        deltaUsec >= LAG_THRESHOLD_CATCHUP_USEC ? SAFETY_MARGIN_PCT_CATCHUP : SAFETY_MARGIN_PCT);
    const uint64_t cyclesEmulated =
        socRun(soc, deltaUsec * cyclesPerSecond / 1E6, round(cyclesPerSecond));

    virtualTimeUsec += cyclesEmulated / cyclesPerSecond * 1E6;
    uint64_t now2 = timestampUsec();

    if (now2 != now) cyclesPerSecondAverage.Add((cyclesEmulated * 1000000) / (now2 - now));

    currentIps = cyclesPerSecond;
    currentIpsMax = cyclesPerSecondAverage.Calculate();
}

uint64_t MainLoop::GetTimesliceSizeUsec() const { return TIMESLICE_SIZE_USEC; }

uint32_t MainLoop::GetCurrentIps() const { return currentIps; }

uint32_t MainLoop::GetCurrentIpsMax() const { return currentIpsMax; }

uint64_t MainLoop::CalculateCyclesPerSecond(uint64_t safetyMargin) {
    const uint64_t avg = (cyclesPerSecondAverage.Calculate() * safetyMargin) / 100;
    const uint64_t avgBinned = max((avg / BIN_SIZE) * BIN_SIZE, static_cast<uint64_t>(BIN_SIZE));

    if (avgBinned < lastCyclesPerSecond || avg > lastCyclesPerSecond + BIN_SIZE + BIN_SIZE / 2)
        lastCyclesPerSecond = avgBinned;

    if (lastCyclesPerSecond > configuredCyclesPerSecond)
        lastCyclesPerSecond = configuredCyclesPerSecond;

    return lastCyclesPerSecond;
}
