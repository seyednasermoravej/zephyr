#include "humidifier.h"


K_THREAD_STACK_DEFINE(schedulerStackArea, SCHEDULER_STACK_SIZE);
struct k_thread schedulerThread;


LOG_MODULE_REGISTER(humidifier, LOG_LEVEL_INF);

#define LED_PWM_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds)

static Humidifier *instance = nullptr;

static const struct device *pwmsDev = DEVICE_DT_GET(LED_PWM_NODE_ID);

static const struct device *const buttons = DEVICE_DT_GET(DT_PATH(buttons));

#define DT_SPEC_AND_COMMA_GATE(node_id, prop, idx) \
 	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),
static const struct gpio_dt_spec selectors[] = {
    DT_FOREACH_PROP_ELEM(DT_PATH(piezo_selector), gpios, DT_SPEC_AND_COMMA_GATE)
};


#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
};

Humidifier::Humidifier()
{
    LOG_INF("Humidifier constructor");

    int ret = 0;
    currentTime = 0;
    timeSynced = false;

    readInfosFromMemory();

    syncTimeSntp();

    hardwareInit();

    led_set_brightness(pwmsDev, FAN, settings.fanSpeed);

    if (safetyCheck())
    {
	return;
    }

    instance = this;

    INPUT_CALLBACK_DEFINE(buttons, buttonsHandlerWrapper, (void *)this);

    k_thread_create(
        &scheduleThread,
        schedulerStackArea,
        K_THREAD_STACK_SIZEOF(schedulerStackArea),
        schedulerThreadWrapper,
        this,
        NULL,
        NULL,
        5,
        0,
        K_NO_WAIT
    );

    LOG_INF("Scheduler thread started");
}

void Humidifier::setDefaultSettings()
{
    memset(&settings, 0, sizeof(settings));

    settings.fanSpeed = 30;

    settings.piezos.active = -1;
    settings.piezos.piezosNum = NUM_OF_PIEZOS;

    settings.piezos.piezos[0].brightness = 20;
    settings.piezos.piezos[0].intensity = 30;

    settings.piezos.piezos[1].brightness = 40;
    settings.piezos.piezos[1].intensity = 50;

    settings.piezos.piezos[2].brightness = 60;
    settings.piezos.piezos[2].intensity = 70;

    strcpy(settings.credentials.ssid, "Humidifier");
    strcpy(settings.credentials.password, "12345678");

    settings.nextScheduleId = 1;

    settings.schedulesNum = 1;

    // Example 1
    settings.schedules[0] = {
        .id = 1,
        .enabled = true,
        .days = 0x7F,
        .hour = 0,
        .minute = 0,
        .durationMinutes = 600,
        .oneTime = false,
        .activePiezo = 1,
        .intensity = 60,
        .fanLevel = 100,
        .brightness = 5,
        .lastRunEpochMinute = 0,
        .running = false,
        .stopEpoch = 0,
    };

    // Example 2
    settings.schedules[1] = {
        .id = 2,
        .enabled = true,
        .days = (1 << 3),
        .hour = 14,
        .minute = 30,
        .durationMinutes = 10,
        .oneTime = true,
        .activePiezo = 0,
        .intensity = 40,
        .fanLevel = 30,
        .brightness = 20,
        .lastRunEpochMinute = 0,
        .running = false,
        .stopEpoch = 0,
    };

    // Example 3
    settings.schedules[2] = {
        .id = 3,
        .enabled = true,
        .days = (1 << 1) |
                (1 << 2) |
                (1 << 3) |
                (1 << 4) |
                (1 << 5),
        .hour = 9,
        .minute = 15,
        .durationMinutes = 15,
        .oneTime = false,
        .activePiezo = 2,
        .intensity = 80,
        .fanLevel = 70,
        .brightness = 80,
        .lastRunEpochMinute = 0,
        .running = false,
        .stopEpoch = 0,
    };

    // Example 4
    settings.schedules[3] = {
        .id = 4,
        .enabled = true,
        .days = (1 << 0) | (1 << 6),
        .hour = 22,
        .minute = 45,
        .durationMinutes = 5,
        .oneTime = false,
        .activePiezo = 0,
        .intensity = 20,
        .fanLevel = 10,
        .brightness = 10,
        .lastRunEpochMinute = 0,
        .running = false,
        .stopEpoch = 0,
    };

    // Example 5
    settings.schedules[4] = {
        .id = 5,
        .enabled = true,
        .days = 0x7F,
        .hour = 23,
        .minute = 59,
        .durationMinutes = 2,
        .oneTime = false,
        .activePiezo = 1,
        .intensity = 100,
        .fanLevel = 100,
        .brightness = 100,
        .lastRunEpochMinute = 0,
        .running = false,
        .stopEpoch = 0,
    };

    // Example 6
    settings.schedules[5] = {
        .id = 6,
        .enabled = false,
        .days = 0x7F,
        .hour = 12,
        .minute = 0,
        .durationMinutes = 30,
        .oneTime = false,
        .activePiezo = 2,
        .intensity = 90,
        .fanLevel = 90,
        .brightness = 90,
        .lastRunEpochMinute = 0,
        .running = false,
        .stopEpoch = 0,
    };

    writeSettings();
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

    settings.piezos.active = s->activePiezo;

    settings.fanSpeed = s->fanLevel;

    setPiezoPwm(
        s->activePiezo,
        s->brightness,
        s->brightness,
        s->brightness,
        s->intensity
    );

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

    led_set_brightness(pwmsDev, PIEZO, 0);

    s->running = false;

    s->stopEpoch = 0;

    writeSettings();
}

void Humidifier::setPiezoPwm(
    uint8_t piezoNum,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t intensity)
{
    int redCh;
    int greenCh;
    int blueCh;

    switch (piezoNum)
    {
        case 0:
            redCh = RED0;
            greenCh = GREEN0;
            blueCh = BLUE0;
            break;

        case 1:
            redCh = RED1;
            greenCh = GREEN1;
            blueCh = BLUE1;
            break;

        case 2:
            redCh = RED2;
            greenCh = GREEN2;
            blueCh = BLUE2;
            break;

        default:
            LOG_ERR("Invalid piezo number");
            return;
    }

    led_set_brightness(pwmsDev, PIEZO, intensity);

    led_set_brightness(pwmsDev, redCh, red);

    led_set_brightness(pwmsDev, greenCh, green);

    led_set_brightness(pwmsDev, blueCh, blue);
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

    fs = new nvs_fs;

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
        LOG_ERR("nvs_mount failed");
        return rc;
    }

    return 0;
}

int Humidifier::readInfosFromMemory()
{
    nvsInit();

//     int rc = nvs_read(
//         fs,
//         NVS_SETTINGS_ID,
//         &settings,
//         sizeof(settings)
//     );

//     if (rc <= 0)
//     {
//         LOG_INF("No saved settings found");

        setDefaultSettings();
//     }

//     return rc;
    return 0;
}

int Humidifier::writeSettings()
{
    return nvs_write(
        fs,
        NVS_SETTINGS_ID,
        &settings,
        sizeof(settings)
    );
}

void Humidifier::parsePiezosPost(char *buf, size_t len)
{
    struct PiezoStatusUpdate cmd = {0};

    int ret = json_obj_parse(
        buf,
        len,
        piezoDescrUpdate,
        ARRAY_SIZE(piezoDescrUpdate),
        &cmd
    );

    if (ret < 0) {
        LOG_ERR("Piezo parse failed");
        return;
    }

    if (cmd.piezoNum >= NUM_OF_PIEZOS) {
        return;
    }

    settings.piezos.piezos[cmd.piezoNum].brightness = cmd.brightness;

    settings.piezos.piezos[cmd.piezoNum].intensity = cmd.intensity;

    setPiezoPwm(
        cmd.piezoNum,
        cmd.brightness,
        cmd.brightness,
        cmd.brightness,
        cmd.intensity
    );

    writeSettings();
}

void Humidifier::parseFanPost(char *buf, size_t len)
{
    struct FanStatus cmd;

    int ret = json_obj_parse(
        buf,
        len,
        fanDescr,
        ARRAY_SIZE(fanDescr),
        &cmd
    );

    if (ret < 0) {
        LOG_ERR("Fan parse failed");
        return;
    }

    settings.fanSpeed = cmd.speed;

    led_set_brightness(pwmsDev, FAN, cmd.speed);

    writeSettings();
}

void Humidifier::parseCredentialsPost(char *buf, size_t len)
{
    struct Credentials cmd = {0};

    int ret = json_obj_parse(
        buf,
        len,
        credentialsDescr,
        ARRAY_SIZE(credentialsDescr),
        &cmd
    );

    if (ret < 0) {
        LOG_ERR("Credentials parse failed");
        return;
    }

    setCredentials(cmd.ssid, cmd.password);

    writeSettings();
}

int Humidifier::fanStatus(char *buf, size_t bufSize)
{
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
    strncpy(
        ssid,
        settings.credentials.ssid,
        strlen(settings.credentials.ssid)
    );

    strncpy(
        psk,
        settings.credentials.password,
        strlen(settings.credentials.password)
    );
}

bool Humidifier::safetyCheck()
{
	bool lowPizos[NUM_OF_PIEZOS];
	bool fault = false;

	for (uint8_t i = 0; i < NUM_OF_PIEZOS; i++)
	{
		lowPizos[i] = checkLowPiezo(i);
	}

	if(lowPizos[0])
	{
		led_set_brightness(pwmsDev, RED0, settings.piezos.piezos[0].brightness);
		fault = true;
	}

	if(lowPizos[1])
	{
		led_set_brightness(pwmsDev, RED0, settings.piezos.piezos[1].brightness);
		fault = true;
	}

	if(lowPizos[2])
	{
		led_set_brightness(pwmsDev, RED0, settings.piezos.piezos[2].brightness);
		fault = true;
	}

	k_sleep(K_SECONDS(60));
	return fault;
}

bool Humidifier::checkLowPiezo(uint8_t i)
{
	int ret = gpio_pin_set_dt(&selectors[i], GPIO_OUTPUT_ACTIVE);
	uint16_t value = readAdc(0);

	if(ret)
	{
		LOG_ERR("failed to turn the led on");
	}
	if (value < LOW_LEVEL_THRESHOLD)
	{
		return true;
	}
	else
	{
		return false;
	}
}

uint16_t Humidifier::readAdc(uint8_t index)
{
	uint16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
		// .calibrate = true,
		// .options = adc_sequence_options
	};

	// int32_t val_mv;

	// LOG_INF("- %s, channel %d: ",
	// 		adc_channels[index].dev->name,
	// 		adc_channels[index].channel_id);

	(void)adc_sequence_init_dt(&adc_channels[index], &sequence);

	int err = adc_read_dt(&adc_channels[index], &sequence);
	if (err < 0) {
		LOG_INF("Could not read (%d)\n", err);
	}
	return buf>>2;

	// val_mv = (int32_t)buf;

	// // LOG_INF("%"PRId32, val_mv);
	// // err = adc_raw_to_millivolts_dt(&adc_channels[index],
	// // 					&val_mv);
	/* conversion to mV may not be supported, skip if not */
		// if (err < 0) {
		// 	printk(" (value in mV not available)\n");
		// } else {
		// 	printk(" = %"PRId32" mV\n", val_mv);
		// }
	// return val_mv;

}

int Humidifier::hardwareInit()
{

	int ret = 0;

	if (!device_is_ready(pwmsDev)) {
		LOG_ERR("PWM device not ready");
		return -1;
	}
	if (!adc_is_ready_dt(&adc_channels[0])) {
		LOG_ERR("ADC controller device %s not ready\n", adc_channels[0].dev->name);
		return -1;
	}

	ret = adc_channel_setup_dt(&adc_channels[0]);
	if (ret < 0) {
		LOG_ERR("Could not setup channel #%d (%d)\n", 0, ret);
		return ret;
	}

	for (int i = 0; i < NUM_OF_PIEZOS; i++) {

		if (!gpio_is_ready_dt(&selectors[i])) {
		LOG_ERR("GPIO selector %d not ready", i);
		return -1;
		}

		ret = gpio_pin_configure_dt(
		&selectors[i],
		GPIO_OUTPUT_INACTIVE
		);

		if (ret < 0) {
		LOG_ERR("GPIO selector %d configure failed (%d)", i, ret);
		return ret;
		}
	}
}
