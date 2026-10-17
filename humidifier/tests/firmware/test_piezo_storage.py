#!/usr/bin/env python3
"""Test production settings migration, persistence, and reboot behavior on a host.

Run: python3 tests/firmware/test_piezo_storage.py
NVS, hardware initialization, and thread startup are doubled; settings layouts,
validation, loading, saving, defaults, and the constructor come from production.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/humidifier.cpp").read_text()
header = (ROOT / "inc/humidifier.h").read_text()
structs = header[header.index("#define NUM_OF_PIEZOS"):header.index("static const struct json_obj_descr credentialsDescr")]
storage_types = source[source.index("struct LegacyPiezoStatus"):source.index("LOG_MODULE_REGISTER")]
constructor = source[source.index("Humidifier::Humidifier()"):source.index("void Humidifier::schedulerThreadWrapper")]
storage_methods = source[source.index("int Humidifier::readInfosFromMemory()"):source.index("int Humidifier::parsePiezosPost")]

harness = r'''
#include "piezo_pwm.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define K_THREAD_STACK_SIZEOF(x) sizeof(x)
#define K_NO_WAIT 0
'''

doubles = r'''
struct nvs_fs {};
static nvs_fs fakeFs;
static std::map<uint16_t, std::vector<uint8_t>> records;
static int reads, writes, stops, threads, initializationResult, writeResult, hardwareResult;
static int fanLevel = -1;
static char schedulerStackArea[16];
static const void *pwmsDev = nullptr;
class Humidifier;
static Humidifier *instance;

static int nvs_read(nvs_fs *fs, uint16_t id, void *output, size_t length) {
    assert(fs == &fakeFs);
    ++reads;
    auto found = records.find(id);
    if (found == records.end()) return -ENOENT;
    std::memcpy(output, found->second.data(), std::min(length, found->second.size()));
    // Zephyr returns the stored record length even if the buffer is smaller.
    return static_cast<int>(found->second.size());
}

static int nvs_write(nvs_fs *fs, uint16_t id, const void *value, size_t length) {
    assert(fs == &fakeFs);
    ++writes;
    if (writeResult < 0) return writeResult;
    const auto *bytes = static_cast<const uint8_t *>(value);
    records[id] = std::vector<uint8_t>(bytes, bytes + length);
    return static_cast<int>(length);
}

static int led_set_brightness(const void *device, int index, int level) {
    assert(device == pwmsDev && index == FAN);
    fanLevel = level;
    return 0;
}

static int k_thread_create(int *, char *, size_t, void (*)(void *, void *, void *),
                           void *, void *, void *, int, int, int) {
    ++threads;
    return 0;
}

class Humidifier {
public:
    Settings settings;
    nvs_fs *fs;
    bool storageReady;
    bool hardwareReady;
    uint32_t currentTime;
    bool timeSynced;
    int scheduleThread;
    Humidifier();
    void setDefaultSettings();
    int readInfosFromMemory();
    int writeSettings();
    int nvsInit() { fs = &fakeFs; return initializationResult; }
    int hardwareInit() {
        // Boot must clear persisted output state before hardware is touched.
        assert(settings.activeRoutine == MANUAL);
        assert(settings.piezos.active == -1);
        assert(settings.piezos.actualFrequencyHz == 0);
        assert(settings.piezos.actualDutyBasisPoints == 0);
        for (const auto &schedule : settings.schedules) {
            assert(!schedule.running && schedule.stopEpoch == 0);
        }
        return hardwareResult;
    }
    int disablePiezos() { ++stops; return 0; }
    static void schedulerThreadWrapper(void *, void *, void *) {}
};
'''

checks = r'''
static void reset() {
    records.clear();
    reads = writes = stops = threads = 0;
    initializationResult = writeResult = hardwareResult = 0;
    fanLevel = -1;
    instance = nullptr;
}

template <typename T>
static void seed(uint16_t id, const T &value) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    records[id] = std::vector<uint8_t>(bytes, bytes + sizeof(value));
}

static LegacySettings legacyRecord() {
    LegacySettings legacy{};
    legacy.activeRoutine = SCHEDULING;
    legacy.fanSpeed = 42;
    legacy.piezos.active = 2;
    legacy.piezos.piezosNum = NUM_OF_PIEZOS;
    for (int i = 0; i < NUM_OF_PIEZOS; ++i) {
        legacy.piezos.piezos[i].brightness = 13 + i;
        legacy.piezos.piezos[i].intensity = 63 + i;
    }
    std::strcpy(legacy.credentials.ssid, "Saved network");
    std::strcpy(legacy.credentials.password, "Saved password");
    legacy.nextScheduleId = 25;
    legacy.schedulesNum = 1;
    legacy.schedules[0].id = 24;
    legacy.schedules[0].enabled = true;
    legacy.schedules[0].activePiezo = 1;
    legacy.schedules[0].running = true;
    legacy.schedules[0].stopEpoch = 999999;
    return legacy;
}

static void expectBootOff(const Humidifier &device) {
    assert(device.settings.activeRoutine == MANUAL);
    assert(device.settings.piezos.active == -1);
    assert(device.settings.piezos.actualFrequencyHz == 0);
    assert(device.settings.piezos.actualDutyBasisPoints == 0);
    for (const auto &schedule : device.settings.schedules) {
        assert(!schedule.running && schedule.stopEpoch == 0);
    }
    assert(instance == &device);
}

static void checkFreshStorage() {
    reset();
    Humidifier device;
    assert(device.storageReady && device.hardwareReady);
    assert(records.count(NVS_SETTINGS_V2_ID) == 1 && writes == 1);
    assert(stops == 1 && threads == 1 && fanLevel == 30);
    assert(device.settings.schedulesNum == 0);
    assert(device.settings.piezos.piezosNum == NUM_OF_PIEZOS);
    for (const auto &piezo : device.settings.piezos.piezos) {
        assert(piezo.frequencyHz == PIEZO_FREQUENCY_DEFAULT_HZ);
        assert(piezo.intensity == PIEZO_DUTY_DEFAULT_PERCENT);
    }
    expectBootOff(device);
}

static void checkLegacyMigration() {
    reset();
    LegacySettings legacy = legacyRecord();
    // Old byte-sized input could contain values above the new duty limit.
    legacy.fanSpeed = 255;
    legacy.piezos.piezos[0].intensity = 255;
    legacy.piezos.piezos[0].brightness = 255;
    seed(NVS_SETTINGS_ID, legacy);
    const auto legacyBytes = records[NVS_SETTINGS_ID];
    Humidifier device;
    assert(writes == 1 && records[NVS_SETTINGS_ID] == legacyBytes);
    assert(std::strcmp(device.settings.credentials.ssid, legacy.credentials.ssid) == 0);
    assert(std::strcmp(device.settings.credentials.password, legacy.credentials.password) == 0);
    assert(device.settings.fanSpeed == 100);
    assert(device.settings.piezos.piezos[0].intensity == 100);
    assert(device.settings.piezos.piezos[0].brightness == 100);
    assert(device.settings.piezos.piezos[1].intensity == 64);
    assert(device.settings.piezos.piezos[1].brightness == 14);
    assert(device.settings.nextScheduleId == 25 && device.settings.schedulesNum == 1);
    assert(device.settings.schedules[0].id == 24 && device.settings.schedules[0].enabled);
    for (const auto &piezo : device.settings.piezos.piezos) {
        assert(piezo.frequencyHz == PIEZO_FREQUENCY_DEFAULT_HZ);
    }
    expectBootOff(device);

    StoredSettings saved{};
    assert(nvs_read(device.fs, NVS_SETTINGS_V2_ID, &saved, sizeof(saved)) == sizeof(saved));
    assert(saved.magic == SETTINGS_MAGIC && saved.version == 2 && validSettings(saved.value));

    reset();
    legacy = legacyRecord();
    std::memset(legacy.credentials.password, 'x', sizeof(legacy.credentials.password));
    seed(NVS_SETTINGS_ID, legacy);
    Humidifier invalidCredentials;
    assert(std::strcmp(invalidCredentials.settings.credentials.ssid, "Humidifier") == 0);
    expectBootOff(invalidCredentials);
}

static void checkV2Reload() {
    reset();
    seed(NVS_SETTINGS_ID, legacyRecord());
    Humidifier first;
    first.settings.piezos.piezos[0].frequencyHz = 1600000;
    first.settings.piezos.piezos[1].frequencyHz = 1732000;
    first.settings.piezos.piezos[2].frequencyHz = 1800000;
    first.settings.piezos.piezos[2].intensity = 100;
    first.settings.piezos.active = 2;
    first.settings.piezos.actualFrequencyHz = 1777778;
    first.settings.piezos.actualDutyBasisPoints = 10000;
    first.settings.activeRoutine = SCHEDULING;
    first.settings.schedules[0].running = true;
    first.settings.schedules[0].stopEpoch = 888888;
    std::strcpy(first.settings.credentials.ssid, "New network");
    assert(first.writeSettings() == 0);
    const auto savedBytes = records[NVS_SETTINGS_V2_ID];
    reads = writes = stops = threads = 0;
    Humidifier rebooted;
    assert(reads == 1 && writes == 0);
    assert(stops == 1 && threads == 1);
    assert(records[NVS_SETTINGS_V2_ID] == savedBytes);
    assert(std::strcmp(rebooted.settings.credentials.ssid, "New network") == 0);
    assert(rebooted.settings.piezos.piezos[0].frequencyHz == 1600000);
    assert(rebooted.settings.piezos.piezos[1].frequencyHz == 1732000);
    assert(rebooted.settings.piezos.piezos[2].frequencyHz == 1800000);
    assert(rebooted.settings.piezos.piezos[2].intensity == 100);
    expectBootOff(rebooted);
}

static void checkInvalidRecordsAndFailures() {
    reset();
    Humidifier baseline;
    StoredSettings valid{SETTINGS_MAGIC, 2, baseline.settings};
    for (int invalid = 0; invalid < 6; ++invalid) {
        reset();
        auto bad = valid;
        if (invalid == 0) bad.magic = 0;
        if (invalid == 1) bad.version = 99;
        if (invalid == 2) bad.value.piezos.piezos[0].frequencyHz = 0;
        if (invalid == 3) bad.value.piezos.piezosNum = 99;
        seed(NVS_SETTINGS_V2_ID, bad);
        if (invalid == 4) records[NVS_SETTINGS_V2_ID].pop_back();
        if (invalid == 5) records[NVS_SETTINGS_V2_ID].push_back(0);
        seed(NVS_SETTINGS_ID, legacyRecord());
        Humidifier device;
        assert(writes == 1);
        assert(std::strcmp(device.settings.credentials.ssid, "Saved network") == 0);
        assert(device.settings.piezos.piezos[0].frequencyHz == PIEZO_FREQUENCY_DEFAULT_HZ);
        expectBootOff(device);
    }

    reset();
    initializationResult = -EIO;
    Humidifier unavailable;
    assert(!unavailable.storageReady && reads == 0 && writes == 0);
    assert(unavailable.writeSettings() == -EIO);
    expectBootOff(unavailable);

    reset();
    Humidifier writeFailure;
    writeResult = -ENOSPC;
    assert(writeFailure.writeSettings() == -ENOSPC);

    reset();
    hardwareResult = -ENODEV;
    Humidifier hardwareFailure;
    assert(!hardwareFailure.hardwareReady && stops == 0 && fanLevel == -1);
    expectBootOff(hardwareFailure);
}

int main() {
    checkFreshStorage();
    checkLegacyMigration();
    checkV2Reload();
    checkInvalidRecordsAndFailures();
    std::puts("Settings tests passed (legacy migration, v2 reload, reboot off, storage failures).");
}
'''

with tempfile.TemporaryDirectory(prefix="humidifier-storage-test-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(harness + structs + storage_types + doubles + constructor + storage_methods + checks)
    subprocess.run(
        [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-I", str(ROOT / "inc"), str(cpp), "-o", str(binary)], check=True,
    )
    subprocess.run([str(binary)], check=True)
