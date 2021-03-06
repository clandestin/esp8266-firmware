/*
 * dhap_pages.h
 *
 * Copyright 2015 DeviceHive
 *
 * Author: Nikolay Khabarov
 *
 */

#include <c_types.h>
#include <osapi.h>
#include <mem.h>
#include "dhap_pages.h"
#include "snprintf.h"
#include "dhsettings.h"
#include "rand.h"

#define DHAP_PAGE_TITLE_META  "<title>DeviceHive ESP8266 Configuration</title><meta name='viewport' content='width=device-width, initial-scale=1.0'>"
#define DHAP_PAGE_HTTP_HEADER "HTTP/1.0 %u %s\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %u\r\n\r\n"
#define DHAP_PAGE_ERROR "<html><head>"DHAP_PAGE_TITLE_META"</head><body><font color='red'>%s<br>Go <a href='javascript:history.back()'>back</a> to try again.</font></body></html>"
#define DHAP_PAGE_OK "<html><head>"DHAP_PAGE_TITLE_META"</head><body><font color='green'>Configuration was saved. System will reboot shortly.</font></body></html>"
#define DHAP_PAGE_FORM "<html>" \
							"<head>"DHAP_PAGE_TITLE_META\
							"<style type='text/css'>input{width:100%%;}input[type=submit]{width:30%%;}</style></head>"\
							"<body><form method='post'>"\
								"Wi-Fi SSID:<br>"\
								"<input type='text' name='ssid' value='%s'><br><br>"\
								"Wi-Fi Password(leave empty to keep current):<br>"\
								"<input type='password' name='pass'><br><br>"\
								"DeviceHive API Url:<br>"\
								"<input type='text' name='url' value='%s'><br><br>"\
								"DeviceId (allowed chars are A-Za-z0-9_-):<br>"\
								"<input type='text' name='id' value='%s'><br><br>"\
								"DeviceKey (leave empty to keep current, do not use &#92; and &quot; chars):<br>"\
								"<input type='password' name='key'><br><br>"\
								"<input type='submit' value='Apply'>"\
							"</form></body>"\
						"</html>"
#define DHAP_PAGE_MAX_SIZE 4096
LOCAL char *mPageBuffer = 0;

LOCAL char * ICACHE_FLASH_ATTR init() {
	if(mPageBuffer == 0)
		mPageBuffer = (char *)os_malloc(DHAP_PAGE_MAX_SIZE);
	return mPageBuffer;
}

char * ICACHE_FLASH_ATTR dhap_pages_error(const char *error, unsigned int *len) {
	if(init() == 0)
		return 0;
	const unsigned int cl = sizeof(DHAP_PAGE_ERROR) - 3 + os_strlen(error);
	*len = snprintf(mPageBuffer, DHAP_PAGE_MAX_SIZE, DHAP_PAGE_HTTP_HEADER""DHAP_PAGE_ERROR, 400, "Bad Request", cl, error);
	return mPageBuffer;
}

char * ICACHE_FLASH_ATTR dhap_pages_ok(unsigned int *len) {
	if(init() == 0)
		return 0;
	*len = snprintf(mPageBuffer, DHAP_PAGE_MAX_SIZE, DHAP_PAGE_HTTP_HEADER""DHAP_PAGE_OK, 200, "OK", sizeof(DHAP_PAGE_OK) - 1);
	return mPageBuffer;
}

LOCAL void esc_cpystr(char *dst, const char *src) {
	const char esc_quote[] = "&#39;";
	while(*src) {
		if(*src == '\'')
			dst += snprintf(dst, sizeof(esc_quote), "%s", esc_quote);
		else
			*dst++ = *src;
		src++;
	}
	*dst = 0;
}

LOCAL unsigned int esc_len(const char *str) {
	int res = 0;
	while(*str) {
		res++;
		if(*str == '\'')
			res += 4;
		str++;
	}
	return res;
}

char * ICACHE_FLASH_ATTR dhap_pages_form(unsigned int *len) {
	if(init() == 0)
		return 0;
	const char http[] = "http://";
	const char *ssid = dhsettings_get_wifi_ssid();
	const char *server = dhsettings_get_devicehive_server();
	const char *deviceid = dhsettings_get_devicehive_deviceid();
	unsigned int esc_ssid_len = esc_len(ssid);
	unsigned int esc_server_len = esc_len(server);
	unsigned int esc_deviceid_len = esc_len(deviceid);
	if(server[0] == 0)
		esc_server_len = esc_len(http);
	if(deviceid[0] == 0)
		esc_deviceid_len = rand_generate_deviceid(0);
	char esc_ssid[esc_ssid_len + 1];
	char esc_server[esc_server_len + 1];
	char esc_deviceid[esc_deviceid_len + 1];
	esc_cpystr(esc_ssid, ssid);
	if(server[0])
		esc_cpystr(esc_server, server);
	else
		esc_cpystr(esc_server, http);
	if(deviceid[0])
		esc_cpystr(esc_deviceid, deviceid);
	else
		esc_deviceid_len = rand_generate_deviceid(esc_deviceid);
	const unsigned int cl = sizeof(DHAP_PAGE_FORM) - 9 + esc_ssid_len + esc_server_len + esc_deviceid_len;
	*len = snprintf(mPageBuffer, DHAP_PAGE_MAX_SIZE, DHAP_PAGE_HTTP_HEADER""DHAP_PAGE_FORM, 200, "OK", cl, esc_ssid, esc_server, esc_deviceid);
	return mPageBuffer;
}
