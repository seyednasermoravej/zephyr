#include "humidifier.h"


LOG_MODULE_REGISTER(humidifier, LOG_LEVEL_INF);


#define DT_SPEC_AND_COMMA_GATE(node_id, prop, idx) \
 	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),


//  //const struct gpio_dt_spec spec = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(leds), gpios, 1);
// static const struct gpio_dt_spec sw[] = {
//     DT_FOREACH_PROP_ELEM(DT_NODELABEL(buttons), gpios, DT_SPEC_AND_COMMA_GATE)
// };

// static Humidifier *instance = nullptr;
// static const struct device *const buttons = DEVICE_DT_GET(buttons);

#define LED_PWM_NODE_ID	 DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds)
extern Humidifier *humidifier;
static const struct device *pwmsDev = DEVICE_DT_GET(LED_PWM_NODE_ID);

Humidifier:: Humidifier()
{
	LOG_INF("enter consgtructure");
	readInfosFromMemory();
	bool ret = device_is_ready(pwmsDev);
	LOG_INF("ret = %d", ret);
	if (!ret) {
		LOG_INF("Device %s is not ready, ret = %d", pwmsDev->name, ret);
		return;
	}

	// settings.fanSpeed = 5;
	int err = led_set_brightness(pwmsDev, FAN, settings.fanSpeed);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, settings.fanSpeed);
		return;
	}
	// settings.piezos.piezos[0].intensity = 20;
	// err = led_set_brightness(pwmsDev, PIEZO, settings.piezos.piezos[0].intensity);
	// LOG_INF("err=%d \n", err);
	// if (err < 0) {
	// 	LOG_ERR("err=%d brightness=%d\n", err, settings.piezos.piezos[0].intensity);
	// 	return;
	// }
	// settings.piezos.piezos[0].led.red = 30;
	// err = led_set_brightness(pwmsDev, RED0, settings.piezos.piezos[0].led.red);
	// LOG_INF("err=%d \n", err);
	// if (err < 0) {
	// 	LOG_ERR("err=%d brightness=%d\n", err, settings.piezos.piezos[0].led.red);
	// 	return;
	// }
	// settings.piezos.piezos[0].led.green = 40;
	// err = led_set_brightness(pwmsDev, GREEN0, settings.piezos.piezos[0].led.green);
	// LOG_INF("err=%d \n", err);
	// if (err < 0) {
	// 	LOG_ERR("err=%d brightness=%d\n", err, settings.piezos.piezos[0].led.green);
	// 	return;
	// }

	// setup_tls();
#ifdef CONFIG_BOARD_ESP32_DEVKITC
	uint8_t pwmLevel = 50;
	err = led_set_brightness(pwmsDev, BLUE0, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return;
	}

	pwmLevel = 60;
	err = led_set_brightness(pwmsDev, RED1, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return;
	}
	pwmLevel = 70;
	err = led_set_brightness(pwmsDev, GREEN1, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return;
	}
	pwmLevel = 80;
	err = led_set_brightness(pwmsDev, BLUE1, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return;
	}
	err = led_set_brightness(pwmsDev, RED2, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return;
	}
	pwmLevel = 20;
	err = led_set_brightness(pwmsDev, GREEN2, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return 0;
	}
	pwmLevel = 30;
	err = led_set_brightness(pwmsDev, BLUE2, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return;
	}
#endif
	// INPUT_CALLBACK_DEFINE(NULL, buttonsHandlerWrapper, (void *)this);
	// INPUT_CALLBACK_DEFINE(buttons, buttonsHandlerWrapper, (void *)this);
}

void Humidifier:: setDefaultSettings()
{
#ifdef CONFIG_BOARD_ESP32_DEVKITC
	strcpy(settings.credentials.ssid,"Humidifer");
	strcpy(settings.credentials.password,"12345678");
#endif
	settings.fanSpeed = 50;
	settings.piezos.active = -1;
	settings.piezos.piezosNum = 3;
	settings.piezos.piezos[0].intensity = 50;
	settings.piezos.piezos[0].led.red = 50;
	settings.piezos.piezos[0].led.green = 50;
	settings.piezos.piezos[0].led.blue = 50;
	settings.piezos.piezos[1].intensity = 50;
	settings.piezos.piezos[1].led.red = 50;
	settings.piezos.piezos[1].led.green = 50;
	settings.piezos.piezos[1].led.blue = 50;
	settings.piezos.piezos[2].intensity = 50;
	settings.piezos.piezos[2].led.red = 50;
	settings.piezos.piezos[2].led.green = 50;
	settings.piezos.piezos[2].led.blue = 50;
}

// void Humidifier:: buttonsHandlerWrapper(struct input_event *val, void *userData)
// {
//     humidifier->buttonsHandler(val);
// }
// void Humidifier:: buttonsHandler(struct input_event *val)
// {
//     if (val->type == INPUT_EV_KEY)
//     {
//         // if((val->code == INPUT_BTN_0)
//         switch(val->code)
// 	{
// 		case INPUT_BTN_0:

// 			break;

// 		case INPUT_BTN_1:
// 			break;

// 		case INPUT_BTN_2:
// 			break;
// 	};
//         // {
//         //     sprintf(msg.topic, "%sswitch1", instance->mqttCommand);
//         //     val->value ? sprintf(msg.msg, "true"): sprintf(msg.msg, "false");
//         // }
//         // else if((val->code == INPUT_BTN_7) && (val->value))
//         // {
//         //     sprintf(msg.topic, "%slangButton", instance->mqttCommand);
//         //     sprintf(msg.msg, "true");
//         // }
//         // else if((val->code == INPUT_BTN_8) && (val->value))
//         // {
//         //     sprintf(msg.topic, "%sroomButton", instance->mqttCommand);
//         //     sprintf(msg.msg, "true");
//         // }
//         // else
//         // {
//         //     if(val->value)
//         //     {
//         //         sprintf(msg.topic, "%sbutton%d", mqttCommand, (val->code - 0x100) + 1);
//         //         sprintf(msg.msg, "true");
//         //     }
//         // }
//         // k_msgq_put(&msqSendToMQTT, &msg, K_NO_WAIT);
//     }
// }

int Humidifier:: readInfosFromMemory()
{
	// nvsInit();
	// eraseStorage();
	int rc = -1;
	// rc = nvs_read(fs, NVS_SETTINGS_ID, &settings, sizeof(struct Settings));
	if (rc < 0)
	{
		setDefaultSettings();
	}
	return rc;
}

// int Humidifier:: nvsInit()
// {
// 	int rc;
// 	struct flash_pages_info info;
// 	fs = new(struct nvs_fs);
// 	fs->flash_device = NVS_PARTITION_DEVICE;
//     	if (!device_is_ready(fs->flash_device)) {
// 		printk("Flash device %s is not ready\n", fs->flash_device->name);
// 		return 0;
// 	}
// 	fs->offset = NVS_PARTITION_OFFSET;
// 	rc = flash_get_page_info_by_offs(fs->flash_device, fs->offset, &info);
// 	if (rc) {
// 		printk("Unable to get page info, rc=%d\n", rc);
// 		return 0;
// 	}
// 	fs->sector_size = info.size;
// 	LOG_DBG("The page size is: %d", info.size);
// 	fs->sector_count = 2U;
// 	rc = nvs_mount(fs);
// 	LOG_DBG("rc is: %d", rc);

// 	if (rc) {
// 		flash_erase(fs->flash_device, NVS_PARTITION_OFFSET, fs->sector_count * fs->sector_size);
// 		rc = nvs_mount(fs);
// 		if (rc) {
// 			printk("Flash Init failed, rc=%d\n", rc);
// 			return 0;
// 		}
// 	}
// 	return 0;
// }

void Humidifier:: setPiezoPwm(uint8_t piezoNum,
			uint8_t red,
			uint8_t green,
			uint8_t blue,
			uint8_t intensity
			)
{
	int redCh, greenCh, blueCh;

	switch (piezoNum) {

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
		LOG_ERR("Invalid piezo %d", piezoNum);
		return;
	}

	led_set_brightness(pwmsDev, redCh, red);
	led_set_brightness(pwmsDev, greenCh, green);
	led_set_brightness(pwmsDev, blueCh, blue);
	led_set_brightness(pwmsDev, PIEZO, intensity);
}

int Humidifier::piezosStatus(char *buf, size_t bufSize)
{
    int ret = json_obj_encode_buf(piezosDescr, ARRAY_SIZE(piezosDescr), &settings.piezos, buf, bufSize);
    if (ret < 0) LOG_ERR("Failed to encode piezos status");
    return ret;
}

void Humidifier:: parsePiezosPost(char *buf, size_t len)
{
	struct PiezoStatus cmd;
	int ret = json_obj_parse(buf, len, piezoDescr, ARRAY_SIZE(piezoDescr), &cmd);

	if (ret < 0) {
		LOG_ERR("Piezo JSON parse failed");
		return;
	}

	LOG_INF("Piezo %u RGB(%u,%u,%u) Intensity %u",
		cmd.piezoNum,
		cmd.led.red,
		cmd.led.green,
		cmd.led.blue,
		cmd.intensity
	);

	if (cmd.piezoNum < NUM_OF_PIEZOS) {
		settings.piezos.piezos[cmd.piezoNum].led.red = cmd.led.red;
		settings.piezos.piezos[cmd.piezoNum].led.green = cmd.led.red;
		settings.piezos.piezos[cmd.piezoNum].led.blue = cmd.led.red;
		settings.piezos.piezos[cmd.piezoNum].intensity = cmd.intensity;
		settings.piezos.active = cmd.piezoNum;
	}

	setPiezoPwm(
		cmd.piezoNum,
		cmd.led.red,
		cmd.led.red,
		cmd.led.red,
		cmd.intensity
	);
}

void Humidifier:: parseFanPost(char *buf, size_t len)
{
	struct FanStatus cmd;

	int ret = json_obj_parse(
		buf,
		len,
		fanDescr,
		ARRAY_SIZE(fanDescr),
		&cmd);

	if (ret < 0) {
		LOG_ERR("Fan JSON parse failed");
		return;
	}

	LOG_INF("Fan speed %d", cmd.speed);

	led_set_brightness(
		pwmsDev,
		FAN,
		cmd.speed);
	settings.fanSpeed = cmd.speed;
}

// void Humidifier:: parseCredentialsPost(char *buf, size_t len)
// {
	// struct Credentials cmd;
	// int ret = json_obj_parse(buf, len, credentialsDescr, ARRAY_SIZE(credentialsDescr), &cmd);

	// if (ret < 0) {
	// 	LOG_ERR("Credentials JSON parse failed");
	// 	return;
	// }
	// LOG_INF("Set credentials to ssid: %s, password: %s", cmd.ssid, cmd.password);
	// setCredentials(cmd.ssid, cmd.password);
// }

// void Humidifier:: parseIpAddressPost(char *buf, size_t len)
// {
	// struct IpAddress cmd;
	// int ret = json_obj_parse(buf, len, ipAddressDescr, ARRAY_SIZE(ipAddressDescr), &cmd);

	// if (ret < 0) {
	// 	LOG_ERR("Piezo JSON parse failed");
	// 	return;
	// }
	// LOG_INF("Set IP Address to: %s", cmd.ip);
	// setIpAddress(cmd.ipAddress);
// }


int Humidifier:: fanStatus(char *buf, size_t bufSize)
{
	int ret = 0;
	static FanStatus fan;
	fan.speed = settings.fanSpeed;
	ret = json_obj_encode_buf(fanDescr, ARRAY_SIZE(fanDescr), &fan, buf, bufSize);
	LOG_DBG("buffer is: %s", buf);
	return ret;
}
