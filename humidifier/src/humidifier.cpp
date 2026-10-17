#include "humidifier.h"
#include "piezo_pwm.h"
#include <errno.h>


K_THREAD_STACK_DEFINE(schedulerStackArea, SCHEDULER_STACK_SIZE);
// HTTP handlers and the scheduler share settings and the selector GPIOs.
K_MUTEX_DEFINE(settingsMutex);

class SettingsLock {
public:
    SettingsLock() { k_mutex_lock(&settingsMutex, K_FOREVER); }
    ~SettingsLock() { k_mutex_unlock(&settingsMutex); }
};

// Keep the original layout solely for migrating ID 0 without losing credentials.
struct LegacyPiezoStatus { uint8_t brightness; uint8_t intensity; };
struct LegacyPiezosStatus {
    int8_t active;
    LegacyPiezoStatus piezos[NUM_OF_PIEZOS];
    size_t piezosNum;
};
struct LegacySettings {
    Routines activeRoutine;
    uint8_t fanSpeed;
    LegacyPiezosStatus piezos;
    Credentials credentials;
    uint32_t nextScheduleId;
    uint8_t schedulesNum;
    Schedule schedules[MAX_SCHEDULES];
};
struct StoredSettings {
    uint32_t magic;
    uint32_t version;
    Settings value;
};
static constexpr uint32_t SETTINGS_MAGIC = 0x48554d32;

static bool validSettings(const Settings &value)
{
    if (value.fanSpeed > 100 || value.piezos.piezosNum != NUM_OF_PIEZOS ||
        value.schedulesNum > MAX_SCHEDULES ||
        !memchr(value.credentials.ssid, '\0', sizeof(value.credentials.ssid)) ||
        !memchr(value.credentials.password, '\0', sizeof(value.credentials.password))) {
        return false;
    }
    for (const auto &piezo : value.piezos.piezos) {
        if (piezo.brightness > 100 || piezo.intensity > 100 ||
            piezo.frequencyHz < PIEZO_FREQUENCY_MIN_HZ ||
            piezo.frequencyHz > PIEZO_FREQUENCY_MAX_HZ) {
            return false;
        }
    }
    return true;
}


LOG_MODULE_REGISTER(humidifier, LOG_LEVEL_INF);

#define LED_PWM_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds)

static Humidifier *instance = nullptr;

static const struct device *pwmsDev = DEVICE_DT_GET(LED_PWM_NODE_ID);
#if DT_NODE_HAS_STATUS(DT_ALIAS(piezo_pwm), okay)
static const struct pwm_dt_spec piezoPwm = PWM_DT_SPEC_GET(DT_ALIAS(piezo_pwm));
#endif

static const struct device *const buttons = DEVICE_DT_GET(DT_PATH(buttons));

#define DT_SPEC_AND_COMMA_GATE(node_id, prop, idx) \
 	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),
static const struct gpio_dt_spec selectors[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(piezo_selector), gpios, DT_SPEC_AND_COMMA_GATE)
};


Humidifier::Humidifier()
    : fs(nullptr), storageReady(false), hardwareReady(false),
      currentTime(0), timeSynced(false)
{
    LOG_INF("Humidifier constructor");
    readInfosFromMemory();

    // A reboot never energizes a saved selection automatically.
    settings.activeRoutine = MANUAL;
    settings.piezos.active = -1;
    settings.piezos.actualFrequencyHz = 0;
    settings.piezos.actualDutyBasisPoints = 0;
    for (auto &schedule : settings.schedules) {
        schedule.running = false;
        schedule.stopEpoch = 0;
    }

    int ret = hardwareInit();
    if (ret == 0) {
        hardwareReady = true;
        ret = disablePiezos();
        if (ret == 0) {
            led_set_brightness(pwmsDev, FAN, settings.fanSpeed);
        } else {
            hardwareReady = false;
        }
    }
    if (ret < 0) {
        LOG_ERR("Hardware initialization failed (%d)", ret);
    }

    instance = this;
    k_thread_create(&scheduleThread, schedulerStackArea,
                    K_THREAD_STACK_SIZEOF(schedulerStackArea),
                    schedulerThreadWrapper, this, NULL, NULL, 5, 0, K_NO_WAIT);
}

void Humidifier::setDefaultSettings()
{
    settings = Settings{};
    settings.fanSpeed = 30;
    settings.piezos.active = -1;
    settings.piezos.piezosNum = NUM_OF_PIEZOS;
    for (int i = 0; i < NUM_OF_PIEZOS; ++i) {
        settings.piezos.piezos[i].brightness = 20 * (i + 1);
        settings.piezos.piezos[i].intensity = PIEZO_DUTY_DEFAULT_PERCENT;
        settings.piezos.piezos[i].frequencyHz = PIEZO_FREQUENCY_DEFAULT_HZ;
    }
    strcpy(settings.credentials.ssid, "Humidifier");
    strcpy(settings.credentials.password, "12345678");
    settings.nextScheduleId = 1;
    settings.schedulesNum = 0;
}

void Humidifier::schedulerThreadWrapper(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    Humidifier *self = (Humidifier *)arg1;

    self->scheduleLoop();
}

void Humidifier::scheduleLoop()
{
    while (1)
    {
        checkSchedules();

        k_sleep(K_SECONDS(30));
    }
}

void Humidifier::checkSchedules()
{
    SettingsLock lock;
    if (settings.activeRoutine != SCHEDULING || !hardwareReady) {
        return;
    }
#ifdef CONFIG_BOARD_ESP32_DEVKITC
    time_t now = time(NULL);
#else
	// time_t now = 1779265194;
    time_t now = time(NULL);
#endif
    struct tm *utc = gmtime(&now);

    if (!utc) {
        return;
    }

    uint32_t currentEpochMinute = now / 60;

    for (int i = 0; i < settings.schedulesNum; i++)
    {
        struct Schedule *s = &settings.schedules[i];

        // Stop active schedule
        if (s->running && now >= s->stopEpoch)
        {
            stopSchedule(s);
        }

        if (!s->enabled) {
            continue;
        }

        if (!(s->days & (1 << utc->tm_wday))) {
            continue;
        }

        if (s->hour != utc->tm_hour) {
            continue;
        }

        if (s->minute != utc->tm_min) {
            continue;
        }

        if (s->lastRunEpochMinute == currentEpochMinute) {
            continue;
        }

        s->lastRunEpochMinute = currentEpochMinute;

        runSchedule(s);

        if (s->oneTime)
        {
            s->enabled = false;
        }
    }
}

void Humidifier::runSchedule(struct Schedule *s)
{
    LOG_INF(
        "Run schedule id=%d piezo=%d fan=%d intensity=%d",
        s->id,
        s->activePiezo,
        s->fanLevel,
        s->intensity
    );

    if (s->activePiezo >= NUM_OF_PIEZOS || s->intensity > 100 ||
        s->brightness > 100 || s->fanLevel > 100) {
        return;
    }
    settings.piezos.piezos[s->activePiezo].intensity = s->intensity;
    settings.piezos.piezos[s->activePiezo].brightness = s->brightness;
    int ret = setPiezoPwm(s->activePiezo);
    if (ret < 0) {
        LOG_ERR("Schedule PWM failed (%d)", ret);
        return;
    }
    settings.fanSpeed = s->fanLevel;

    led_set_brightness(
        pwmsDev,
        FAN,
        s->fanLevel
    );

    s->running = true;

    s->stopEpoch = time(NULL) + (s->durationMinutes * 60);

    writeSettings();
}

void Humidifier::stopSchedule(struct Schedule *s)
{
    LOG_INF("Stop schedule id=%d", s->id);

    led_set_brightness(pwmsDev, FAN, 0);

    setPiezoPwm(-1);
    settings.fanSpeed = 0;

    s->running = false;

    s->stopEpoch = 0;

    writeSettings();
}

int Humidifier::disablePiezos()
{
    // Break before make: disconnect every piezo before changing the shared PWM.
    int result = 0;
    for (const auto &selector : selectors) {
        int ret = gpio_pin_set_dt(&selector, 0);
        if (ret < 0 && result == 0) {
            result = ret;
        }
    }
#if DT_NODE_HAS_STATUS(DT_ALIAS(piezo_pwm), okay)
    int ret = pwm_set_pulse_dt(&piezoPwm, 0);
    if (ret < 0 && result == 0) {
        result = ret;
    }
#else
    result = -ENOTSUP;
#endif
    settings.piezos.active = -1;
    settings.piezos.actualFrequencyHz = 0;
    settings.piezos.actualDutyBasisPoints = 0;
    return result;
}

int Humidifier::setPiezoPwm(int8_t piezoNum)
{
    if (piezoNum < -1 || piezoNum >= NUM_OF_PIEZOS) {
        return -EINVAL;
    }
    if (!hardwareReady) {
        return -ENODEV;
    }
    int ret = disablePiezos();
    if (ret < 0 || piezoNum == -1) {
        return ret;
    }
#if DT_NODE_HAS_STATUS(DT_ALIAS(piezo_pwm), okay) && defined(CONFIG_PWM_LED_ESP32)
    const auto &piezo = settings.piezos.piezos[piezoNum];
    uint64_t clockHz;
    ret = pwm_get_cycles_per_sec(piezoPwm.dev, piezoPwm.channel, &clockHz);
    if (ret < 0) {
        return ret;
    }
    PiezoPwmTiming timing;
    if (!calculatePiezoPwm(clockHz, piezo.frequencyHz, piezo.intensity, timing)) {
        return -ENOTSUP;
    }
    ret = pwm_set_cycles(piezoPwm.dev, piezoPwm.channel,
                         timing.periodCycles, timing.pulseCycles, piezoPwm.flags);
    if (ret == 0) {
        ret = gpio_pin_set_dt(&selectors[piezoNum], 1);
    }
    if (ret < 0) {
        disablePiezos();
        return ret;
    }
    settings.piezos.active = piezoNum;
    settings.piezos.actualFrequencyHz = timing.actualFrequencyHz;
    settings.piezos.actualDutyBasisPoints = timing.actualDutyBasisPoints;
    return 0;
#else
    return -ENOTSUP;
#endif
}

void Humidifier::buttonsHandlerWrapper(struct input_event *val, void *userData)
{
    ARG_UNUSED(userData);

    instance->buttonsHandler(val);
}

void Humidifier::buttonsHandler(struct input_event *val)
{
    LOG_INF(
        "button type=%d code=%d value=%d",
        val->type,
        val->code,
        val->value
    );
}

int Humidifier::nvsInit()
{
    int rc;

    struct flash_pages_info info;

    fs = new nvs_fs{};

    fs->flash_device = NVS_PARTITION_DEVICE;

    if (!device_is_ready(fs->flash_device)) {
        LOG_ERR("Flash device not ready");
        return -ENODEV;
    }

    fs->offset = NVS_PARTITION_OFFSET;

    rc = flash_get_page_info_by_offs(
        fs->flash_device,
        fs->offset,
        &info
    );

    if (rc) {
        LOG_ERR("flash_get_page_info_by_offs failed");
        return rc;
    }

    fs->sector_size = info.size;

    fs->sector_count = 2;

    rc = nvs_mount(fs);

    if (rc) {
        LOG_ERR("nvs_mount failed (%d): offset=0x%lx, sector_size=%u, sector_count=%u",
                rc, static_cast<unsigned long>(fs->offset),
                static_cast<unsigned int>(fs->sector_size),
                static_cast<unsigned int>(fs->sector_count));
        return rc;
    }

    return 0;
}

int Humidifier::readInfosFromMemory()
{
    setDefaultSettings();
    int ret = nvsInit();
    if (ret < 0) {
        LOG_ERR("Settings storage unavailable (%d)", ret);
        return ret;
    }
    storageReady = true;
    StoredSettings saved{};
    ret = nvs_read(fs, NVS_SETTINGS_V2_ID, &saved, sizeof(saved));
    if (ret == sizeof(saved) && saved.magic == SETTINGS_MAGIC &&
        saved.version == 2 && validSettings(saved.value)) {
        settings = saved.value;
        return 0;
    }

    LegacySettings legacy{};
    ret = nvs_read(fs, NVS_SETTINGS_ID, &legacy, sizeof(legacy));
    if (ret == sizeof(legacy) && legacy.piezos.piezosNum == NUM_OF_PIEZOS &&
        legacy.schedulesNum <= MAX_SCHEDULES) {
        settings.fanSpeed = MIN(legacy.fanSpeed, 100);
        if (legacy.credentials.ssid[0] != '\0' &&
            memchr(legacy.credentials.ssid, '\0', sizeof(legacy.credentials.ssid)) &&
            memchr(legacy.credentials.password, '\0', sizeof(legacy.credentials.password))) {
            settings.credentials = legacy.credentials;
        }
        settings.nextScheduleId = legacy.nextScheduleId;
        settings.schedulesNum = legacy.schedulesNum;
        memcpy(settings.schedules, legacy.schedules, sizeof(settings.schedules));
        for (int i = 0; i < NUM_OF_PIEZOS; ++i) {
            settings.piezos.piezos[i].brightness = MIN(legacy.piezos.piezos[i].brightness, 100);
            settings.piezos.piezos[i].intensity = MIN(legacy.piezos.piezos[i].intensity, 100);
        }
    }
    return writeSettings();
}

int Humidifier::writeSettings()
{
    if (!storageReady) {
        return -EIO;
    }
    StoredSettings saved{SETTINGS_MAGIC, 2, settings};
    int ret = nvs_write(fs, NVS_SETTINGS_V2_ID, &saved, sizeof(saved));
    return ret < 0 ? ret : 0;
}

int Humidifier::parsePiezosPost(char *buf, size_t len)
{
    PiezoStatusUpdate cmd{};
    int fields = parsePiezoCommand(buf, len, cmd);
    if (fields < 0 || !(fields & BIT(0))) {
        return -EINVAL;
    }
    if (cmd.piezoNum < -1 || cmd.piezoNum >= NUM_OF_PIEZOS ||
        ((fields & BIT(1)) && (cmd.brightness < 0 || cmd.brightness > 100)) ||
        ((fields & BIT(2)) && (cmd.intensity < 0 || cmd.intensity > 100)) ||
        ((fields & BIT(3)) && (cmd.frequencyHz < static_cast<int32_t>(PIEZO_FREQUENCY_MIN_HZ) ||
                               cmd.frequencyHz > static_cast<int32_t>(PIEZO_FREQUENCY_MAX_HZ)))) {
        return -ERANGE;
    }

    SettingsLock lock;
    PiezoStatus previous{};
    if (cmd.piezoNum >= 0) {
        auto &piezo = settings.piezos.piezos[cmd.piezoNum];
        previous = piezo;
        if (fields & BIT(1)) { piezo.brightness = cmd.brightness; }
        if (fields & BIT(2)) { piezo.intensity = cmd.intensity; }
        if (fields & BIT(3)) { piezo.frequencyHz = cmd.frequencyHz; }
    }
    int ret = setPiezoPwm(cmd.piezoNum);
    if (ret < 0) {
        if (cmd.piezoNum >= 0) {
            settings.piezos.piezos[cmd.piezoNum] = previous;
        }
        LOG_ERR("Piezo output failed (%d)", ret);
        // Validation errors above are 4xx; a driver failure is always 5xx.
        return -EIO;
    }
    settings.activeRoutine = MANUAL;
    for (auto &schedule : settings.schedules) {
        schedule.running = false;
        schedule.stopEpoch = 0;
    }
    ret = writeSettings();
    if (ret < 0) {
        // A failed save must not leave an unacknowledged output running.
        setPiezoPwm(-1);
        return -EIO;
    }
    return 0;
}

void Humidifier::parseFanPost(char *buf, size_t len)
{
    SettingsLock lock;
    struct FanStatus cmd{};

    int ret = json_obj_parse(
        buf,
        len,
        fanDescr,
        ARRAY_SIZE(fanDescr),
        &cmd
    );

    if (ret != BIT(0) || cmd.speed > 100) {
        LOG_ERR("Fan parse failed");
        return;
    }

    settings.fanSpeed = cmd.speed;

    led_set_brightness(pwmsDev, FAN, cmd.speed);

    writeSettings();
}

void Humidifier::parseCredentialsPost(char *buf, size_t len)
{
    SettingsLock lock;
    struct Credentials cmd = {0};

    int ret = json_obj_parse(
        buf,
        len,
        credentialsDescr,
        ARRAY_SIZE(credentialsDescr),
        &cmd
    );

    if (ret != (BIT(0) | BIT(1)) || cmd.ssid[0] == '\0') {
        LOG_ERR("Credentials parse failed");
        return;
    }

    setCredentials(cmd.ssid, cmd.password);

    writeSettings();
}

int Humidifier::fanStatus(char *buf, size_t bufSize)
{
    SettingsLock lock;
    struct FanStatus fan;

    fan.speed = settings.fanSpeed;

    return json_obj_encode_buf(
        fanDescr,
        ARRAY_SIZE(fanDescr),
        &fan,
        buf,
        bufSize
    );
}

int Humidifier::piezosStatus(char *buf, size_t bufSize)
{
    SettingsLock lock;
    return json_obj_encode_buf(
        piezosDescr,
        ARRAY_SIZE(piezosDescr),
        &settings.piezos,
        buf,
        bufSize
    );
}

int Humidifier::credentialStatus(char *buf, size_t bufSize)
{
    SettingsLock lock;
    return json_obj_encode_buf(
        credentialsDescr,
        ARRAY_SIZE(credentialsDescr),
        &settings.credentials,
        buf,
        bufSize
    );
}

int Humidifier::timeStatus(char *buf, size_t bufSize)
{
    if (!timeSynced) {
        currentTime = k_uptime_get() / 1000;
    }

    struct TimeStatus status = {
        .timestamp = currentTime,
        .synced = timeSynced,
    };

    return json_obj_encode_buf(
        timeDescr,
        ARRAY_SIZE(timeDescr),
        &status,
        buf,
        bufSize
    );
}

void Humidifier::syncTimeSntp()
{
    struct sntp_time sntpTime;

    int ret = sntp_simple(
        "pool.ntp.org",
        5000,
        &sntpTime
    );

    if (ret < 0)
    {
        LOG_ERR("SNTP failed %d", ret);
        return;
    }

    struct timespec ts;

    ts.tv_sec = sntpTime.seconds;

    ts.tv_nsec = ((uint64_t)sntpTime.fraction * NSEC_PER_SEC) >> 32;

    clock_settime(CLOCK_REALTIME, &ts);

    currentTime = ts.tv_sec;

    timeSynced = true;

    LOG_INF("Time synchronized");
}

void Humidifier::setCredentials(char *ssid, char *password)
{
    strncpy(
        settings.credentials.ssid,
        ssid,
        sizeof(settings.credentials.ssid) - 1
    );

    strncpy(
        settings.credentials.password,
        password,
        sizeof(settings.credentials.password) - 1
    );
}

void Humidifier::getCredentials(char *ssid, char *psk)
{
    SettingsLock lock;
    memcpy(ssid, settings.credentials.ssid, sizeof(settings.credentials.ssid));
    memcpy(psk, settings.credentials.password, sizeof(settings.credentials.password));
}

int Humidifier::hardwareInit()
{
    // Configure selectors off before touching any peripheral that could fail.
    BUILD_ASSERT(ARRAY_SIZE(selectors) == NUM_OF_PIEZOS);
    for (int i = 0; i < NUM_OF_PIEZOS; ++i) {
        if (!gpio_is_ready_dt(&selectors[i])) {
            return -ENODEV;
        }
        int ret = gpio_pin_configure_dt(&selectors[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            return ret;
        }
    }
#if DT_NODE_HAS_STATUS(DT_ALIAS(piezo_pwm), okay) && defined(CONFIG_PWM_LED_ESP32)
    if (!pwm_is_ready_dt(&piezoPwm) || !device_is_ready(pwmsDev)) {
        return -ENODEV;
    }
    return 0;
#else
    LOG_ERR("This board has no supported independent piezo PWM output");
    return -ENOTSUP;
#endif
}
