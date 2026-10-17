#ifndef HUMIDIFIER_PIEZO_PWM_H_
#define HUMIDIFIER_PIEZO_PWM_H_

#include <stdint.h>

constexpr uint32_t PIEZO_FREQUENCY_MIN_HZ = 1600000;
constexpr uint32_t PIEZO_FREQUENCY_MAX_HZ = 1800000;
constexpr uint32_t PIEZO_FREQUENCY_DEFAULT_HZ = 1700000;
constexpr uint32_t PIEZO_DUTY_DEFAULT_PERCENT = 50;

struct PiezoPwmTiming {
    uint32_t periodCycles;
    uint32_t pulseCycles;
    uint32_t actualFrequencyHz;
    uint32_t actualDutyBasisPoints;
};

/*
 * Calculate the cycle arguments accepted by Zephyr's ESP32 LEDC driver.
 * At an 80 MHz clock the allowed period is 45..50 cycles, and the driver
 * chooses five duty bits (32 steps, or 3.125% per step). Its integer-cycle
 * API also limits frequency precision, even though LEDC has a fractional
 * divider. Keep the resulting signal inside the requested frequency range.
 *
 * 0% and 100% are constant inactive/active outputs, respectively. Frequency
 * in the result describes the configured period, not oscillation at these
 * endpoints. Actual duty is rounded to the nearest basis point (0.01%).
 */
inline bool calculatePiezoPwm(uint64_t clockHz, uint32_t frequencyHz,
                              uint32_t dutyPercent, PiezoPwmTiming &timing)
{
    if (clockHz == 0 || frequencyHz < PIEZO_FREQUENCY_MIN_HZ ||
        frequencyHz > PIEZO_FREQUENCY_MAX_HZ || dutyPercent > 100) {
        return false;
    }

    const uint64_t minimumPeriod = clockHz / PIEZO_FREQUENCY_MAX_HZ +
        (clockHz % PIEZO_FREQUENCY_MAX_HZ != 0);
    uint64_t maximumPeriod = clockHz / PIEZO_FREQUENCY_MIN_HZ;
    if (minimumPeriod > UINT32_MAX || maximumPeriod < minimumPeriod) {
        return false;
    }
    if (maximumPeriod > UINT32_MAX) {
        maximumPeriod = UINT32_MAX;
    }

    uint64_t period = (clockHz + frequencyHz / 2) / frequencyHz;
    if (period < minimumPeriod) {
        period = minimumPeriod;
    } else if (period > maximumPeriod) {
        period = maximumPeriod;
    }

    // LEDC chooses the largest power-of-two duty resolution fitting a period.
    uint64_t dutyLevels = 1;
    while (dutyLevels * 2 <= period) {
        dutyLevels *= 2;
    }
    const uint64_t dutySteps = (dutyLevels * dutyPercent + 50) / 100;

    // The driver floors dutyLevels * pulseCycles / periodCycles. Rounding
    // this conversion upward preserves the chosen duty step through it.
    const uint64_t pulse = (dutySteps * period + dutyLevels - 1) / dutyLevels;

    timing.periodCycles = static_cast<uint32_t>(period);
    timing.pulseCycles = static_cast<uint32_t>(pulse);
    timing.actualFrequencyHz = static_cast<uint32_t>((clockHz + period / 2) / period);
    timing.actualDutyBasisPoints =
        static_cast<uint32_t>((dutySteps * 10000 + dutyLevels / 2) / dutyLevels);
    return true;
}

#endif // HUMIDIFIER_PIEZO_PWM_H_
