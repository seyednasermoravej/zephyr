#ifndef __HUMIDIFIER__H__
#define __HUMIDIFIER__H__

#include <stdio.h>
#include <inttypes.h>
#include <time.h>

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
#include <zephyr/logging/log.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/net/sntp.h>
#include <zephyr/drivers/adc.h>

#define NVS_PARTITION               storage_partition
#define NVS_PARTITION_DEVICE        PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET        PARTITION_OFFSET(NVS_PARTITION)

#define NUM_OF_PIEZOS 3
#define MAX_SCHEDULES 10
#define NVS_SETTINGS_ID 0

#define SCHEDULER_STACK_SIZE	2048
#define LOW_LEVEL_THRESHOLD	1000
struct Schedule {

    uint32_t id;

    bool enabled;

    // bit0=Sunday ... bit6=Saturday
    uint8_t days;

    uint8_t hour;
    uint8_t minute;

    uint16_t durationMinutes;

    bool oneTime;

    uint8_t activePiezo;

    uint8_t intensity;

    uint8_t fanLevel;

    uint8_t brightness;

    // anti duplicate trigger
    uint32_t lastRunEpochMinute;

    // runtime state
    bool running;

    uint32_t stopEpoch;
};

enum pwmIndices {
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

struct PiezoStatusUpdate {
    uint8_t piezoNum;
    uint8_t brightness;
    uint8_t intensity;
};

struct PiezoStatus {
    uint8_t brightness;
    uint8_t intensity;
};

struct PiezosStatus {
    int8_t active;
    struct PiezoStatus piezos[NUM_OF_PIEZOS];
    size_t piezosNum;
};

struct FanStatus {
    uint8_t speed;
};

struct TimeStatus {
    uint32_t timestamp;
    bool synced;
};

struct Credentials {
    char ssid[32];
    char password[32];
};

struct Settings {

    uint8_t fanSpeed;

    struct PiezosStatus piezos;

    struct Credentials credentials;

    uint32_t nextScheduleId;

    uint8_t schedulesNum;

    struct Schedule schedules[MAX_SCHEDULES];
};

static const struct json_obj_descr credentialsDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct Credentials, ssid, JSON_TOK_STRING_BUF),
    JSON_OBJ_DESCR_PRIM(struct Credentials, password, JSON_TOK_STRING_BUF),
};

static const struct json_obj_descr piezoDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct PiezoStatus, brightness, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct PiezoStatus, intensity, JSON_TOK_UINT),
};

static const struct json_obj_descr piezoDescrUpdate[] = {
    JSON_OBJ_DESCR_PRIM(struct PiezoStatusUpdate, piezoNum, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct PiezoStatusUpdate, brightness, JSON_TOK_UINT),
    JSON_OBJ_DESCR_PRIM(struct PiezoStatusUpdate, intensity, JSON_TOK_UINT),
};

static const struct json_obj_descr piezosDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct PiezosStatus, active, JSON_TOK_INT),
    JSON_OBJ_DESCR_OBJ_ARRAY(
        struct PiezosStatus,
        piezos,
        NUM_OF_PIEZOS,
        piezosNum,
        piezoDescr,
        ARRAY_SIZE(piezoDescr)
    ),
};

static const struct json_obj_descr fanDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct FanStatus, speed, JSON_TOK_UINT),
};

static const struct json_obj_descr timeDescr[] = {
    JSON_OBJ_DESCR_PRIM(struct TimeStatus, timestamp, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct TimeStatus, synced, JSON_TOK_TRUE),
};

class Humidifier
{
public:

    Humidifier();

    void parsePiezosPost(char *buf, size_t len);

    void parseFanPost(char *buf, size_t len);

    void parseCredentialsPost(char *buf, size_t len);

    int fanStatus(char *buf, size_t bufSize);

    int piezosStatus(char *buf, size_t bufSize);

    int credentialStatus(char *buf, size_t bufSize);

    int timeStatus(char *buf, size_t bufSize);

    void getCredentials(char *ssid, char *psk);

    void syncTimeSntp();

    static void buttonsHandlerWrapper(struct input_event *val, void *userData);

    void buttonsHandler(struct input_event *val);

private:

    struct Settings settings;

    struct nvs_fs *fs;

    uint32_t currentTime;

    bool timeSynced;

    struct k_thread scheduleThread;

    static void schedulerThreadWrapper(void *arg1, void *arg2, void *arg3);

    void scheduleLoop();

    void checkSchedules();

    void runSchedule(struct Schedule *schedule);

    void stopSchedule(struct Schedule *schedule);

    void setPiezoPwm(
        uint8_t piezoNum,
        uint8_t red,
        uint8_t green,
        uint8_t blue,
        uint8_t intensity
    );

    int nvsInit();

    int readInfosFromMemory();

    int writeSettings();

    void setDefaultSettings();

    void setCredentials(char *ssid, char *password);

	bool safetyCheck();

	bool checkLowPiezo(uint8_t i);

	uint16_t readAdc(uint8_t index);

	int hardwareInit();
};

#endif
