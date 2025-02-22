/*
 * hdhomerun_device.c
 *
 * Copyright © 2006-2022 Silicondust USA Inc. <www.silicondust.com>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "hdhomerun.h"

struct hdhomerun_device_t {
	struct hdhomerun_control_sock_t *cs;
	struct hdhomerun_video_sock_t *vs;
	struct hdhomerun_debug_t *dbg;
	struct hdhomerun_channelscan_t *scan;
	struct sockaddr_storage multicast_addr;
	uint32_t device_id;
	unsigned int tuner;
	uint32_t lockkey;
	char name[32];
	char model[32];
};

int hdhomerun_device_set_device(struct hdhomerun_device_t *hd, uint32_t device_id, uint32_t device_ip)
{
	struct sockaddr_in device_addr;
	memset(&device_addr, 0, sizeof(device_addr));
	device_addr.sin_family = AF_INET;
	device_addr.sin_addr.s_addr = htonl(device_ip);

	return hdhomerun_device_set_device_ex(hd, device_id, (const struct sockaddr *)&device_addr);
}

int hdhomerun_device_set_device_ex(struct hdhomerun_device_t *hd, uint32_t device_id, const struct sockaddr *device_addr)
{
	if ((device_id == 0) && !hdhomerun_sock_sockaddr_is_addr(device_addr)) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_device: device not specified\n");
		return -1;
	}

	if (hdhomerun_sock_sockaddr_is_multicast(device_addr)) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_device: invalid address\n");
		return -1;
	}

	if (!hd->cs) {
		hd->cs = hdhomerun_control_create(0, 0, hd->dbg);
		if (!hd->cs) {
			hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_device: failed to create control object\n");
			return -1;
		}
	}

	hdhomerun_control_set_device_ex(hd->cs, device_id, device_addr);

	if ((device_id == 0) || (device_id == HDHOMERUN_DEVICE_ID_WILDCARD)) {
		device_id = hdhomerun_control_get_device_id(hd->cs);
	}

	memset(&hd->multicast_addr, 0, sizeof(hd->multicast_addr));
	hd->device_id = device_id;
	hd->tuner = 0;
	hd->lockkey = 0;

	hdhomerun_sprintf(hd->name, hd->name + sizeof(hd->name), "%08X-%u", (unsigned int)hd->device_id, hd->tuner);
	hdhomerun_sprintf(hd->model, hd->model + sizeof(hd->model), ""); /* clear cached model string */

	return 1;
}

int hdhomerun_device_set_multicast(struct hdhomerun_device_t *hd, uint32_t multicast_ip, uint16_t multicast_port)
{
	struct sockaddr_in multicast_addr;
	memset(&multicast_addr, 0, sizeof(multicast_addr));
	multicast_addr.sin_family = AF_INET;
	multicast_addr.sin_addr.s_addr = htonl(multicast_ip);
	multicast_addr.sin_port = htons(multicast_port);

	return hdhomerun_device_set_multicast_ex(hd, (const struct sockaddr *)&multicast_addr);
}

int hdhomerun_device_set_multicast_ex(struct hdhomerun_device_t *hd, const struct sockaddr *multicast_addr)
{
	if (!hdhomerun_sock_sockaddr_is_multicast(multicast_addr)) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_device_multicast: invalid address\n");
		return -1;
	}

	uint16_t multicast_port = hdhomerun_sock_sockaddr_get_port(multicast_addr);
	if (multicast_port == 0) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_device_multicast: invalid port %u\n", (unsigned int)multicast_port);
		return -1;
	}

	if (hd->cs) {
		hdhomerun_control_destroy(hd->cs);
		hd->cs = NULL;
	}

	hdhomerun_sock_sockaddr_copy(&hd->multicast_addr, multicast_addr);
	hd->device_id = 0;
	hd->tuner = 0;
	hd->lockkey = 0;

	hdhomerun_sprintf(hd->name, hd->name + sizeof(hd->name), "multicast:%u", (unsigned int)multicast_port);
	hdhomerun_sprintf(hd->model, hd->model + sizeof(hd->model), "multicast");

	return 1;
}

int hdhomerun_device_set_tuner(struct hdhomerun_device_t *hd, unsigned int tuner)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		if (tuner != 0) {
			hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner: tuner cannot be specified in multicast mode\n");
			return -1;
		}

		return 1;
	}

	hd->tuner = tuner;
	hdhomerun_sprintf(hd->name, hd->name + sizeof(hd->name), "%08X-%u", (unsigned int)hd->device_id, hd->tuner);

	return 1;
}

int hdhomerun_device_set_tuner_from_str(struct hdhomerun_device_t *hd, const char *tuner_str)
{
	unsigned int tuner;
	if (sscanf(tuner_str, "%u", &tuner) == 1) {
		hdhomerun_device_set_tuner(hd, tuner);
		return 1;
	}
	if (sscanf(tuner_str, "/tuner%u", &tuner) == 1) {
		hdhomerun_device_set_tuner(hd, tuner);
		return 1;
	}

	return -1;
}

static struct hdhomerun_device_t *hdhomerun_device_create_internal(struct hdhomerun_debug_t *dbg)
{
	struct hdhomerun_device_t *hd = (struct hdhomerun_device_t *)calloc(1, sizeof(struct hdhomerun_device_t));
	if (!hd) {
		hdhomerun_debug_printf(dbg, "hdhomerun_device_create: failed to allocate device object\n");
		return NULL;
	}

	hd->dbg = dbg;
	return hd;
}

struct hdhomerun_device_t *hdhomerun_device_create(uint32_t device_id, uint32_t device_ip, unsigned int tuner, struct hdhomerun_debug_t *dbg)
{
	struct sockaddr_in device_addr;
	memset(&device_addr, 0, sizeof(device_addr));
	device_addr.sin_family = AF_INET;
	device_addr.sin_addr.s_addr = htonl(device_ip);

	return hdhomerun_device_create_ex(device_id, (const struct sockaddr *)&device_addr, tuner, dbg);
}

struct hdhomerun_device_t *hdhomerun_device_create_ex(uint32_t device_id, const struct sockaddr *device_addr, unsigned int tuner, struct hdhomerun_debug_t *dbg)
{
	if ((device_id != 0) && !hdhomerun_discover_validate_device_id(device_id)) {
		return NULL;
	}

	struct hdhomerun_device_t *hd = hdhomerun_device_create_internal(dbg);
	if (!hd) {
		return NULL;
	}

	if ((device_id == 0) && !hdhomerun_sock_sockaddr_is_addr(device_addr) && (tuner == 0)) {
		return hd;
	}

	if (hdhomerun_device_set_device_ex(hd, device_id, device_addr) <= 0) {
		free(hd);
		return NULL;
	}
	if (hdhomerun_device_set_tuner(hd, tuner) <= 0) {
		free(hd);
		return NULL;
	}

	return hd;
}

struct hdhomerun_device_t *hdhomerun_device_create_multicast(uint32_t multicast_ip, uint16_t multicast_port, struct hdhomerun_debug_t *dbg)
{
	struct sockaddr_in multicast_addr;
	memset(&multicast_addr, 0, sizeof(multicast_addr));
	multicast_addr.sin_family = AF_INET;
	multicast_addr.sin_addr.s_addr = htonl(multicast_ip);
	multicast_addr.sin_port = htons(multicast_port);

	return hdhomerun_device_create_multicast_ex((const struct sockaddr *)&multicast_addr, dbg);
}

struct hdhomerun_device_t *hdhomerun_device_create_multicast_ex(const struct sockaddr *multicast_addr, struct hdhomerun_debug_t *dbg)
{
	struct hdhomerun_device_t *hd = hdhomerun_device_create_internal(dbg);
	if (!hd) {
		return NULL;
	}

	if (hdhomerun_device_set_multicast_ex(hd, multicast_addr) <= 0) {
		free(hd);
		return NULL;
	}

	return hd;
}

void hdhomerun_device_destroy(struct hdhomerun_device_t *hd)
{
	if (hd->scan) {
		channelscan_destroy(hd->scan);
	}

	if (hd->vs) {
		hdhomerun_video_destroy(hd->vs);
	}

	if (hd->cs) {
		hdhomerun_control_destroy(hd->cs);
	}

	free(hd);
}

static bool hdhomerun_device_create_from_str_parse_device_id(const char *name, uint32_t *pdevice_id)
{
	char *end;
	uint32_t device_id = (uint32_t)strtoul(name, &end, 16);
	if (end != name + 8) {
		return false;
	}

	if (*end != 0) {
		return false;
	}

	*pdevice_id = device_id;
	return true;
}

static bool hdhomerun_device_create_from_str_parse_dns(const char *name, struct sockaddr_storage *device_addr)
{
	const char *ptr = name;
	if (*ptr == 0) {
		return false;
	}

	while (1) {
		char c = *ptr++;
		if (c == 0) {
			break;
		}

		if ((c >= '0') && (c <= '9')) {
			continue;
		}
		if ((c >= 'a') && (c <= 'z')) {
			continue;
		}
		if ((c >= 'A') && (c <= 'Z')) {
			continue;
		}
		if ((c == '.') || (c == '-')) {
			continue;
		}

		return false;
	}

	return hdhomerun_sock_getaddrinfo_addr_ex(AF_INET, name, device_addr);
}

static struct hdhomerun_device_t *hdhomerun_device_create_from_str_tail(const char *tail, uint32_t device_id, struct sockaddr_storage *device_addr, struct hdhomerun_debug_t *dbg)
{
	const char *ptr = tail;
	if (*ptr == 0) {
		return hdhomerun_device_create_ex(device_id, (struct sockaddr *)device_addr, 0, dbg);
	}

	if (*ptr == ':') {
		ptr++;

		char *end;
		unsigned long port = strtoul(ptr + 1, &end, 10);
		if (*end != 0) {
			return NULL;
		}
		if ((port < 1024) || (port > 65535)) {
			return NULL;
		}

		if (device_addr->ss_family == AF_INET) {
			struct sockaddr_in *device_addr_in = (struct sockaddr_in *)device_addr;
			device_addr_in->sin_port = htons((uint16_t)port);
			return hdhomerun_device_create_multicast_ex((struct sockaddr *)device_addr, dbg);
		}

		if (device_addr->ss_family == AF_INET6) {
			struct sockaddr_in6 *device_addr_in = (struct sockaddr_in6 *)device_addr;
			device_addr_in->sin6_port = htons((uint16_t)port);
			return hdhomerun_device_create_multicast_ex((struct sockaddr *)device_addr, dbg);
		}

		return NULL;
	}

	if (*ptr == '-') {
		ptr++;

		char *end;
		unsigned int tuner_index = (unsigned int)strtoul(ptr, &end, 10);
		if (*end != 0) {
			return NULL;
		}

		return hdhomerun_device_create_ex(device_id, (struct sockaddr *)device_addr, tuner_index, dbg);
	}

	return NULL;
}

struct hdhomerun_device_t *hdhomerun_device_create_from_str(const char *device_str, struct hdhomerun_debug_t *dbg)
{
	char str[64];
	if (!hdhomerun_sprintf(str, str + sizeof(str), "%s", device_str)) {
		return NULL;
	}

	uint32_t device_id = HDHOMERUN_DEVICE_ID_WILDCARD;
	struct sockaddr_storage device_addr;
	device_addr.ss_family = 0;

	char *ptr = str;
	bool framed = (*ptr == '[');
	if (framed) {
		ptr++;

		char *end = strchr(ptr, ']');
		if (!end) {
			return NULL;
		}

		*end++ = 0;

		if (hdhomerun_sock_ip_str_to_sockaddr(ptr, &device_addr)) {
			return hdhomerun_device_create_from_str_tail(end, device_id, &device_addr, dbg);
		}
		
		return NULL;
	}
	
	char *dash = strchr(ptr, '-');
	if (dash) {
		*dash = 0;

		if (hdhomerun_device_create_from_str_parse_device_id(ptr, &device_id)) {
			*dash = '-';
			return hdhomerun_device_create_from_str_tail(dash, device_id, &device_addr, dbg);
		}
		if (hdhomerun_sock_ip_str_to_sockaddr(ptr, &device_addr)) {
			*dash = '-';
			return hdhomerun_device_create_from_str_tail(dash, device_id, &device_addr, dbg);
		}

		*dash = '-';
		if (hdhomerun_device_create_from_str_parse_dns(ptr, &device_addr)) {
			return hdhomerun_device_create_ex(device_id, (struct sockaddr *)&device_addr, 0, dbg);
		}

		return NULL;
	}

	char *colon = strchr(ptr, ':');
	if (colon) {
		char *second_colon = strchr(colon, ':');
		if (second_colon) {
			if (hdhomerun_sock_ip_str_to_sockaddr(ptr, &device_addr)) {
				return hdhomerun_device_create_ex(device_id, (struct sockaddr *)&device_addr, 0, dbg);
			}

			return NULL;
		}

		*colon = 0;

		if (hdhomerun_sock_ip_str_to_sockaddr(ptr, &device_addr)) {
			*colon = ':';
			return hdhomerun_device_create_from_str_tail(colon, device_id, &device_addr, dbg);
		}

		return NULL;
	}

	if (hdhomerun_device_create_from_str_parse_device_id(ptr, &device_id)) {
		return hdhomerun_device_create_ex(device_id, (struct sockaddr *)&device_addr, 0, dbg);
	}
	if (hdhomerun_sock_ip_str_to_sockaddr(ptr, &device_addr)) {
		return hdhomerun_device_create_ex(device_id, (struct sockaddr *)&device_addr, 0, dbg);
	}
	if (hdhomerun_device_create_from_str_parse_dns(ptr, &device_addr)) {
		return hdhomerun_device_create_ex(device_id, (struct sockaddr *)&device_addr, 0, dbg);
	}

	return NULL;
}

const char *hdhomerun_device_get_name(struct hdhomerun_device_t *hd)
{
	return hd->name;
}

uint32_t hdhomerun_device_get_device_id(struct hdhomerun_device_t *hd)
{
	return hd->device_id;
}

uint32_t hdhomerun_device_get_device_ip(struct hdhomerun_device_t *hd)
{
	struct sockaddr_storage device_addr;
	if (!hdhomerun_device_get_device_addr(hd, &device_addr)) {
		return 0;
	}
	if (device_addr.ss_family != AF_INET) {
		return 0;
	}

	struct sockaddr_in *device_addr_in = (struct sockaddr_in *)&device_addr;
	return ntohl(device_addr_in->sin_addr.s_addr);
}

bool hdhomerun_device_get_device_addr(struct hdhomerun_device_t *hd, struct sockaddr_storage *result)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		*result = hd->multicast_addr;
		return true;
	}

	if (!hd->cs) {
		memset(result, 0, sizeof(struct sockaddr_storage));
		return false;
	}

	return hdhomerun_control_get_device_addr(hd->cs, result);
}

uint32_t hdhomerun_device_get_device_id_requested(struct hdhomerun_device_t *hd)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		return 0;
	}

	if (!hd->cs) {
		return 0;
	}

	return hdhomerun_control_get_device_id_requested(hd->cs);
}

uint32_t hdhomerun_device_get_device_ip_requested(struct hdhomerun_device_t *hd)
{
	struct sockaddr_storage device_addr;
	if (!hdhomerun_device_get_device_addr_requested(hd, &device_addr)) {
		return 0;
	}
	if (device_addr.ss_family != AF_INET) {
		return 0;
	}

	struct sockaddr_in *device_addr_in = (struct sockaddr_in *)&device_addr;
	return ntohl(device_addr_in->sin_addr.s_addr);
}

bool hdhomerun_device_get_device_addr_requested(struct hdhomerun_device_t *hd, struct sockaddr_storage *result)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		*result = hd->multicast_addr;
		return true;
	}

	if (!hd->cs) {
		memset(result, 0, sizeof(struct sockaddr_storage));
		return false;
	}

	return hdhomerun_control_get_device_addr_requested(hd->cs, result);
}

unsigned int hdhomerun_device_get_tuner(struct hdhomerun_device_t *hd)
{
	return hd->tuner;
}

struct hdhomerun_control_sock_t *hdhomerun_device_get_control_sock(struct hdhomerun_device_t *hd)
{
	return hd->cs;
}

struct hdhomerun_video_sock_t *hdhomerun_device_get_video_sock(struct hdhomerun_device_t *hd)
{
	if (hd->vs) {
		return hd->vs;
	}

	bool allow_port_reuse = false;
	struct sockaddr_storage listen_addr;
	memset(&listen_addr, 0, sizeof(listen_addr));

	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		listen_addr.ss_family = hd->multicast_addr.ss_family;
		hdhomerun_sock_sockaddr_set_port((struct sockaddr *)&listen_addr, hdhomerun_sock_sockaddr_get_port((struct sockaddr *)&hd->multicast_addr));
		allow_port_reuse = true;
	}

	struct sockaddr_storage device_addr;
	if (!hdhomerun_control_get_device_addr(hd->cs, &device_addr)) {
		return NULL;
	}

	listen_addr.ss_family = device_addr.ss_family;

	hd->vs = hdhomerun_video_create_ex((struct sockaddr *)&listen_addr, allow_port_reuse, VIDEO_DATA_BUFFER_SIZE_1S * 2, hd->dbg);
	if (!hd->vs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_video_sock: failed to create video object\n");
		return NULL;
	}

	return hd->vs;
}

uint32_t hdhomerun_device_get_local_machine_addr(struct hdhomerun_device_t *hd)
{
	struct sockaddr_storage local_addr;
	if (!hdhomerun_device_get_local_machine_addr_ex(hd, &local_addr)) {
		return 0;
	}
	if (local_addr.ss_family != AF_INET) {
		return 0;
	}

	struct sockaddr_in *local_addr_in = (struct sockaddr_in *)&local_addr;
	return ntohl(local_addr_in->sin_addr.s_addr);
}

bool hdhomerun_device_get_local_machine_addr_ex(struct hdhomerun_device_t *hd, struct sockaddr_storage *result)
{
	if (!hd->cs) {
		memset(result, 0, sizeof(struct sockaddr_storage));
		return false;
	}

	return hdhomerun_control_get_local_addr_ex(hd->cs, result);
}

static uint32_t hdhomerun_device_get_status_parse(const char *status_str, const char *tag)
{
	const char *ptr = strstr(status_str, tag);
	if (!ptr) {
		return 0;
	}

	unsigned int value = 0;
	(void)sscanf(ptr + strlen(tag), "%u", &value);

	return (uint32_t)value;
}

static bool hdhomerun_device_get_tuner_status_lock_is_bcast(struct hdhomerun_tuner_status_t *status)
{
	if (strcmp(status->lock_str, "8vsb") == 0) {
		return true;
	}
	if (strcmp(status->lock_str, "atsc3") == 0) {
		return true;
	}
	if (strncmp(status->lock_str, "t8", 2) == 0) {
		return true;
	}
	if (strncmp(status->lock_str, "t7", 2) == 0) {
		return true;
	}
	if (strncmp(status->lock_str, "t6", 2) == 0) {
		return true;
	}

	return false;
}

uint32_t hdhomerun_device_get_tuner_status_ss_color(struct hdhomerun_tuner_status_t *status)
{
	unsigned int ss_yellow_min;
	unsigned int ss_green_min;

	if (!status->lock_supported) {
		return HDHOMERUN_STATUS_COLOR_NEUTRAL;
	}

	if (hdhomerun_device_get_tuner_status_lock_is_bcast(status)) {
		ss_yellow_min = 50;	/* -30dBmV */
		ss_green_min = 75;	/* -15dBmV */
	} else {
		ss_yellow_min = 80;	/* -12dBmV */
		ss_green_min = 90;	/* -6dBmV */
	}

	if (status->signal_strength >= ss_green_min) {
		return HDHOMERUN_STATUS_COLOR_GREEN;
	}
	if (status->signal_strength >= ss_yellow_min) {
		return HDHOMERUN_STATUS_COLOR_YELLOW;
	}

	return HDHOMERUN_STATUS_COLOR_RED;
}

uint32_t hdhomerun_device_get_tuner_status_snq_color(struct hdhomerun_tuner_status_t *status)
{
	if (status->signal_to_noise_quality >= 70) {
		return HDHOMERUN_STATUS_COLOR_GREEN;
	}
	if (status->signal_to_noise_quality >= 50) {
		return HDHOMERUN_STATUS_COLOR_YELLOW;
	}

	return HDHOMERUN_STATUS_COLOR_RED;
}

uint32_t hdhomerun_device_get_tuner_status_seq_color(struct hdhomerun_tuner_status_t *status)
{
	if (status->symbol_error_quality >= 100) {
		return HDHOMERUN_STATUS_COLOR_GREEN;
	}

	return HDHOMERUN_STATUS_COLOR_RED;
}

int hdhomerun_device_get_tuner_status(struct hdhomerun_device_t *hd, char **pstatus_str, struct hdhomerun_tuner_status_t *status)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_status: device not set\n");
		return -1;
	}

	memset(status, 0, sizeof(struct hdhomerun_tuner_status_t));

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/status", hd->tuner);

	char *status_str;
	int ret = hdhomerun_control_get(hd->cs, name, &status_str, NULL);
	if (ret <= 0) {
		return ret;
	}

	if (pstatus_str) {
		*pstatus_str = status_str;
	}

	if (status) {
		char *channel = strstr(status_str, "ch=");
		if (channel) {
			(void)sscanf(channel + 3, "%31s", status->channel);
		}

		char *lock = strstr(status_str, "lock=");
		if (lock) {
			(void)sscanf(lock + 5, "%31s", status->lock_str);
		}

		status->signal_strength = (unsigned int)hdhomerun_device_get_status_parse(status_str, "ss=");
		status->signal_to_noise_quality = (unsigned int)hdhomerun_device_get_status_parse(status_str, "snq=");
		status->symbol_error_quality = (unsigned int)hdhomerun_device_get_status_parse(status_str, "seq=");
		status->raw_bits_per_second = hdhomerun_device_get_status_parse(status_str, "bps=");
		status->packets_per_second = hdhomerun_device_get_status_parse(status_str, "pps=");

		status->signal_present = status->signal_strength >= 35;

		if (strcmp(status->lock_str, "none") != 0) {
			if (status->lock_str[0] == '(') {
				status->lock_unsupported = true;
			} else {
				status->lock_supported = true;
			}
		}
	}

	return 1;
}

int hdhomerun_device_get_oob_status(struct hdhomerun_device_t *hd, char **pstatus_str, struct hdhomerun_tuner_status_t *status)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_oob_status: device not set\n");
		return -1;
	}

	memset(status, 0, sizeof(struct hdhomerun_tuner_status_t));

	char *status_str;
	int ret = hdhomerun_control_get(hd->cs, "/oob/status", &status_str, NULL);
	if (ret <= 0) {
		return ret;
	}

	if (pstatus_str) {
		*pstatus_str = status_str;
	}

	if (status) {
		char *channel = strstr(status_str, "ch=");
		if (channel) {
			(void)sscanf(channel + 3, "%31s", status->channel);
		}

		char *lock = strstr(status_str, "lock=");
		if (lock) {
			(void)sscanf(lock + 5, "%31s", status->lock_str);
		}

		status->signal_strength = (unsigned int)hdhomerun_device_get_status_parse(status_str, "ss=");
		status->signal_to_noise_quality = (unsigned int)hdhomerun_device_get_status_parse(status_str, "snq=");
		status->signal_present = status->signal_strength >= 35;
		status->lock_supported = (strcmp(status->lock_str, "none") != 0);
	}

	return 1;
}

int hdhomerun_device_get_tuner_vstatus(struct hdhomerun_device_t *hd, char **pvstatus_str, struct hdhomerun_tuner_vstatus_t *vstatus)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_vstatus: device not set\n");
		return -1;
	}

	memset(vstatus, 0, sizeof(struct hdhomerun_tuner_vstatus_t));

	char var_name[32];
	hdhomerun_sprintf(var_name, var_name + sizeof(var_name), "/tuner%u/vstatus", hd->tuner);

	char *vstatus_str;
	int ret = hdhomerun_control_get(hd->cs, var_name, &vstatus_str, NULL);
	if (ret <= 0) {
		return ret;
	}

	if (pvstatus_str) {
		*pvstatus_str = vstatus_str;
	}

	if (vstatus) {
		char *vch = strstr(vstatus_str, "vch=");
		if (vch) {
			(void)sscanf(vch + 4, "%31s", vstatus->vchannel);
		}

		char *name = strstr(vstatus_str, "name=");
		if (name) {
			(void)sscanf(name + 5, "%31s", vstatus->name);
		}

		char *auth = strstr(vstatus_str, "auth=");
		if (auth) {
			(void)sscanf(auth + 5, "%31s", vstatus->auth);
		}

		char *cci = strstr(vstatus_str, "cci=");
		if (cci) {
			(void)sscanf(cci + 4, "%31s", vstatus->cci);
		}

		char *cgms = strstr(vstatus_str, "cgms=");
		if (cgms) {
			(void)sscanf(cgms + 5, "%31s", vstatus->cgms);
		}

		if (strncmp(vstatus->auth, "not-subscribed", 14) == 0) {
			vstatus->not_subscribed = true;
		}

		if (strncmp(vstatus->auth, "error", 5) == 0) {
			vstatus->not_available = true;
		}
		if (strncmp(vstatus->auth, "dialog", 6) == 0) {
			vstatus->not_available = true;
		}

		if (strncmp(vstatus->cci, "protected", 9) == 0) {
			vstatus->copy_protected = true;
		}
		if (strncmp(vstatus->cgms, "protected", 9) == 0) {
			vstatus->copy_protected = true;
		}
	}

	return 1;
}

int hdhomerun_device_get_tuner_plpinfo(struct hdhomerun_device_t *hd, char **pplpinfo)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_plpinfo: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/plpinfo", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, pplpinfo, NULL);
}

int hdhomerun_device_get_tuner_streaminfo(struct hdhomerun_device_t *hd, char **pstreaminfo)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_streaminfo: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/streaminfo", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, pstreaminfo, NULL);
}

int hdhomerun_device_get_tuner_channel(struct hdhomerun_device_t *hd, char **pchannel)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_channel: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/channel", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, pchannel, NULL);
}

int hdhomerun_device_get_tuner_vchannel(struct hdhomerun_device_t *hd, char **pvchannel)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_vchannel: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/vchannel", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, pvchannel, NULL);
}

int hdhomerun_device_get_tuner_channelmap(struct hdhomerun_device_t *hd, char **pchannelmap)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_channelmap: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/channelmap", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, pchannelmap, NULL);
}

int hdhomerun_device_get_tuner_filter(struct hdhomerun_device_t *hd, char **pfilter)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_filter: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/filter", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, pfilter, NULL);
}

int hdhomerun_device_get_tuner_program(struct hdhomerun_device_t *hd, char **pprogram)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_program: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/program", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, pprogram, NULL);
}

int hdhomerun_device_get_tuner_target(struct hdhomerun_device_t *hd, char **ptarget)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_target: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/target", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, ptarget, NULL);
}

static int hdhomerun_device_get_tuner_plotsample_internal(struct hdhomerun_device_t *hd, const char *name, struct hdhomerun_plotsample_t **psamples, size_t *pcount)
{
	char *result;
	int ret = hdhomerun_control_get(hd->cs, name, &result, NULL);
	if (ret <= 0) {
		return ret;
	}

	struct hdhomerun_plotsample_t *samples = (struct hdhomerun_plotsample_t *)result;
	*psamples = samples;
	size_t count = 0;

	while (1) {
		char *ptr = strchr(result, ' ');
		if (!ptr) {
			break;
		}
		*ptr++ = 0;

		unsigned int raw;
		if (sscanf(result, "%x", &raw) != 1) {
			break;
		}

		uint16_t real = (raw >> 12) & 0x0FFF;
		if (real & 0x0800) {
			real |= 0xF000;
		}

		uint16_t imag = (raw >> 0) & 0x0FFF;
		if (imag & 0x0800) {
			imag |= 0xF000;
		}

		samples->real = (int16_t)real;
		samples->imag = (int16_t)imag;
		samples++;
		count++;

		result = ptr;
	}

	*pcount = count;
	return 1;
}

int hdhomerun_device_get_tuner_plotsample(struct hdhomerun_device_t *hd, struct hdhomerun_plotsample_t **psamples, size_t *pcount)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_plotsample: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/plotsample", hd->tuner);
	return hdhomerun_device_get_tuner_plotsample_internal(hd, name, psamples, pcount);
}

int hdhomerun_device_get_oob_plotsample(struct hdhomerun_device_t *hd, struct hdhomerun_plotsample_t **psamples, size_t *pcount)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_oob_plotsample: device not set\n");
		return -1;
	}

	return hdhomerun_device_get_tuner_plotsample_internal(hd, "/oob/plotsample", psamples, pcount);
}

int hdhomerun_device_get_tuner_lockkey_owner(struct hdhomerun_device_t *hd, char **powner)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_tuner_lockkey_owner: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/lockkey", hd->tuner);
	return hdhomerun_control_get(hd->cs, name, powner, NULL);
}

int hdhomerun_device_get_ir_target(struct hdhomerun_device_t *hd, char **ptarget)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_ir_target: device not set\n");
		return -1;
	}

	return hdhomerun_control_get(hd->cs, "/ir/target", ptarget, NULL);
}

int hdhomerun_device_get_version(struct hdhomerun_device_t *hd, char **pversion_str, uint32_t *pversion_num)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_version: device not set\n");
		return -1;
	}

	char *version_str;
	int ret = hdhomerun_control_get(hd->cs, "/sys/version", &version_str, NULL);
	if (ret <= 0) {
		return ret;
	}

	if (pversion_str) {
		*pversion_str = version_str;
	}

	if (pversion_num) {
		unsigned int version_num;
		if (sscanf(version_str, "%u", &version_num) != 1) {
			*pversion_num = 0;
		} else {
			*pversion_num = (uint32_t)version_num;
		}
	}

	return 1;
}

int hdhomerun_device_get_supported(struct hdhomerun_device_t *hd, char *prefix, char **pstr)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_channel: device not set\n");
		return -1;
	}

	char *features;
	int ret = hdhomerun_control_get(hd->cs, "/sys/features", &features, NULL);
	if (ret <= 0) {
		return ret;
	}

	if (!prefix) {
		*pstr = features;
		return 1;
	}

	char *ptr = strstr(features, prefix);
	if (!ptr) {
		return 0;
	}

	ptr += strlen(prefix);
	*pstr = ptr;

	ptr = strchr(ptr, '\n');
	if (ptr) {
		*ptr = 0;
	}

	return 1;
}

int hdhomerun_device_set_tuner_channel(struct hdhomerun_device_t *hd, const char *channel)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_channel: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/channel", hd->tuner);
	return hdhomerun_control_set_with_lockkey(hd->cs, name, channel, hd->lockkey, NULL, NULL);
}

int hdhomerun_device_set_tuner_vchannel(struct hdhomerun_device_t *hd, const char *vchannel)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_vchannel: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/vchannel", hd->tuner);
	return hdhomerun_control_set_with_lockkey(hd->cs, name, vchannel, hd->lockkey, NULL, NULL);
}

int hdhomerun_device_set_tuner_channelmap(struct hdhomerun_device_t *hd, const char *channelmap)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_channelmap: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/channelmap", hd->tuner);
	return hdhomerun_control_set_with_lockkey(hd->cs, name, channelmap, hd->lockkey, NULL, NULL);
}

int hdhomerun_device_set_tuner_filter(struct hdhomerun_device_t *hd, const char *filter)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_filter: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/filter", hd->tuner);
	return hdhomerun_control_set_with_lockkey(hd->cs, name, filter, hd->lockkey, NULL, NULL);
}

static bool hdhomerun_device_set_tuner_filter_by_array_append(char *ptr, char *end, uint16_t range_begin, uint16_t range_end)
{
	if (range_begin == range_end) {
		return hdhomerun_sprintf(ptr, end, "0x%04x ", (unsigned int)range_begin);
	} else {
		return hdhomerun_sprintf(ptr, end, "0x%04x-0x%04x ", (unsigned int)range_begin, (unsigned int)range_end);
	}
}

int hdhomerun_device_set_tuner_filter_by_array(struct hdhomerun_device_t *hd, unsigned char filter_array[0x2000])
{
	char filter[1024];
	char *ptr = filter;
	char *end = filter + sizeof(filter);

	uint16_t range_begin = 0xFFFF;
	uint16_t range_end = 0xFFFF;

	uint16_t i;
	for (i = 0; i <= 0x1FFF; i++) {
		if (!filter_array[i]) {
			if (range_begin == 0xFFFF) {
				continue;
			}
			if (!hdhomerun_device_set_tuner_filter_by_array_append(ptr, end, range_begin, range_end)) {
				return 0;
			}
			ptr = strchr(ptr, 0);
			range_begin = 0xFFFF;
			range_end = 0xFFFF;
			continue;
		}

		if (range_begin == 0xFFFF) {
			range_begin = i;
			range_end = i;
			continue;
		}

		range_end = i;
	}

	if (range_begin != 0xFFFF) {
		if (!hdhomerun_device_set_tuner_filter_by_array_append(ptr, end, range_begin, range_end)) {
			return 0;
		}
		ptr = strchr(ptr, 0);
	}

	/* Remove trailing space. */
	if (ptr > filter) {
		ptr--;
		*ptr = 0;
	}

	return hdhomerun_device_set_tuner_filter(hd, filter);
}

int hdhomerun_device_set_tuner_program(struct hdhomerun_device_t *hd, const char *program)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_program: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/program", hd->tuner);
	return hdhomerun_control_set_with_lockkey(hd->cs, name, program, hd->lockkey, NULL, NULL);
}

int hdhomerun_device_set_tuner_target(struct hdhomerun_device_t *hd, const char *target)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_target: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/target", hd->tuner);
	return hdhomerun_control_set_with_lockkey(hd->cs, name, target, hd->lockkey, NULL, NULL);
}

static int hdhomerun_device_set_tuner_target_to_local(struct hdhomerun_device_t *hd, const char *protocol)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_target_to_local: device not set\n");
		return -1;
	}
	if (!hd->vs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_tuner_target_to_local: video not initialized\n");
		return -1;
	}

	/* Set target. */
	char target[64];
	uint32_t local_ip = hdhomerun_control_get_local_addr(hd->cs);
	uint16_t local_port = hdhomerun_video_get_local_port(hd->vs);
	hdhomerun_sprintf(target, target + sizeof(target), "%s://%u.%u.%u.%u:%u",
		protocol,
		(unsigned int)(local_ip >> 24) & 0xFF, (unsigned int)(local_ip >> 16) & 0xFF,
		(unsigned int)(local_ip >> 8) & 0xFF, (unsigned int)(local_ip >> 0) & 0xFF,
		(unsigned int)local_port
	);

	return hdhomerun_device_set_tuner_target(hd, target);
}

int hdhomerun_device_set_ir_target(struct hdhomerun_device_t *hd, const char *target)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_ir_target: device not set\n");
		return -1;
	}

	return hdhomerun_control_set(hd->cs, "/ir/target", target, NULL, NULL);
}

int hdhomerun_device_set_sys_dvbc_modulation(struct hdhomerun_device_t *hd, const char *modulation_list)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_sys_dvbc_modulation: device not set\n");
		return -1;
	}

	return hdhomerun_control_set(hd->cs, "/sys/dvbc_modulation", modulation_list, NULL, NULL);
}

int hdhomerun_device_get_var(struct hdhomerun_device_t *hd, const char *name, char **pvalue, char **perror)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_var: device not set\n");
		return -1;
	}

	return hdhomerun_control_get(hd->cs, name, pvalue, perror);
}

int hdhomerun_device_set_var(struct hdhomerun_device_t *hd, const char *name, const char *value, char **pvalue, char **perror)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_set_var: device not set\n");
		return -1;
	}

	return hdhomerun_control_set_with_lockkey(hd->cs, name, value, hd->lockkey, pvalue, perror);
}

int hdhomerun_device_tuner_lockkey_request(struct hdhomerun_device_t *hd, char **perror)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		return 1;
	}
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_tuner_lockkey_request: device not set\n");
		return -1;
	}

	uint32_t new_lockkey = random_get32();

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/lockkey", hd->tuner);

	char new_lockkey_str[64];
	hdhomerun_sprintf(new_lockkey_str, new_lockkey_str + sizeof(new_lockkey_str), "%u", (unsigned int)new_lockkey);

	int ret = hdhomerun_control_set_with_lockkey(hd->cs, name, new_lockkey_str, hd->lockkey, NULL, perror);
	if (ret <= 0) {
		hd->lockkey = 0;
		return ret;
	}

	hd->lockkey = new_lockkey;
	return ret;
}

int hdhomerun_device_tuner_lockkey_release(struct hdhomerun_device_t *hd)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		return 1;
	}
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_tuner_lockkey_release: device not set\n");
		return -1;
	}

	if (hd->lockkey == 0) {
		return 1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/lockkey", hd->tuner);
	int ret = hdhomerun_control_set_with_lockkey(hd->cs, name, "none", hd->lockkey, NULL, NULL);

	hd->lockkey = 0;

	hdhomerun_device_set_tuner_channel(hd, "none");
	
	return ret;
}

int hdhomerun_device_tuner_lockkey_force(struct hdhomerun_device_t *hd)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		return 1;
	}
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_tuner_lockkey_force: device not set\n");
		return -1;
	}

	char name[32];
	hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/lockkey", hd->tuner);
	int ret = hdhomerun_control_set(hd->cs, name, "force", NULL, NULL);

	hd->lockkey = 0;
	return ret;
}

void hdhomerun_device_tuner_lockkey_use_value(struct hdhomerun_device_t *hd, uint32_t lockkey)
{
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		return;
	}

	hd->lockkey = lockkey;
}

int hdhomerun_device_wait_for_lock(struct hdhomerun_device_t *hd, struct hdhomerun_tuner_status_t *status)
{
	/* Delay for SS reading to be valid (signal present). */
	msleep_minimum(250);

	/* Wait for up to 2.5 seconds for lock. */
	uint64_t timeout = getcurrenttime() + 2500;
	while (1) {
		/* Get status to check for lock. Quality numbers will not be valid yet. */
		int ret = hdhomerun_device_get_tuner_status(hd, NULL, status);
		if (ret <= 0) {
			return ret;
		}

		if (!status->signal_present) {
			return 1;
		}
		if (status->lock_supported || status->lock_unsupported) {
			return 1;
		}

		if (getcurrenttime() >= timeout) {
			return 1;
		}

		msleep_approx(250);
	}
}

int hdhomerun_device_stream_start(struct hdhomerun_device_t *hd)
{
	hdhomerun_device_get_video_sock(hd);
	if (!hd->vs) {
		return -1;
	}

	hdhomerun_video_set_keepalive(hd->vs, 0, 0, 0);

	/* Set target. */
	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		struct sockaddr local_ip;
		memset(&local_ip, 0, sizeof(local_ip));
		int ret = hdhomerun_video_join_multicast_group_ex(hd->vs, (struct sockaddr *)&hd->multicast_addr, &local_ip);
		if (ret <= 0) {
			return ret;
		}
	} else {
		int ret = hdhomerun_device_set_tuner_target_to_local(hd, HDHOMERUN_TARGET_PROTOCOL_RTP);
		if (ret == 0) {
			ret = hdhomerun_device_set_tuner_target_to_local(hd, HDHOMERUN_TARGET_PROTOCOL_UDP);
		}
		if (ret <= 0) {
			return ret;
		}

		uint32_t remote_ip = hdhomerun_control_get_device_ip(hd->cs);
		hdhomerun_video_set_keepalive(hd->vs, remote_ip, 5004, hd->lockkey);
	}

	/* Flush video buffer. */
	msleep_minimum(64);
	hdhomerun_video_flush(hd->vs);

	/* Success. */
	return 1;
}

uint8_t *hdhomerun_device_stream_recv(struct hdhomerun_device_t *hd, size_t max_size, size_t *pactual_size)
{
	if (!hd->vs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_stream_recv: video not initialized\n");
		return NULL;
	}

	return hdhomerun_video_recv(hd->vs, max_size, pactual_size);
}

void hdhomerun_device_stream_flush(struct hdhomerun_device_t *hd)
{
	if (!hd->vs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_stream_flush: video not initialized\n");
		return;
	}

	hdhomerun_video_flush(hd->vs);
}

void hdhomerun_device_stream_stop(struct hdhomerun_device_t *hd)
{
	if (!hd->vs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_stream_stop: video not initialized\n");
		return;
	}

	if (hdhomerun_sock_sockaddr_is_addr((struct sockaddr *)&hd->multicast_addr)) {
		struct sockaddr local_ip;
		memset(&local_ip, 0, sizeof(local_ip));
		hdhomerun_video_leave_multicast_group_ex(hd->vs, (struct sockaddr *)&hd->multicast_addr, &local_ip);
	} else {
		hdhomerun_device_set_tuner_target(hd, "none");
	}
}

int hdhomerun_device_channelscan_init(struct hdhomerun_device_t *hd, const char *channelmap)
{
	if (hd->scan) {
		channelscan_destroy(hd->scan);
	}

	hd->scan = channelscan_create(hd, channelmap);
	if (!hd->scan) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_channelscan_init: failed to create scan object\n");
		return -1;
	}

	return 1;
}

int hdhomerun_device_channelscan_advance(struct hdhomerun_device_t *hd, struct hdhomerun_channelscan_result_t *result)
{
	if (!hd->scan) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_channelscan_advance: scan not initialized\n");
		return 0;
	}

	int ret = channelscan_advance(hd->scan, result);
	if (ret <= 0) { /* Free scan if normal finish or fatal error */
		channelscan_destroy(hd->scan);
		hd->scan = NULL;
	}

	return ret;
}

int hdhomerun_device_channelscan_detect(struct hdhomerun_device_t *hd, struct hdhomerun_channelscan_result_t *result)
{
	if (!hd->scan) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_channelscan_detect: scan not initialized\n");
		return 0;
	}

	int ret = channelscan_detect(hd->scan, result);
	if (ret < 0) { /* Free scan if fatal error */
		channelscan_destroy(hd->scan);
		hd->scan = NULL;
	}

	return ret;
}

uint8_t hdhomerun_device_channelscan_get_progress(struct hdhomerun_device_t *hd)
{
	if (!hd->scan) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_channelscan_get_progress: scan not initialized\n");
		return 0;
	}

	return channelscan_get_progress(hd->scan);
}

const char *hdhomerun_device_get_hw_model_str(struct hdhomerun_device_t *hd)
{
    if (!hd->cs) {
        hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_hw_model_str: device not set\n");
        return NULL;
    }

    char *model_str;
    int ret = hdhomerun_control_get(hd->cs, "/sys/hwmodel", &model_str, NULL);
    if (ret < 0) {
        return NULL;
    }
    return model_str;
}


const char *hdhomerun_device_get_model_str(struct hdhomerun_device_t *hd)
{
	if (*hd->model) {
		return hd->model;
	}

	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_get_model_str: device not set\n");
		return NULL;
	}

	char *model_str;
	int ret = hdhomerun_control_get(hd->cs, "/sys/model", &model_str, NULL);
	if (ret < 0) {
		return NULL;
	}
	if (ret == 0) {
		hdhomerun_sprintf(hd->model, hd->model + sizeof(hd->model), "hdhomerun_atsc");
		return hd->model;
	}

	hdhomerun_sprintf(hd->model, hd->model + sizeof(hd->model), "%s", model_str);
	return hd->model;
}

int hdhomerun_device_upgrade(struct hdhomerun_device_t *hd, FILE *upgrade_file)
{
	if (!hd->cs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_upgrade: device not set\n");
		return -1;
	}

	hdhomerun_control_set(hd->cs, "/tuner0/lockkey", "force", NULL, NULL);
	hdhomerun_control_set(hd->cs, "/tuner0/channel", "none", NULL, NULL);

	hdhomerun_control_set(hd->cs, "/tuner1/lockkey", "force", NULL, NULL);
	hdhomerun_control_set(hd->cs, "/tuner1/channel", "none", NULL, NULL);

	return hdhomerun_control_upgrade(hd->cs, upgrade_file);
}

void hdhomerun_device_debug_print_video_stats(struct hdhomerun_device_t *hd)
{
	if (!hdhomerun_debug_enabled(hd->dbg)) {
		return;
	}

	if (hd->cs) {
		char name[32];
		hdhomerun_sprintf(name, name + sizeof(name), "/tuner%u/debug", hd->tuner);

		char *debug_str;
		char *error_str;
		int ret = hdhomerun_control_get(hd->cs, name, &debug_str, &error_str);
		if (ret < 0) {
			hdhomerun_debug_printf(hd->dbg, "video dev: communication error getting debug stats\n");
			return;
		}

		if (error_str) {
			hdhomerun_debug_printf(hd->dbg, "video dev: %s\n", error_str);
		} else {
			hdhomerun_debug_printf(hd->dbg, "video dev: %s\n", debug_str);
		}
	}

	if (hd->vs) {
		hdhomerun_video_debug_print_stats(hd->vs);
	}
}

void hdhomerun_device_get_video_stats(struct hdhomerun_device_t *hd, struct hdhomerun_video_stats_t *stats)
{
	if (!hd->vs) {
		hdhomerun_debug_printf(hd->dbg, "hdhomerun_device_stream_flush: video not initialized\n");
		memset(stats, 0, sizeof(struct hdhomerun_video_stats_t));
		return;
	}

	hdhomerun_video_get_stats(hd->vs, stats);
}
