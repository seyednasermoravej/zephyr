#!/usr/bin/env python3
"""Run the production /piezos callback against a host-side HTTP/firmware double.

Usage: python3 tests/backend/test_piezos_handler.py
Requires a C++ compiler; no board or Zephyr SDK is needed.
"""

from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/main.cpp").read_text()
start = source.index("static int piezosHandler(")
end = source.index("static struct http_resource_detail_dynamic piezosResourceDetail", start)
handler = source[start:end]

harness = r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_ERR(...) ((void)0)
enum http_method { HTTP_GET, HTTP_POST };
enum http_transaction_status {
    HTTP_SERVER_TRANSACTION_ABORTED = -1,
    HTTP_SERVER_REQUEST_DATA_MORE = 0,
    HTTP_SERVER_REQUEST_DATA_FINAL = 1,
    HTTP_SERVER_TRANSACTION_COMPLETE = 2,
};
enum http_status {
    HTTP_200_OK = 200,
    HTTP_400_BAD_REQUEST = 400,
    HTTP_413_PAYLOAD_TOO_LARGE = 413,
    HTTP_500_INTERNAL_SERVER_ERROR = 500,
};
struct http_client_ctx { http_method method; };
struct http_request_ctx { uint8_t *data; size_t data_len; };
struct http_response_ctx {
    http_status status;
    const uint8_t *body;
    size_t body_len;
    bool final_chunk;
};
struct Humidifier {
    int parseCalls = 0, statusCalls = 0, parseResult = 0, statusResult = 0;
    std::string lastPayload;
    const char *canonical = "{\"active\":1,\"piezos\":[{\"brightness\":50,\"intensity\":100,\"frequencyHz\":1800000}]}";
    int parsePiezosPost(char *buf, size_t len) {
        ++parseCalls;
        assert(buf[len] == '\0');
        lastPayload.assign(buf, len);
        return parseResult;
    }
    int piezosStatus(char *buf, size_t len) {
        ++statusCalls;
        if (statusResult < 0) return statusResult;
        assert(std::strlen(canonical) < len);
        std::strcpy(buf, canonical);
        return 0;
    }
};
static Humidifier device;
static Humidifier *humidifier = &device;
'''

checks = r'''
static http_client_ctx client{HTTP_POST};
static http_response_ctx send(http_transaction_status status, const std::string &data = "") {
    http_request_ctx request{reinterpret_cast<uint8_t *>(const_cast<char *>(data.data())), data.size()};
    http_response_ctx response{};
    assert(piezosHandler(&client, status, &request, &response, nullptr) == 0);
    return response;
}
static void terminal(http_transaction_status status = HTTP_SERVER_TRANSACTION_COMPLETE) {
    // Zephyr terminal callbacks are notifications and need no request/response access.
    assert(piezosHandler(&client, status, nullptr, nullptr, nullptr) == 0);
}
static void reset() {
    terminal(HTTP_SERVER_TRANSACTION_ABORTED);
    device = Humidifier{};
    client.method = HTTP_POST;
}
static void expectStatus(http_response_ctx response, http_status status) {
    assert(response.status == status);
    assert(response.final_chunk);
    assert(response.body != nullptr);
    assert(response.body_len == std::strlen(reinterpret_cast<const char *>(response.body)));
}
int main() {
    reset();
    auto response = send(HTTP_SERVER_REQUEST_DATA_MORE, "{\"piezoNum\":1,");
    assert(!response.final_chunk && response.body == nullptr && device.parseCalls == 0);
    response = send(HTTP_SERVER_REQUEST_DATA_FINAL, "\"intensity\":100,\"frequencyHz\":1800000}");
    expectStatus(response, HTTP_200_OK);
    assert(device.parseCalls == 1 && device.statusCalls == 1);
    assert(device.lastPayload == "{\"piezoNum\":1,\"intensity\":100,\"frequencyHz\":1800000}");
    assert(std::string(reinterpret_cast<const char *>(response.body)) == device.canonical);
    terminal();
    assert(device.parseCalls == 1 && device.statusCalls == 1);

    // A shorter next request must not retain a previous request's suffix.
    response = send(HTTP_SERVER_REQUEST_DATA_FINAL, "{\"piezoNum\":-1}");
    expectStatus(response, HTTP_200_OK);
    assert(device.lastPayload == "{\"piezoNum\":-1}");
    terminal();

    reset();
    client.method = HTTP_GET;
    assert(!send(HTTP_SERVER_REQUEST_DATA_MORE).final_chunk);
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL), HTTP_200_OK);
    terminal();
    assert(device.parseCalls == 0 && device.statusCalls == 1);
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL), HTTP_200_OK);
    assert(device.statusCalls == 2);
    terminal();

    // Validation and firmware/storage errors are distinct HTTP errors.
    for (int error : {-EINVAL, -ERANGE, -EIO, -ENOSPC}) {
        reset();
        device.parseResult = error;
        expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL, "invalid"),
                     (error == -EINVAL || error == -ERANGE)
                         ? HTTP_400_BAD_REQUEST : HTTP_500_INTERNAL_SERVER_ERROR);
        assert(device.parseCalls == 1 && device.statusCalls == 0);
        terminal();
    }
    reset();
    device.statusResult = -ENOMEM;
    client.method = HTTP_GET;
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL), HTTP_500_INTERNAL_SERVER_ERROR);
    terminal();

    // 255 bytes fit, but the 256th byte belongs to the mandatory terminator.
    reset();
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL, std::string(255, ' ')), HTTP_200_OK);
    assert(device.lastPayload.size() == 255);
    terminal();
    reset();
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL, std::string(256, ' ')), HTTP_413_PAYLOAD_TOO_LARGE);
    assert(device.parseCalls == 0 && device.statusCalls == 0);
    terminal();

    reset();
    assert(!send(HTTP_SERVER_REQUEST_DATA_MORE, std::string(255, ' ')).final_chunk);
    assert(!send(HTTP_SERVER_REQUEST_DATA_MORE, " ").final_chunk);
    // Draining later fragments must never make an oversized request valid again.
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL, "{\"piezoNum\":0}"), HTTP_413_PAYLOAD_TOO_LARGE);
    assert(device.parseCalls == 0);
    terminal();
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL, "{\"piezoNum\":0}"), HTTP_200_OK);
    assert(device.parseCalls == 1);
    terminal();

    reset();
    std::string nulBody("{}\0{}", 5);
    expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL, nulBody), HTTP_400_BAD_REQUEST);
    assert(device.parseCalls == 0);
    terminal();
    reset();
    http_request_ctx nullRequest{nullptr, 1};
    http_response_ctx nullResponse{};
    assert(piezosHandler(&client, HTTP_SERVER_REQUEST_DATA_FINAL, &nullRequest,
                         &nullResponse, nullptr) == 0);
    expectStatus(nullResponse, HTTP_400_BAD_REQUEST);
    assert(device.parseCalls == 0);
    terminal();

    // Disconnects must discard both accumulated data and overflow/error state.
    for (const auto &partial : {std::string("{\"piezoNum\":"), std::string(256, ' '), nulBody}) {
        reset();
        send(HTTP_SERVER_REQUEST_DATA_MORE, partial);
        terminal(HTTP_SERVER_TRANSACTION_ABORTED);
        expectStatus(send(HTTP_SERVER_REQUEST_DATA_FINAL, "{\"piezoNum\":2}"), HTTP_200_OK);
        assert(device.parseCalls == 1 && device.lastPayload == "{\"piezoNum\":2}");
        terminal();
    }
    std::puts("/piezos callback tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix="humidifier-http-test-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(harness + handler + checks)
    subprocess.run(
        [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
         "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(cpp), "-o", str(binary)], check=True,
    )
    subprocess.run([str(binary)], check=True)
