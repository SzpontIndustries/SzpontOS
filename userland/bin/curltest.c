/*
 * SzpontOS - /usr/tbin/curltest (cURL Diagnostic Tool)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

static int debug_callback(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr) {
    (void)handle;
    (void)userptr;
    const char *prefix = "*";
    switch (type) {
    case CURLINFO_TEXT:
        prefix = "== Info";
        break;
    case CURLINFO_HEADER_OUT:
        prefix = "=> Send header";
        break;
    case CURLINFO_DATA_OUT:
        prefix = "=> Send data";
        break;
    case CURLINFO_SSL_DATA_OUT:
        prefix = "=> Send SSL data";
        break;
    case CURLINFO_HEADER_IN:
        prefix = "<= Recv header";
        break;
    case CURLINFO_DATA_IN:
        prefix = "<= Recv data";
        break;
    case CURLINFO_SSL_DATA_IN:
        prefix = "<= Recv SSL data";
        break;
    default:
        return 0;
    }
    printf("[%s (%zu bytes)]: %.*s\n", prefix, size, (int)size, data);
    return 0;
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)userp;
    size_t realsize = size * nmemb;
    printf("[curltest received %zu bytes]:\n%.*s\n", realsize, (int)realsize, (char *)contents);
    return realsize;
}

int main(int argc, char **argv) {
    const char *url = (argc > 1) ? argv[1] : "http://127.0.0.1/";

    printf("[1] Calling curl_global_init()...\n");
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_global_init() failed: %s\n", curl_easy_strerror(res));
        return 1;
    }
    printf("[2] curl_global_init() SUCCESS!\n");

    printf("[3] Calling curl_easy_init()...\n");
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init() failed!\n");
        return 1;
    }
    printf("[4] curl_easy_init() SUCCESS!\n");

    printf("[5] Setting options for URL: %s...\n", url);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, debug_callback);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);

    printf("[6] Calling curl_easy_perform()...\n");
    res = curl_easy_perform(curl);
    printf("[7] curl_easy_perform() returned code: %d (%s)\n", res, curl_easy_strerror(res));

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return (res == CURLE_OK) ? 0 : 1;
}
