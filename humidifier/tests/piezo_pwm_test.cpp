#include "piezo_pwm.h"

#include <assert.h>
#include <cmath>
#include <cstdio>
#include <limits>

static constexpr uint64_t ESP32_CLOCK_HZ = 80000000;

static void checkFrequencyAndDutySweep()
{
    bool periodsSeen[51] = {};
    uint32_t previousPeriod = 51;
    for (uint32_t frequency = PIEZO_FREQUENCY_MIN_HZ;
         frequency <= PIEZO_FREQUENCY_MAX_HZ; frequency += 1000) {
        uint32_t previousActualDuty = 0;
        for (uint32_t duty = 0; duty <= 100; ++duty) {
            PiezoPwmTiming timing = {};
            assert(calculatePiezoPwm(ESP32_CLOCK_HZ, frequency, duty, timing));
            assert(timing.periodCycles >= 45 && timing.periodCycles <= 50);
            assert(timing.pulseCycles <= timing.periodCycles);
            assert(timing.actualFrequencyHz >= PIEZO_FREQUENCY_MIN_HZ);
            assert(timing.actualFrequencyHz <= PIEZO_FREQUENCY_MAX_HZ);
            assert(timing.actualDutyBasisPoints >= previousActualDuty);
            previousActualDuty = timing.actualDutyBasisPoints;
            periodsSeen[timing.periodCycles] = true;

            // Check the actual rational output, not only its rounded report.
            const double actualHz = static_cast<double>(ESP32_CLOCK_HZ) /
                                    timing.periodCycles;
            assert(actualHz >= PIEZO_FREQUENCY_MIN_HZ);
            assert(actualHz <= PIEZO_FREQUENCY_MAX_HZ);
            assert(std::abs(timing.actualFrequencyHz - actualHz) <= 0.5);

            // Model the existing LEDC driver's final integer duty conversion.
            const unsigned hardwareSteps = static_cast<unsigned>(
                32.0 * timing.pulseCycles / timing.periodCycles);
            const double hardwarePercent = 100.0 * hardwareSteps / 32;
            assert(std::abs(hardwarePercent - duty) <= 1.5625);
            assert(std::abs(timing.actualDutyBasisPoints - hardwarePercent * 100) <= 0.5);

            if (duty == 0) {
                assert(timing.pulseCycles == 0);
                assert(timing.actualDutyBasisPoints == 0);
                assert(timing.periodCycles <= previousPeriod);
                previousPeriod = timing.periodCycles;
            } else if (duty == 100) {
                assert(timing.pulseCycles == timing.periodCycles);
                assert(timing.actualDutyBasisPoints == 10000);
            }
        }
    }
    for (unsigned period = 45; period <= 50; ++period) {
        assert(periodsSeen[period]);
    }
}

static void checkKnownOutputs()
{
    struct Expected {
        uint32_t requestedHz;
        uint32_t period;
        uint32_t reportedHz;
    };
    const Expected cases[] = {
        {1600000, 50, 1600000},
        {1633000, 49, 1632653},
        {1667000, 48, 1666667},
        {1700000, 47, 1702128},
        {1740000, 46, 1739130},
        {1800000, 45, 1777778},
    };
    for (const Expected &expected : cases) {
        PiezoPwmTiming timing = {};
        assert(calculatePiezoPwm(ESP32_CLOCK_HZ, expected.requestedHz, 50, timing));
        assert(timing.periodCycles == expected.period);
        assert(timing.actualFrequencyHz == expected.reportedHz);
        assert(timing.actualDutyBasisPoints == 5000);
    }

    // 5% should reach the closest LEDC duty (6.25%), not be truncated to 3.125%.
    PiezoPwmTiming timing = {};
    assert(calculatePiezoPwm(ESP32_CLOCK_HZ, 1800000, 5, timing));
    assert(timing.pulseCycles == 3);
    assert(timing.actualDutyBasisPoints == 625);
}

static void checkRejectedInputs()
{
    PiezoPwmTiming timing = {123, 45, 67, 89};
    assert(!calculatePiezoPwm(0, 1700000, 50, timing));
    assert(!calculatePiezoPwm(1000000, 1700000, 50, timing));
    assert(!calculatePiezoPwm(ESP32_CLOCK_HZ, 0, 50, timing));
    assert(!calculatePiezoPwm(ESP32_CLOCK_HZ, 1599999, 50, timing));
    assert(!calculatePiezoPwm(ESP32_CLOCK_HZ, 1800001, 50, timing));
    assert(!calculatePiezoPwm(ESP32_CLOCK_HZ, 1700000, 101, timing));
    assert(!calculatePiezoPwm(std::numeric_limits<uint64_t>::max(), 1700000, 50, timing));
    assert(timing.periodCycles == 123 && timing.pulseCycles == 45);
    assert(timing.actualFrequencyHz == 67 && timing.actualDutyBasisPoints == 89);
}

int main()
{
    checkFrequencyAndDutySweep();
    checkKnownOutputs();
    checkRejectedInputs();
    std::puts("Piezo PWM tests passed (20,301 frequency/duty combinations).");
}
