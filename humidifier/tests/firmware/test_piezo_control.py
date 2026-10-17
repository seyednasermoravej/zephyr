#!/usr/bin/env python3
"""Exercise production piezo parsing/control and Zephyr's real JSON encoder.

Run: python3 tests/firmware/test_piezo_control.py
The checkout's parent must be the Zephyr source tree (or set ZEPHYR_BASE).
Only GPIO/PWM/NVS and the mutex are doubled; parsing and control methods are real.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ZEPHYR = Path(os.environ.get("ZEPHYR_BASE", ROOT.parent))
source = (ROOT / "src/humidifier.cpp").read_text()
header = (ROOT / "inc/humidifier.h").read_text()
structs = header[header.index("#define NUM_OF_PIEZOS"):header.index("class Humidifier")]


def method(name, next_name):
    start = source.index("int Humidifier::" + name + "(")
    end = source.index(next_name, start)
    return source[start:end]


methods = (
    method("disablePiezos", "void Humidifier::buttonsHandlerWrapper")
    + method("parsePiezosPost", "void Humidifier::parseFanPost")
    + method("piezosStatus", "int Humidifier::credentialStatus")
)

harness = r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zephyr/data/json.h>
#include "piezo_pwm.h"
#include "piezo_command.h"
#define CONFIG_PWM_LED_ESP32 1
#define DT_NODE_HAS_STATUS(...) 1
#define LOG_ERR(...) ((void)0)
struct SettingsLock {};
'''

hardware = r'''
static std::vector<std::string> events;
struct gpio_dt_spec { int pin; };
static const gpio_dt_spec selectors[] = {{0}, {1}, {2}};
struct pwm_dt_spec { const void *dev; int channel; int flags; };
static const pwm_dt_spec piezoPwm{nullptr, 0, 0};
static int selected = -1, gpioFailure = 0, pwmFailure = 0, clockFailure = 0;
static int offFailurePin = -1, stopFailure = 0;
static uint32_t periodCycles, pulseCycles;
static int gpio_pin_set_dt(const gpio_dt_spec *spec, int value) {
    events.push_back("gpio" + std::to_string(spec->pin) + "=" + std::to_string(value));
    if (value) {
        assert(selected == -1); // At most one channel may be enabled.
        if (gpioFailure) return gpioFailure;
        selected = spec->pin;
    } else {
        if (spec->pin == offFailurePin) return -EIO;
        if (selected == spec->pin) selected = -1;
    }
    return 0;
}
static int pwm_set_pulse_dt(const pwm_dt_spec *, uint32_t pulse) {
    events.push_back("pwmoff");
    assert(selected == -1);
    pulseCycles = pulse;
    return stopFailure;
}
static int pwm_get_cycles_per_sec(const void *, int, uint64_t *clock) {
    *clock = 80000000;
    return clockFailure;
}
static int pwm_set_cycles(const void *, int, uint32_t period, uint32_t pulse, int) {
    events.push_back("pwmset");
    assert(selected == -1); // Reconfigure only after disconnecting selectors.
    periodCycles = period;
    pulseCycles = pulse;
    return pwmFailure;
}
class Humidifier {
public:
    Settings settings{};
    bool hardwareReady = true;
    int saveResult = 0, saves = 0;
    int disablePiezos();
    int setPiezoPwm(int8_t piezoNum);
    int parsePiezosPost(char *buf, size_t len);
    int piezosStatus(char *buf, size_t len);
    int writeSettings() { ++saves; return saveResult; }
};
'''

checks = r'''
static Humidifier device;
static void reset() {
    device = Humidifier{};
    device.settings.piezos.active = -1;
    device.settings.piezos.piezosNum = NUM_OF_PIEZOS;
    for (auto &piezo : device.settings.piezos.piezos) {
        piezo.brightness = 20;
        piezo.intensity = 50;
        piezo.frequencyHz = 1700000;
    }
    selected = -1;
    gpioFailure = pwmFailure = clockFailure = 0;
    offFailurePin = -1;
    stopFailure = 0;
    periodCycles = pulseCycles = 0;
    events.clear();
}
static int post(std::string json) {
    // Keep the public mutable-buffer interface exercised even with read-only parsing.
    std::vector<char> buffer(json.begin(), json.end());
    buffer.push_back('\0');
    return device.parsePiezosPost(buffer.data(), json.size());
}
int main() {
    PiezoStatusUpdate command{42, 43, 44, 45};
    assert(parsePiezoCommand(nullptr, 0, command) == -EINVAL);
    assert(parsePiezoCommand("{}", 2, command) == -EINVAL && command.piezoNum == 42);
    const std::string limits = "{\"piezoNum\":-2147483648,\"intensity\":2147483647}";
    assert(parsePiezoCommand(limits.data(), limits.size(), command) == 5);
    assert(command.piezoNum == INT32_MIN && command.intensity == INT32_MAX);
    for (const std::string &overflow : {
        "{\"piezoNum\":2147483648}", "{\"piezoNum\":-2147483649}",
        "{\"piezoNum\":99999999999999999999999999}"}) {
        assert(parsePiezoCommand(overflow.data(), overflow.size(), command) == -ERANGE);
        assert(command.piezoNum == INT32_MIN);
    }
    reset();
    assert(post(" \t\n{ \"frequencyHz\" : 1700000, \"piezoNum\" : -0 }\r\n") == 0);
    assert(selected == 0);
    for (int index = 0; index < NUM_OF_PIEZOS; ++index) {
        events.clear();
        assert(post("{\"piezoNum\":" + std::to_string(index) + "}") == 0);
        assert(device.settings.piezos.active == index && selected == index);
        assert(events == std::vector<std::string>({"gpio0=0", "gpio1=0", "gpio2=0", "pwmoff", "pwmset", "gpio" + std::to_string(index) + "=1"}));
    }
    assert(post("{\"piezoNum\":1,\"intensity\":0,\"frequencyHz\":1600000,\"brightness\":0}") == 0);
    assert(periodCycles == 50 && pulseCycles == 0);
    assert(device.settings.piezos.piezos[1].intensity == 0);
    assert(device.settings.piezos.piezos[0].intensity == 50);
    assert(post("{\"piezoNum\":1,\"intensity\":100,\"frequencyHz\":1800000,\"brightness\":100}") == 0);
    assert(periodCycles == 45 && pulseCycles == periodCycles);
    assert(device.settings.piezos.actualDutyBasisPoints == 10000);
    assert(post("{\"piezoNum\":1}") == 0);
    assert(device.settings.piezos.piezos[1].frequencyHz == 1800000);
    assert(device.settings.piezos.piezos[1].intensity == 100);
    assert(post("{\"piezoNum\":-1}") == 0);
    assert(selected == -1 && pulseCycles == 0);
    assert(device.settings.piezos.active == -1);
    assert(device.settings.piezos.actualFrequencyHz == 0);
    assert(device.settings.piezos.actualDutyBasisPoints == 0);

    const char *invalid[] = {
        "", "{}", "[]", "null", "{", "{\"intensity\":50}",
        "{\"piezoNum\":-2}", "{\"piezoNum\":3}", "{\"piezoNum\":256}",
        "{\"piezoNum\":0,\"intensity\":-1}", "{\"piezoNum\":0,\"intensity\":101}",
        "{\"piezoNum\":0,\"brightness\":-1}", "{\"piezoNum\":0,\"brightness\":101}",
        "{\"piezoNum\":0,\"frequencyHz\":1599999}", "{\"piezoNum\":0,\"frequencyHz\":1800001}",
        "{\"piezoNum\":0,\"intensity\":1.5}", "{\"piezoNum\":0,\"intensity\":true}",
        "{\"piezoNum\":0,\"intensity\":\"50\"}", "{\"piezoNum\":0,\"intensity\":null}",
        "{\"piezoNum\":0,\"frequencyHz\":[]}", "{\"piezoNum\":0,\"frequencyHz\":{}}",
        "{\"piezoNum\":0,\"frequencyHz\":99999999999999999999999999}",
        "{\"piezoNum\":+0}", "{\"piezoNum\":00}", "{\"piezoNum\":-01}",
        "{\"piezoNum\":0e0}", "{\"piezoNum\":0.0}", "{\"piezoNum\":-}",
        "{\"piezoNum\":0,\"intensity\":}", "{\"piezoNum\":0,\"intensity\" 10}",
        "{\"piezoNum\":0,\"intensity\":10", "{\"piezoNum\":0,\"intensity:10}",
        "{\"piezoNum\":0}garbage", "{\"piezoNum\":0}{}", "{\"piezoNum\":0,}",
        "{\"piezoNum\":0,\"piezoNum\":2}", "{\"piezoNum\":0,\"unknown\":50}",
        "{\"piezoNum\":0,\"intensity\":4294967346}", "{\"piezoNum\":4294967296}",
    };
    for (auto json : invalid) {
        reset();
        int result = post(json);
        if (result >= 0) std::fprintf(stderr, "Unexpectedly accepted: %s\n", json);
        assert(result < 0);
        assert(events.empty() && device.saves == 0);
        assert(device.settings.piezos.active == -1);
        assert(device.settings.piezos.piezos[0].intensity == 50);
    }

    reset();
    device.settings.activeRoutine = SCHEDULING;
    device.settings.schedules[0].running = true;
    device.settings.schedules[0].stopEpoch = 100;
    assert(post("{\"piezoNum\":0}") == 0);
    assert(device.settings.activeRoutine == MANUAL);
    assert(!device.settings.schedules[0].running && device.settings.schedules[0].stopEpoch == 0);

    for (int failure = 0; failure < 4; ++failure) {
        reset();
        if (failure == 0) device.hardwareReady = false;
        if (failure == 1) pwmFailure = -EINVAL;
        if (failure == 2) gpioFailure = -EIO;
        if (failure == 3) clockFailure = -ENODEV;
        assert(post("{\"piezoNum\":2,\"intensity\":70,\"frequencyHz\":1800000}") == -EIO);
        assert(device.settings.piezos.piezos[2].intensity == 50);
        assert(device.settings.piezos.piezos[2].frequencyHz == 1700000);
        assert(device.settings.piezos.active == -1 && selected == -1);
        assert(device.saves == 0);
    }
    // Failed shutdown must attempt all remaining operations and never enable a channel.
    for (int failure = 0; failure < 4; ++failure) {
        reset();
        if (failure < 3) offFailurePin = failure;
        else stopFailure = -EIO;
        assert(post("{\"piezoNum\":1}") == -EIO);
        assert(events == std::vector<std::string>({"gpio0=0", "gpio1=0", "gpio2=0", "pwmoff"}));
        assert(device.saves == 0 && selected == -1);
    }
    reset();
    device.saveResult = -ENOSPC;
    assert(post("{\"piezoNum\":0}") == -EIO);
    assert(selected == -1 && pulseCycles == 0);
    assert(device.settings.piezos.active == -1 && device.saves == 1);

    reset();
    assert(post("{\"piezoNum\":2,\"intensity\":100,\"frequencyHz\":1800000}") == 0);
    char response[512];
    assert(device.piezosStatus(response, sizeof(response)) == 0);
    assert(std::strstr(response, "\"active\":2") != nullptr);
    assert(std::strstr(response, "\"frequencyHz\":1800000") != nullptr);
    assert(std::strstr(response, "\"actualDutyBasisPoints\":10000") != nullptr);
    assert(device.piezosStatus(response, 4) < 0);

    std::puts("Piezo command, control, and serialization tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix="humidifier-control-test-") as directory:
    temporary = Path(directory)
    stubs = {
        "zephyr/sys/util.h": """
#pragma once
#define ARG_UNUSED(x) ((void)(x))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define SIZEOF_FIELD(t, f) sizeof(((t *)0)->f)
#define ROUND_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define BIT(x) (1UL << (x))
#define ZERO_OR_COMPILE_ERROR(cond) (0 * sizeof(char[(cond) ? 1 : -1]))
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define CONFIG_JSON_FLOAT_PRECISION 6
#define CONFIG_JSON_DOUBLE_PRECISION 15
""",
        "zephyr/sys/__assert.h": "#include <assert.h>\n#define __ASSERT_NO_MSG(x) assert(x)\n",
        "zephyr/sys/printk.h": "#include <stdio.h>\n#include <inttypes.h>\n#define snprintk snprintf\n",
        "zephyr/toolchain.h": "#pragma once\n#define __fallthrough __attribute__((fallthrough))\n",
    }
    for name, content in stubs.items():
        path = temporary / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    cpp = temporary / "test.cpp"
    cpp.write_text(harness + structs + hardware + methods + checks)
    common = ["-I", str(temporary), "-I", str(ZEPHYR / "include"),
              "-I", str(ROOT / "inc"), "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    json_object = temporary / "json.o"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", *common,
                    "-c", str(ZEPHYR / "lib/utils/json.c"), "-o", str(json_object)], check=True)
    binary = temporary / "test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=gnu++17", *common,
                    str(cpp), str(json_object), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
