#ifndef __HUMIDIFIER__H__
#define __HUMIDIFIER__H__


#include <stdio.h>
#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include "zephyr/sys/util.h"
#include <zephyr/drivers/led.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/net/net_config.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#ifdef CONFIG_BOARD_ESP32_DEVKITC
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#endif

#include <zephyr/drivers/flash.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#define NVS_PARTITION		storage_partition
#define NVS_PARTITION_DEVICE	PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET	PARTITION_OFFSET(NVS_PARTITION)

#include <zephyr/drivers/pwm.h>

#include <zephyr/logging/log.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>

enum pwmIndices
{
	FAN = 0,
	PIEZO,
	RED0,
	GREEN0,
	BLUE0,
	RED1,
	GREEN1,
	BLUE1,
	RED2,
	GREEN2,
	BLUE2,
};

struct rgbValues
{
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};
struct PiezoStatus {
	uint8_t piezoNum;
	struct rgbValues led;
	uint8_t intensity;
};

#define NUM_OF_PIEZOS	3

struct PiezosStatus {
	int8_t active;
	struct PiezoStatus piezos[3];
	uint8_t piezosNum;
};
struct FanStatus {
	uint8_t speed;
};

struct Credentials
{
	char *ssid;
	char *password;
};

struct IpAddress
{
	const char *ip;
};

struct Settings
{
	// uint64_t uptime;
	uint8_t fanSpeed = 50;
	struct PiezosStatus piezos = { 0 };
	// struct IpAddress ipAddress = { 0 };
#ifdef CONFIG_BOARD_ESP32_DEVKITC
	struct Credentials credentials = { 0 };
#endif

};

static const struct json_obj_descr ipAddressDescr[] = {
	JSON_OBJ_DESCR_PRIM(struct IpAddress, ip, JSON_TOK_STRING),
};

static const struct json_obj_descr credentialsDescr[] = {
	JSON_OBJ_DESCR_PRIM(struct Credentials, ssid, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct Credentials, password, JSON_TOK_STRING),
};

static const struct json_obj_descr rgbDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct rgbValues, red, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct rgbValues, green, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct rgbValues, blue, JSON_TOK_UINT),
};

static const struct json_obj_descr piezoDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct PiezoStatus, piezoNum, JSON_TOK_UINT),
    JSON_OBJ_DESCR_OBJECT(struct PiezoStatus, led, rgbDescr),
    JSON_OBJ_DESCR_PRIM(struct PiezoStatus, intensity, JSON_TOK_UINT),
};
static const struct json_obj_descr piezosDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct PiezosStatus, active, JSON_TOK_INT),
    JSON_OBJ_DESCR_OBJ_ARRAY(struct PiezosStatus, piezos, NUM_OF_PIEZOS, piezosNum, piezoDescr, ARRAY_SIZE(piezoDescr)),
};


static const struct json_obj_descr fanDescr[] = {
	JSON_OBJ_DESCR_PRIM(struct FanStatus, speed, JSON_TOK_UINT),
};

// static const struct json_obj_descr settingsDescr[] = {
// 	JSON_OBJ_DESCR_PRIM(struct Settings, uptime, JSON_TOK_UINT64),
// 	JSON_OBJ_DESCR_PRIM(struct Settings, fanSpeed, JSON_TOK_UINT),
// 	JSON_OBJ_DESCR_OBJECT(struct Settings, piezos, piezosDescr),

// };

#define NVS_SETTINGS_ID 0


class Humidifier
{
public:
	Humidifier();
	void parsePiezosPost(char *buf, size_t len);
	void parseFanPost(char *buf, size_t len);
	// // void parseCredentialsPost(char *buf, size_t len);
	void parseIpAddressPost(char *buf, size_t len);
	int fanStatus(char *buf, size_t bufSize);
	int piezosStatus(char *buf, size_t bufSize);

	// static void buttonsHandlerWrapper(struct input_event *val, void* userData);
	// void buttonsHandler(struct input_event *val);

private:
	struct Settings settings;
	// struct nvs_fs *fs;

	int readInfosFromMemory();
	// int nvsInit();
	void setDefaultSettings();
	void setPiezoPwm(uint8_t piezoNum, uint8_t red,	uint8_t green,
			uint8_t blue, uint8_t intensity);

};

#endif/*__HUMIDIFIER__H__*/
