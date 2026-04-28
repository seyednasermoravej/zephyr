/*
 * Copyright (c) 2023, Emna Rekik
 * Copyright (c) 2024, Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include "zephyr/device.h"
#include "zephyr/sys/util.h"
#include <zephyr/drivers/led.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/net/net_config.h>

#ifdef CONFIG_BOARD_ESP32_DEVKITC
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#endif

#include <zephyr/drivers/pwm.h>
#if CONFIG_USB_DEVICE_STACK_NEXT
#include <sample_usbd.h>
#endif

#include "ws.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_http_server_sample, LOG_LEVEL_INF);

#define LED_PWM_NODE_ID	 DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds)

static const struct device *pwmsDev = DEVICE_DT_GET(LED_PWM_NODE_ID);
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

static void setPiezoPwm(uint8_t piezoNum,
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
struct PiezosStatus
{
	int8_t active;
	struct PiezoStatus piezos[NUM_OF_PIEZOS];
	uint8_t piezosNum;
};

struct PiezosStatus piezosStatus = { 0 };

struct FanStatus {
	uint8_t speed;
};

struct FanStatus fanStatus = {0};
struct Credentials
{
	const char *ssid;
	const char *password;
};

struct Credentials credentials = {0};
struct IpAddress
{
	const char *ip;
};

struct IpAddress ipAddress = { 0 };

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

static uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static uint8_t main_js_gz[] = {
#include "main.js.gz.inc"
};

static struct http_resource_detail_static index_html_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};

static struct http_resource_detail_static main_js_gz_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = main_js_gz,
	.static_data_len = sizeof(main_js_gz),
};

static int echo_handler(struct http_client_ctx *client, enum http_transaction_status status,
			const struct http_request_ctx *request_ctx,
			struct http_response_ctx *response_ctx, void *user_data)
{
#define MAX_TEMP_PRINT_LEN 32
	static char print_str[MAX_TEMP_PRINT_LEN];
	enum http_method method = client->method;
	static size_t processed;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		if (status == HTTP_SERVER_TRANSACTION_ABORTED) {
			LOG_DBG("Transaction aborted after %zd bytes.", processed);
		}
		processed = 0;
		return 0;
	}

	__ASSERT_NO_MSG(request_ctx->data != NULL);

	processed += request_ctx->data_len;

	snprintf(print_str, sizeof(print_str), "%s received (%zd bytes)", http_method_str(method),
		 request_ctx->data_len);
	LOG_HEXDUMP_DBG(request_ctx->data, request_ctx->data_len, print_str);

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		LOG_DBG("All data received (%zd bytes).", processed);
		processed = 0;
	}

	/* Echo data back to client */
	response_ctx->body = request_ctx->data;
	response_ctx->body_len = request_ctx->data_len;
	response_ctx->final_chunk = (status == HTTP_SERVER_REQUEST_DATA_FINAL);

	return 0;
}

static struct http_resource_detail_dynamic echo_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
		},
	.cb = echo_handler,
	.user_data = NULL,
};

static int uptime_handler(struct http_client_ctx *client, enum http_transaction_status status,
			  const struct http_request_ctx *request_ctx,
			  struct http_response_ctx *response_ctx, void *user_data)
{
	int ret;
	static uint8_t uptime_buf[sizeof(STRINGIFY(INT64_MAX))];

	LOG_DBG("Uptime handler status %d", status);

	/* A payload is not expected with the GET request. Ignore any data and wait until
	 * final callback before sending response
	 */
	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		ret = snprintf(uptime_buf, sizeof(uptime_buf), "%" PRId64, k_uptime_get());
		if (ret < 0) {
			LOG_ERR("Failed to snprintf uptime, err %d", ret);
			return ret;
		}

		response_ctx->body = uptime_buf;
		response_ctx->body_len = ret;
		response_ctx->final_chunk = true;
	}

	return 0;
}

static struct http_resource_detail_dynamic uptime_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = uptime_handler,
	.user_data = NULL,
};


static void parseIpAddressPost(uint8_t *buf, size_t len)
{
	struct IpAddress cmd;
	int ret = json_obj_parse(buf, len, ipAddressDescr, ARRAY_SIZE(ipAddressDescr), &cmd);

	if (ret < 0) {
		LOG_ERR("Piezo JSON parse failed");
		return;
	}
	LOG_INF("Set IP Address to: %s", cmd.ip);
	// setIpAddress(cmd.ipAddress);
}

static int ipAddressHandler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *requestCtx,
		      struct http_response_ctx *responseCtx,
		      void *userData)
{
	static uint8_t postBuf[128];
	static size_t cursor;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		return 0;
	}

	if (requestCtx->data_len + cursor > sizeof(postBuf)) {
		cursor = 0;
		return -ENOMEM;
	}

	memcpy(postBuf + cursor,
	       requestCtx->data,
	       requestCtx->data_len);

	cursor += requestCtx->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		parseIpAddressPost(postBuf, cursor);
		cursor = 0;
	}

	return 0;
}

static struct http_resource_detail_dynamic ipAddressResourceDetail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = ipAddressHandler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(ipAddressResource,
		     test_http_service,
		     "/ipAddress",
		     &ipAddressResourceDetail);


static void parseCredentialsPost(uint8_t *buf, size_t len)
{
	struct Credentials cmd;
	int ret = json_obj_parse(buf, len, credentialsDescr, ARRAY_SIZE(credentialsDescr), &cmd);

	if (ret < 0) {
		LOG_ERR("Credentials JSON parse failed");
		return;
	}
	LOG_INF("Set credentials to ssid: %s, password: %s", cmd.ssid, cmd.password);
	// setCredentials(cmd.ssid, cmd.password);
}

static int credentialsHandler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *requestCtx,
		      struct http_response_ctx *responseCtx,
		      void *userData)
{
	static uint8_t postBuf[128];
	static size_t cursor;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		return 0;
	}

	if (requestCtx->data_len + cursor > sizeof(postBuf)) {
		cursor = 0;
		return -ENOMEM;
	}

	memcpy(postBuf + cursor,
	       requestCtx->data,
	       requestCtx->data_len);

	cursor += requestCtx->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		parseCredentialsPost(postBuf, cursor);
		cursor = 0;
	}

	return 0;
}

static struct http_resource_detail_dynamic credentialsResourceDetail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = credentialsHandler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(credentialsResource,
		     test_http_service,
		     "/credentials",
		     &credentialsResourceDetail);


static void parsePiezosPost(uint8_t *buf, size_t len)
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

	setPiezoPwm(
		cmd.piezoNum,
		cmd.led.red,
		cmd.led.green,
		cmd.led.blue,
		cmd.intensity
	);
}

static int piezosHandler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *requestCtx,
		      struct http_response_ctx *responseCtx,
		      void *userData)
{
	static uint8_t postBuf[128];
	static size_t cursor;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		return 0;
	}

	if (requestCtx->data_len + cursor > sizeof(postBuf)) {
		cursor = 0;
		return -ENOMEM;
	}

	memcpy(postBuf + cursor,
	       requestCtx->data,
	       requestCtx->data_len);

	cursor += requestCtx->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		parsePiezosPost(postBuf, cursor);
		cursor = 0;
		int ret;
		static uint8_t piezosStatusBuf[128];

		LOG_INF("piezos status %d", status);

		/* A payload is not expected with the GET request. Ignore any data and wait until
		* final callback before sending response
		*/
		ret = json_obj_encode_buf(piezosDescr, ARRAY_SIZE(piezosDescr), &piezosStatus, piezosStatusBuf, 128);
		if (ret < 0) {
			LOG_ERR("Failed to encode piezos, err %d", ret);
			return ret;
		}

		responseCtx->body = piezosStatusBuf;
		responseCtx->body_len = ret;
		responseCtx->final_chunk = true;
	}

	return 0;
}
static struct http_resource_detail_dynamic piezosResourceDetail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = piezosHandler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(piezosResource,
		     test_http_service,
		     "/piezos",
		     &piezosResourceDetail);


static void parseFanPost(uint8_t *buf, size_t len)
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
}

static int fanHandler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *requestCtx,
		      struct http_response_ctx *responseCtx,
		      void *userData)
{
	static uint8_t postBuf[32];
	static size_t cursor;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		return 0;
	}

	if (requestCtx->data_len + cursor > sizeof(postBuf)) {
		cursor = 0;
		return -ENOMEM;
	}

	memcpy(postBuf + cursor,
	       requestCtx->data,
	       requestCtx->data_len);

	cursor += requestCtx->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		parseFanPost(postBuf, cursor);
		cursor = 0;

		int ret;
		static uint8_t fanStatusBuf[sizeof(STRINGIFY(INT64_MAX))];

		LOG_INF("fan status %d", status);

		/* A payload is not expected with the GET request. Ignore any data and wait until
		* final callback before sending response
		*/
		ret = snprintf(fanStatusBuf, sizeof(fanStatusBuf), "%u", fanStatus.speed);
		if (ret < 0) {
			LOG_ERR("Failed to snprintf fan, err %d", ret);
			return ret;
		}

		responseCtx->body = fanStatusBuf;
		responseCtx->body_len = ret;
		responseCtx->final_chunk = true;

	}

	return 0;
}

static struct http_resource_detail_dynamic fanResourceDetail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST) | BIT(HTTP_GET),
	},
	.cb = fanHandler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(
	fanResource,
	test_http_service,
	"/fan",
	&fanResourceDetail);



#if defined(CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE)
static uint8_t ws_echo_buffer[1024];

struct http_resource_detail_websocket ws_echo_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_WEBSOCKET,

			/* We need HTTP/1.1 Get method for upgrading */
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = ws_echo_setup,
	.data_buffer = ws_echo_buffer,
	.data_buffer_len = sizeof(ws_echo_buffer),
	.user_data = NULL, /* Fill this for any user specific data */
};

static uint8_t ws_netstats_buffer[128];

struct http_resource_detail_websocket ws_netstats_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_WEBSOCKET,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		},
	.cb = ws_netstats_setup,
	.data_buffer = ws_netstats_buffer,
	.data_buffer_len = sizeof(ws_netstats_buffer),
	.user_data = NULL,
};

#endif /* CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE */

#if defined(CONFIG_NET_SAMPLE_HTTP_SERVICE)
static uint16_t test_http_service_port = CONFIG_NET_SAMPLE_HTTP_SERVER_SERVICE_PORT;
HTTP_SERVICE_DEFINE(test_http_service, NULL, &test_http_service_port,
		    CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

HTTP_RESOURCE_DEFINE(index_html_gz_resource, test_http_service, "/",
		     &index_html_gz_resource_detail);

HTTP_RESOURCE_DEFINE(main_js_gz_resource, test_http_service, "/main.js",
		     &main_js_gz_resource_detail);

HTTP_RESOURCE_DEFINE(echo_resource, test_http_service, "/dynamic", &echo_resource_detail);

HTTP_RESOURCE_DEFINE(uptime_resource, test_http_service, "/uptime", &uptime_resource_detail);


#if defined(CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE)
HTTP_RESOURCE_DEFINE(ws_echo_resource, test_http_service, "/ws_echo", &ws_echo_resource_detail);

HTTP_RESOURCE_DEFINE(ws_netstats_resource, test_http_service, "/", &ws_netstats_resource_detail);
#endif /* CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE */
#endif /* CONFIG_NET_SAMPLE_HTTP_SERVICE */

#if defined(CONFIG_NET_SAMPLE_HTTPS_SERVICE)
#include "certificate.h"

static const sec_tag_t sec_tag_list_verify_none[] = {
		HTTP_SERVER_CERTIFICATE_TAG,
#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_password_ENABLED)
		password_TAG,
#endif
	};

static uint16_t test_https_service_port = CONFIG_NET_SAMPLE_HTTPS_SERVER_SERVICE_PORT;
HTTPS_SERVICE_DEFINE(test_https_service, NULL, &test_https_service_port,
		     CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL, sec_tag_list_verify_none,
		     sizeof(sec_tag_list_verify_none));

HTTP_RESOURCE_DEFINE(index_html_gz_resource_https, test_https_service, "/",
		     &index_html_gz_resource_detail);

HTTP_RESOURCE_DEFINE(main_js_gz_resource_https, test_https_service, "/main.js",
		     &main_js_gz_resource_detail);

HTTP_RESOURCE_DEFINE(echo_resource_https, test_https_service, "/dynamic", &echo_resource_detail);

HTTP_RESOURCE_DEFINE(uptime_resource_https, test_https_service, "/uptime", &uptime_resource_detail);

HTTP_RESOURCE_DEFINE(led_resource_https, test_https_service, "/led", &led_resource_detail);

#if defined(CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE)
HTTP_RESOURCE_DEFINE(ws_echo_resource_https, test_https_service, "/ws_echo",
		     &ws_echo_resource_detail);

HTTP_RESOURCE_DEFINE(ws_netstats_resource_https, test_https_service, "/",
		     &ws_netstats_resource_detail);
#endif /* CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE */
#endif /* CONFIG_NET_SAMPLE_HTTPS_SERVICE */

static void setup_tls(void)
{
#if defined(CONFIG_NET_SAMPLE_HTTPS_SERVICE)
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	int err;

	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
				 TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
				 server_certificate,
				 sizeof(server_certificate));
	if (err < 0) {
		LOG_ERR("Failed to register public certificate: %d", err);
	}

	err = tls_credential_add(HTTP_SERVER_CERTIFICATE_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY,
				 private_key, sizeof(private_key));
	if (err < 0) {
		LOG_ERR("Failed to register private key: %d", err);
	}

#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_password_ENABLED)
	err = tls_credential_add(password_TAG,
				 TLS_CREDENTIAL_password,
				 password,
				 sizeof(password));
	if (err < 0) {
		LOG_ERR("Failed to register password: %d", err);
	}

	err = tls_credential_add(password_TAG,
				 TLS_CREDENTIAL_password_ID,
				 password_id,
				 sizeof(password_id) - 1);
	if (err < 0) {
		LOG_ERR("Failed to register password ID: %d", err);
	}
#endif /* defined(CONFIG_MBEDTLS_KEY_EXCHANGE_password_ENABLED) */
#endif /* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */
#endif /* defined(CONFIG_NET_SAMPLE_HTTPS_SERVICE) */
}

static int init_usb(void)
{
#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
	struct usbd_context *sample_usbd;
	int err;

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		return -ENODEV;
	}

	err = usbd_enable(sample_usbd);
	if (err) {
		return err;
	}

	(void)net_config_init_app(NULL, "Initializing network");
#endif /* CONFIG_USB_DEVICE_STACK_NEXT */

	return 0;
}

struct Settings
{
	struct PiezosStatus piezos;
	struct FanStatus fan;
	struct Credentials credentials;

};

struct Settings settings = { 0 };

void setDefaultSettings(struct Settings *s)
{
	strcpy(s->credentials.ssid,"Humidifer");
	strcpy(s->credentials.password,"12345678");
	s->fan.speed = 50;
	s->piezos.active = -1;
	s->piezos.piezosNum = 3;
	s->piezos.piezos[0].intensity = 50;
	s->piezos.piezos[0].led.red = 50;
	s->piezos.piezos[0].led.green = 50;
	s->piezos.piezos[0].led.blue = 50;
	s->piezos.piezos[1].intensity = 50;
	s->piezos.piezos[1].led.red = 50;
	s->piezos.piezos[1].led.green = 50;
	s->piezos.piezos[1].led.blue = 50;
	s->piezos.piezos[2].intensity = 50;
	s->piezos.piezos[2].led.red = 50;
	s->piezos.piezos[2].led.green = 50;
	s->piezos.piezos[2].led.blue = 50;
}
void loadSettings()
{
	// it should read the values from the nv memory otherwise eveything loads to default value.
	int ret = 0;
	if (ret > 0)
	{
		memcpy();
	}
	else
	{
		setDefaultSettings(&settings);
	}
}



int main(void)
{
#ifdef CONFIG_BOARD_ESP32_DEVKITC
	struct net_if *iface = net_if_get_default();

	struct wifi_connect_req_params connect_params = {
		// .ssid = "Naser-Wi-Fi",
		// .ssid_length = strlen("Naser-Wi-Fi"),
		// .psk = "1020151515",
		// .psk_length = strlen("1020151515"),
		.ssid = "PAIDAR",
		.ssid_length = strlen("PAIDAR"),
		.psk = "Atal-Matal 347",
		.psk_length = strlen("Atal-Matal 347"),
		// .ssid = "Naser",
		// .ssid_length = strlen("Naser"),
		// .psk = "nasimore",
		// .psk_length = strlen("nasimore"),
		.security = WIFI_SECURITY_TYPE_PSK,
	};
	net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &connect_params, sizeof(connect_params));
#else
	init_usb();
#endif
	LOG_INF("Besme Allah");
	loadSettings();
	if (!device_is_ready(pwmsDev)) {
		LOG_ERR("Device %s is not ready", pwmsDev->name);
		return 0;
	}
	int err;
	fanStatus.speed = 5;
	err = led_set_brightness(pwmsDev, FAN, fanStatus.speed);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, fanStatus.speed);
		return 0;
	}
	piezosStatus.piezos[0].intensity = 20;
	err = led_set_brightness(pwmsDev, PIEZO, piezosStatus.piezos[0].intensity);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, piezosStatus.piezos[0].intensity);
		return 0;
	}
	piezosStatus.piezos[0].led.red = 30;
	err = led_set_brightness(pwmsDev, RED0, piezosStatus.piezos[0].led.red);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, piezosStatus.piezos[0].led.red);
		return 0;
	}
	piezosStatus.piezos[0].led.green = 40;
	err = led_set_brightness(pwmsDev, GREEN0, piezosStatus.piezos[0].led.green);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, piezosStatus.piezos[0].led.green);
		return 0;
	}

	// setup_tls();
#ifdef CONFIG_BOARD_ESP32_DEVKITC
	pwmLevel = 50;
	err = led_set_brightness(pwmsDev, BLUE0, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return 0;
	}

	pwmLevel = 60;
	err = led_set_brightness(pwmsDev, RED1, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return 0;
	}
	pwmLevel = 70;
	err = led_set_brightness(pwmsDev, GREEN1, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return 0;
	}
	pwmLevel = 80;
	err = led_set_brightness(pwmsDev, BLUE1, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return 0;
	}
	err = led_set_brightness(pwmsDev, RED2, pwmLevel);
	LOG_INF("err=%d \n", err);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, pwmLevel);
		return 0;
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
		return 0;
	}
#endif
	http_server_start();
	return 0;
}
