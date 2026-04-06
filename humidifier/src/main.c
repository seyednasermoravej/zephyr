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
//-DCONF_FILE=prj.conf -DOVERLAY_CONFIG=boards/esp32s3_devkitc.conf -DDTC_OVERLAY_FILE=boards/esp32s3_devkitc.overlay
#if CONFIG_USB_DEVICE_STACK_NEXT
#include <sample_usbd.h>
#endif

#include "ws.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_http_server_sample, LOG_LEVEL_DBG);

#include <zephyr/drivers/pwm.h>
void pwmInit(const struct pwm_dt_spec *pwm, const char *message);
void pwmInit(const struct pwm_dt_spec *pwm, const char *message)
{
    device_init(pwm->dev);
    char buf[200];
    strcpy(buf, message);
    strcat(buf, pwm->dev->name);
    if (!pwm_is_ready_dt(pwm))
    {
        LOG_ERR("%s\n", buf);
    }

}

struct led_command {
	int led_num;
	bool led_state;
};

static const struct json_obj_descr led_command_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct led_command, led_num, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct led_command, led_state, JSON_TOK_TRUE),
};

static const struct device *leds_dev = DEVICE_DT_GET_ANY(gpio_leds);

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

static void parse_led_post(uint8_t *buf, size_t len)
{
	int ret;
	struct led_command cmd;
	const int expected_return_code = BIT_MASK(ARRAY_SIZE(led_command_descr));

	ret = json_obj_parse(buf, len, led_command_descr, ARRAY_SIZE(led_command_descr), &cmd);
	if (ret != expected_return_code) {
		LOG_WRN("Failed to fully parse JSON payload, ret=%d", ret);
		return;
	}

	LOG_INF("POST request setting LED %d to state %d", cmd.led_num, cmd.led_state);

	if (leds_dev != NULL) {
		if (cmd.led_state) {
			led_on(leds_dev, cmd.led_num);
		} else {
			led_off(leds_dev, cmd.led_num);
		}
	}
}

static int led_handler(struct http_client_ctx *client, enum http_transaction_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx, void *user_data)
{
	static uint8_t post_payload_buf[32];
	static size_t cursor;

	LOG_DBG("LED handler status %d, size %zu", status, request_ctx->data_len);

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		return 0;
	}

	if (request_ctx->data_len + cursor > sizeof(post_payload_buf)) {
		cursor = 0;
		return -ENOMEM;
	}

	/* Copy payload to our buffer. Note that even for a small payload, it may arrive split into
	 * chunks (e.g. if the header size was such that the whole HTTP request exceeds the size of
	 * the client buffer).
	 */
	memcpy(post_payload_buf + cursor, request_ctx->data, request_ctx->data_len);
	cursor += request_ctx->data_len;

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		parse_led_post(post_payload_buf, cursor);
		cursor = 0;
	}

	return 0;
}

static struct http_resource_detail_dynamic led_resource_detail = {
	.common = {
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
		},
	.cb = led_handler,
	.user_data = NULL,
};

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

HTTP_RESOURCE_DEFINE(led_resource, test_http_service, "/led", &led_resource_detail);

#if defined(CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE)
HTTP_RESOURCE_DEFINE(ws_echo_resource, test_http_service, "/ws_echo", &ws_echo_resource_detail);

HTTP_RESOURCE_DEFINE(ws_netstats_resource, test_http_service, "/", &ws_netstats_resource_detail);
#endif /* CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE */
#endif /* CONFIG_NET_SAMPLE_HTTP_SERVICE */

#if defined(CONFIG_NET_SAMPLE_HTTPS_SERVICE)
#include "certificate.h"

static const sec_tag_t sec_tag_list_verify_none[] = {
		HTTP_SERVER_CERTIFICATE_TAG,
#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED)
		PSK_TAG,
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

#if defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED)
	err = tls_credential_add(PSK_TAG,
				 TLS_CREDENTIAL_PSK,
				 psk,
				 sizeof(psk));
	if (err < 0) {
		LOG_ERR("Failed to register PSK: %d", err);
	}

	err = tls_credential_add(PSK_TAG,
				 TLS_CREDENTIAL_PSK_ID,
				 psk_id,
				 sizeof(psk_id) - 1);
	if (err < 0) {
		LOG_ERR("Failed to register PSK ID: %d", err);
	}
#endif /* defined(CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED) */
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
#define NUM_OF_LEDS	9
#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	PWM_DT_SPEC_GET_BY_IDX(node_id, idx),
static const struct pwm_dt_spec allleds[] = {
    PWM_DT_SPEC_GET(DT_NODELABEL(pwm_leds))
};
int main(void)
{
	// init_usb();

	// setup_tls();
	// http_server_start();
	for (size_t i = 0; i < NUM_OF_LEDS; i++) {
		char message[64];
		sprintf(message, "Error: PWM channel %d is not ready.", i);
		pwmInit(&allleds[i], message);
	}
	pwm_set_pulse_dt(&allleds[1], 100);
	return 0;
}

		// usbd: zephyr_udc0: usbd@40027000 {
		// 	reg = < 0x40027000 0x1000 >;    /* in zephyr/dts/arm/nordic/nrf52840.dtsi:504 */
		// 	interrupts = < 0x27 0x1 >;      /* in zephyr/dts/arm/nordic/nrf52840.dtsi:505 */
		// 	num-bidir-endpoints = < 0x1 >;  /* in zephyr/dts/arm/nordic/nrf52840.dtsi:506 */
		// 	num-in-endpoints = < 0x7 >;     /* in zephyr/dts/arm/nordic/nrf52840.dtsi:507 */
		// 	num-out-endpoints = < 0x7 >;    /* in zephyr/dts/arm/nordic/nrf52840.dtsi:508 */
		// 	num-isoin-endpoints = < 0x1 >;  /* in zephyr/dts/arm/nordic/nrf52840.dtsi:509 */
		// 	num-isoout-endpoints = < 0x1 >; /* in zephyr/dts/arm/nordic/nrf52840.dtsi:510 */
		// 	compatible = "nordic,nrf-usbd"; /* in zephyr/boards/nordic/nrf52840dk/nrf52840dk_nrf52840.dts:289 */
		// 	status = "okay";                /* in zephyr/boards/nordic/nrf52840dk/nrf52840dk_nrf52840.dts:290 */
		// };

		// usb_serial: uart@60038000 {
		// 	compatible = "espressif,esp32-usb-serial"; /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:420 */
		// 	reg = < 0x60038000 0x1000 >;               /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:421 */
		// 	interrupts = < 0x60 0x0 0x0 >;             /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:423 */
		// 	interrupt-parent = < &intc >;              /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:424 */
		// 	clocks = < &clock 0x4 >;                   /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:425 */
		// 	status = "disabled";                       /* in zephyr/boards/espressif/esp32s3_devkitc/esp32s3_devkitc_procpu.dts:46 */
		// };

		// /* node '/soc/usb_otg@60080000' defined in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:428 */
		// usb_otg: zephyr_udc0: usb_otg@60080000 {
		// 	compatible = "espressif,esp32-usb-otg",
		// 	             "snps,dwc2";               /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:429 */
		// 	reg = < 0x60080000 0x40000 >;           /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:430 */
		// 	interrupts = < 0x26 0x0 0x0 >;          /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:432 */
		// 	interrupt-parent = < &intc >;           /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:433 */
		// 	clocks = < &clock 0x4 >;                /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:434 */
		// 	num-out-eps = < 0x6 >;                  /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:435 */
		// 	num-in-eps = < 0x6 >;                   /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:436 */
		// 	ghwcfg1 = < 0x0 >;                      /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:437 */
		// 	ghwcfg2 = < 0x224dd930 >;               /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:438 */
		// 	ghwcfg4 = < 0xd3f0a030 >;               /* in zephyr/dts/xtensa/espressif/esp32s3/esp32s3_common.dtsi:439 */
		// 	status = "okay";                        /* in zephyr/boards/espressif/esp32s3_devkitc/esp32s3_devkitc_procpu.dts:152 */
		// };
