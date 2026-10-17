/*
 * Copyright (c) 2023, Emna Rekik
 * Copyright (c) 2024, Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include "zephyr/device.h"
#include "zephyr/sys/util.h"
#include <zephyr/data/json.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/net/net_config.h>

#include <time.h>

#if defined(CONFIG_WIFI)
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#endif
#if CONFIG_USB_DEVICE_STACK_NEXT
#include <sample_usbd.h>
extern "C" struct usbd_context *sample_usbd_init_device(usbd_msg_cb_t msg_cb);
#endif

#include "ws.h"
#include "humidifier.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_http_server_sample, LOG_LEVEL_INF);

static Humidifier *humidifier = nullptr;

static uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static uint8_t main_js_gz[] = {
#include "main.js.gz.inc"
};

static uint8_t style_css_gz[] = {
#include "style.css.gz.inc"
};

struct http_resource_detail_static index_html_gz_resource_detail = {
	.common = {
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.content_encoding = "gzip",
			.content_type = "text/html",
	},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};



static struct http_resource_detail_static main_js_gz_resource_detail = {
	.common = {
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.content_encoding = "gzip",
			.content_type = "text/javascript",
		},
	.static_data = main_js_gz,
	.static_data_len = sizeof(main_js_gz),
};

static struct http_resource_detail_static style_css_gz_resource_detail = {
    .common = {
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
        .type = HTTP_RESOURCE_TYPE_STATIC,
        .content_encoding = "gzip",
        .content_type = "text/css",
    },
    .static_data = style_css_gz,
    .static_data_len = sizeof(style_css_gz),
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
			.bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
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
		ret = snprintf((char *)uptime_buf, sizeof(uptime_buf), "%" PRId64, k_uptime_get());
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
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		},
	.cb = uptime_handler,
	.user_data = NULL,
};



// static int ipAddressHandler(struct http_client_ctx *client,
// 		      enum http_transaction_status status,
// 		      const struct http_request_ctx *requestCtx,
// 		      struct http_response_ctx *responseCtx,
// 		      void *userData)
// {
// 	static char postBuf[128];
// 	static size_t cursor;

// 	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
// 	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
// 		cursor = 0;
// 		return 0;
// 	}

// 	if (requestCtx->data_len + cursor > sizeof(postBuf)) {
// 		cursor = 0;
// 		return -ENOMEM;
// 	}

// 	memcpy(postBuf + cursor,
// 	       requestCtx->data,
// 	       requestCtx->data_len);

// 	cursor += requestCtx->data_len;

// 	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
// 		// humidifier->parseIpAddressPost(postBuf, cursor);
// 		cursor = 0;
// 	}

// 	return 0;
// }

// static struct http_resource_detail_dynamic ipAddressResourceDetail = {
// 	.common = {
// 			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
// 			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
// 		},
// 	.cb = ipAddressHandler,
// 	.user_data = NULL,
// };

// HTTP_RESOURCE_DEFINE(ipAddressResource,
// 		     test_http_service,
// 		     "/ipAddress",
// 		     &ipAddressResourceDetail);



static int credentialsHandler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *requestCtx,
		      struct http_response_ctx *responseCtx,
		      void *userData)
{
	static char postBuf[128];
	static size_t cursor;

	if (client->method == HTTP_GET) {
		if (status == HTTP_SERVER_REQUEST_DATA_FINAL || status == HTTP_SERVER_TRANSACTION_COMPLETE) {
			int len = humidifier->credentialStatus(postBuf, sizeof(postBuf));
			if (len == 0) {
				responseCtx->body = (uint8_t *)postBuf;
				responseCtx->body_len = strlen(postBuf);
				responseCtx->final_chunk = true;
			}
		}
		return 0;
	}

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
		humidifier->parseCredentialsPost(postBuf, cursor);
		cursor = 0;
	}

	return 0;
}

static struct http_resource_detail_dynamic credentialsResourceDetail = {
	.common = {
			.bitmask_of_supported_http_methods = BIT(HTTP_POST) | BIT(HTTP_GET),
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		},
	.cb = credentialsHandler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(credentialsResource,
		     test_http_service,
		     "/credentials",
		     &credentialsResourceDetail);



static int piezosHandler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *requestCtx,
		      struct http_response_ctx *responseCtx,
		      void *userData)
{
	/* Zephyr holds a dynamic resource for one client until COMPLETE/ABORTED. */
	static char postBuf[256];
	static char responseBuf[512];
	static size_t cursor;
	static bool tooLarge;
	static bool invalidData;
	static bool responded;

	/* Terminal callbacks are cleanup only, including for GET requests. */
	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		cursor = 0;
		tooLarge = false;
		invalidData = false;
		responded = false;
		postBuf[0] = '\0';
		return 0;
	}

	if (responded) {
		return 0;
	}

	if (client->method == HTTP_POST && !tooLarge && !invalidData) {
		/* Reserve a byte for the terminator; keep the failure until FINAL. */
		if (requestCtx->data_len > sizeof(postBuf) - 1 - cursor) {
			tooLarge = true;
		} else if (requestCtx->data_len > 0) {
			if (requestCtx->data == NULL ||
			    memchr(requestCtx->data, '\0', requestCtx->data_len) != NULL) {
				invalidData = true;
			} else {
				memcpy(postBuf + cursor, requestCtx->data, requestCtx->data_len);
				cursor += requestCtx->data_len;
			}
		}
	}

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	int ret = 0;
	const char *error = NULL;
	responseCtx->status = HTTP_200_OK;
	if (tooLarge) {
		responseCtx->status = HTTP_413_PAYLOAD_TOO_LARGE;
		error = "{\"error\":\"Request body is too large\"}";
	} else if (invalidData) {
		responseCtx->status = HTTP_400_BAD_REQUEST;
		error = "{\"error\":\"Invalid JSON body\"}";
	} else {
		if (client->method == HTTP_POST) {
			postBuf[cursor] = '\0';
			ret = humidifier->parsePiezosPost(postBuf, cursor);
		}
		if (ret == -EINVAL || ret == -ERANGE) {
			responseCtx->status = HTTP_400_BAD_REQUEST;
			error = "{\"error\":\"Use piezoNum -1 (off) or 0-2, brightness and intensity 0-100, and frequencyHz 1600000-1800000\"}";
		} else if (ret < 0) {
			LOG_ERR("Unable to update piezo: %d", ret);
			responseCtx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			error = "{\"error\":\"Unable to apply or save piezo settings\"}";
		} else if (humidifier->piezosStatus(responseBuf, sizeof(responseBuf)) < 0) {
			responseCtx->status = HTTP_500_INTERNAL_SERVER_ERROR;
			error = "{\"error\":\"Unable to read piezo settings\"}";
		}
	}

	responseCtx->body = reinterpret_cast<const uint8_t *>(error ? error : responseBuf);
	responseCtx->body_len = strlen(reinterpret_cast<const char *>(responseCtx->body));
	responseCtx->final_chunk = true;
	responded = true;
	return 0;
}
static struct http_resource_detail_dynamic piezosResourceDetail = {
	.common = {
			.bitmask_of_supported_http_methods = BIT(HTTP_POST) | BIT(HTTP_GET),
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.content_type = "application/json",
		},
	.cb = piezosHandler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(piezosResource,
		     test_http_service,
		     "/piezos",
		     &piezosResourceDetail);



static int fanHandler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *requestCtx,
		      struct http_response_ctx *responseCtx,
		      void *userData)
{
	static char postBuf[32];
	static size_t cursor;

    // --- GET: Return current fan status ---
	if (client->method == HTTP_GET) {
		if (status == HTTP_SERVER_REQUEST_DATA_FINAL || status == HTTP_SERVER_TRANSACTION_COMPLETE) {
			int ret = 0;
			ret = humidifier->fanStatus(postBuf, sizeof(postBuf));
			if(ret < 0)
			{
				return ret;
			}
			responseCtx->body = (uint8_t *)postBuf;
			responseCtx->body_len = strlen(postBuf);
			responseCtx->final_chunk = true;
		}
		return 0;
	}
    	if(client->method == HTTP_GET)
	{

	}
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
		humidifier->parseFanPost(postBuf, cursor);
		cursor = 0;
		responseCtx->body = (uint8_t *)postBuf;
		responseCtx->body_len = strlen(postBuf);
		responseCtx->final_chunk = true;
	}

	return 0;
}

static struct http_resource_detail_dynamic fanResourceDetail = {
	.common = {
		.bitmask_of_supported_http_methods = BIT(HTTP_POST) | BIT(HTTP_GET),
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
	},
	.cb = fanHandler,
	.user_data = NULL,
};

HTTP_RESOURCE_DEFINE(
	fanResource,
	test_http_service,
	"/fan",
	&fanResourceDetail);

// Add after fanHandler, before main()

static int timeHandler(struct http_client_ctx *client,
                       enum http_transaction_status status,
                       const struct http_request_ctx *requestCtx,
                       struct http_response_ctx *responseCtx,
                       void *userData)
{
    static char timeBuf[64];

    // Only support GET
    if (client->method != HTTP_GET) {
        return -EINVAL;
    }

    if (status == HTTP_SERVER_REQUEST_DATA_FINAL ||
        status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        int len = humidifier->timeStatus(timeBuf, sizeof(timeBuf));
        if (len > 0) {
            responseCtx->body = (uint8_t *)timeBuf;
            responseCtx->body_len = len;
            responseCtx->final_chunk = true;
        }
    }
    return 0;
}

static struct http_resource_detail_dynamic timeResourceDetail = {
    .common = {
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
    },
    .cb = timeHandler,
    .user_data = NULL,
};

HTTP_RESOURCE_DEFINE(timeResource,
                     test_http_service,
                     "/time",
                     &timeResourceDetail);

#if defined(CONFIG_NET_SAMPLE_WEBSOCKET_SERVICE)
static uint8_t ws_echo_buffer[1024];

struct http_resource_detail_websocket ws_echo_resource_detail = {
	.common = {
			/* We need HTTP/1.1 Get method for upgrading */
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),

			.type = HTTP_RESOURCE_TYPE_WEBSOCKET,

		},
	.cb = ws_echo_setup,
	.data_buffer = ws_echo_buffer,
	.data_buffer_len = sizeof(ws_echo_buffer),
	.user_data = NULL, /* Fill this for any user specific data */
};

static uint8_t ws_netstats_buffer[128];
struct http_resource_detail_websocket ws_netstats_resource_detail = {
	.common = {
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.type = HTTP_RESOURCE_TYPE_WEBSOCKET,
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

HTTP_RESOURCE_DEFINE(style_css_gz_resource, test_http_service, "/style.css",
			&style_css_gz_resource_detail);

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

#if defined(CONFIG_WIFI)
static struct net_mgmt_event_callback wifiCallback;
static struct net_mgmt_event_callback addressCallback;

static void wifiEventHandler(struct net_mgmt_event_callback *callback, uint64_t event,
			     struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const auto *result = static_cast<const struct wifi_status *>(callback->info);
		if (result != NULL && result->status != 0) {
			LOG_ERR("Wi-Fi connection failed: %d", result->status);
		} else {
			LOG_INF("Wi-Fi connected; waiting for DHCP");
		}
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("Wi-Fi disconnected");
	} else if (event == NET_EVENT_IPV4_DHCP_BOUND) {
		char address[NET_IPV4_ADDR_LEN];
		net_addr_ntop(NET_AF_INET, &iface->config.dhcpv4.requested_ip,
			      address, sizeof(address));
		LOG_INF("Humidifier IPv4 address: %s", address);
	}
}

static int connectSavedWifi(void)
{
	/* Keep the credentials alive throughout the asynchronous connection request. */
	static char ssid[sizeof(Credentials::ssid)] = {};
	static char password[sizeof(Credentials::password)] = {};
	static struct wifi_connect_req_params params = {};
	struct net_if *iface = net_if_get_wifi_sta();
	if (iface == NULL) {
		return -ENODEV;
	}

	humidifier->getCredentials(ssid, password);
	if (ssid[0] == '\0') {
		LOG_ERR("No saved Wi-Fi SSID");
		return -EINVAL;
	}

	net_mgmt_init_event_callback(&wifiCallback, wifiEventHandler,
		NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifiCallback);
	net_mgmt_init_event_callback(&addressCallback, wifiEventHandler, NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_add_event_callback(&addressCallback);

	params.ssid = reinterpret_cast<const uint8_t *>(ssid);
	params.ssid_length = strlen(ssid);
	params.psk = reinterpret_cast<const uint8_t *>(password);
	params.psk_length = strlen(password);
	params.security = params.psk_length ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
	params.mfp = WIFI_MFP_OPTIONAL;
	params.timeout = 30;
	net_dhcpv4_start(iface);
	LOG_INF("Connecting using saved Wi-Fi credentials");
	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}
#endif

int main(void)
{
	LOG_INF("Starting humidifier");
	static Humidifier controller;
	humidifier = &controller;

	int ret = init_usb();
	if (ret < 0) {
		LOG_ERR("USB network initialization failed: %d", ret);
		return ret;
	}

	setup_tls();
	ret = http_server_start();
	if (ret < 0) {
		LOG_ERR("HTTP server startup failed: %d", ret);
		return ret;
	}
	LOG_INF("HTTP server started");

#if defined(CONFIG_WIFI)
	ret = connectSavedWifi();
	if (ret < 0) {
		LOG_ERR("Unable to start Wi-Fi connection: %d", ret);
		return ret;
	}
#endif
	return 0;
}
