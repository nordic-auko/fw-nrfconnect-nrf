/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include "libel.h"
#include "beacons.h"

/* scan list definitions (platform dependant) */
struct ap_scan {
    char mac[MAC_SIZE * 2];
    uint32_t age;
    uint32_t frequency;
    int16_t rssi;
};

/* some rssi values intentionally out of range */
struct ap_scan aps[] = /* clang-format off */ 
					{ { "283B8264E08B", 300, 3660, -8 },
					{ "826AB092DC99", 300, 3660, -130 },
					{ "283B823629F0", 300, 3660, -90 },
					{ "283B821C712A", 300, 3660, -77 },
//                     { "0024D2E08E5D", 300, 3660, -92 },
					{ "283B821CC232", 300, 3660, -91 },
					{ "74DADA5E1015", 300, 3660, -88 },
					{ "B482FEA46221", 300, 3660, -89 },
					{ "EC22809E00DB", 300, 3660, -90 } };

static int sky_log_func(Sky_log_level_t level, char *s)
{
	printk("%s", s);
	return 0;
}

static int sky_rand_func(uint8_t *rand_buf, uint32_t bufsize)
{
	*rand_buf = 5; // Decided by dice toss. Guaranteed to be random.

	return 0;
}

static time_t sky_time_func(time_t *t)
{
	return *t;
}

static void skyhook_embedded_lib_test(void)
{
    int i;
    Sky_errno_t sky_errno = -1;
    Sky_ctx_t *ctx;
    Sky_status_t ret_status;
    uint32_t *p;
    uint32_t bufsize;
    void *pstate;
    void *prequest;
    uint8_t *response;
    uint32_t request_size;
    uint32_t response_size;
    uint32_t timestamp = 0;
    Sky_location_t loc;

	// Dummy config values
	uint8_t device_id = 0;
	uint32_t id_len = 0;
	uint32_t partner_id = 0;
	uint8_t aes_key[AES_KEYLEN] = {0};
	uint8_t state_buf[1];
	Sky_log_level_t min_level = SKY_LOG_LEVEL_ALL;
	uint16_t port = 0;
    char server[80] = {0};

	ret_status = sky_open(&sky_errno, &device_id, id_len, partner_id, aes_key,
			state_buf, min_level, sky_log_func, sky_rand_func, sky_time_func);
	if (ret_status == SKY_ERROR) {
		printk("sky_open returned bad value, Can't continue\n");
		return;
	}

    /* Get the size of workspace needed */
    bufsize = sky_sizeof_workspace();
    if (bufsize == 0 || bufsize > 4096) {
        printk("sky_sizeof_workspace returned bad value, Can't continue\n");
        return;
    }

    /* Allocate and initialize workspace */
    ctx = (Sky_ctx_t *)(p = k_malloc(bufsize));
    memset(p, 0, bufsize);

    /* Start new request */
    if (sky_new_request(ctx, bufsize, &sky_errno) != ctx) {
        printk("sky_new_request() returned bad value\n");
        printk("sky_errno contains '%s'\n", sky_perror(sky_errno));
    }

    /* Add APs to the request */
    for (i = 0; i < sizeof(aps) / sizeof(struct ap_scan); i++) {
        uint8_t mac[MAC_SIZE];
        if (hex2bin(aps[i].mac, MAC_SIZE * 2, mac, MAC_SIZE) == MAC_SIZE) {
            ret_status = sky_add_ap_beacon(
                ctx, &sky_errno, mac, timestamp - aps[i].age, aps[i].rssi, aps[i].frequency, 1);
            if (ret_status == SKY_SUCCESS)
                printk("AP #%d added\n", i);
            else
                printk("sky_add_ap_beacon sky_errno contains '%s'", sky_perror(sky_errno));
        } else
            printk("Ignoring AP becon with bad MAC Address '%s' index %d\n", aps[i].mac, i + 1);
    }

    /* Add UMTS cell */
    ret_status = sky_add_cell_umts_beacon(ctx, &sky_errno,
        16101, // lac
        14962, // ucid
        603, // mcc
        1, // mnc
        33, // pci
        440, // earfcn
        timestamp - 315, // timestamp
        -100, // rscp
        0); // serving
    if (ret_status == SKY_SUCCESS)
        printk("Cell UMTS added\n");
    else
        printk("Error adding UMTS cell: '%s'\n", sky_perror(sky_errno));

    /* Add UMTS neighbor cell */
    ret_status = sky_add_cell_umts_neighbor_beacon(ctx, &sky_errno,
        33, // pci
        440, // earfcn
        timestamp - 315, // timestamp
        -100); // rscp

    if (ret_status == SKY_SUCCESS)
        printk("Cell neighbor UMTS added\n");
    else
        printk("Error adding UMTS neighbor cell: '%s'\n", sky_perror(sky_errno));

    /* Add another UMTS neighbor cell */
    ret_status = sky_add_cell_umts_neighbor_beacon(ctx, &sky_errno,
        55, // pci
        660, // earfcn
        timestamp - 316, // timestamp
        -101); // rscp

    if (ret_status == SKY_SUCCESS)
        printk("Cell neighbor UMTS added\n");
    else
        printk("Error adding UMTS neighbor cell: '%s'\n", sky_perror(sky_errno));

    /* Add CDMA cell */
    ret_status = sky_add_cell_cdma_beacon(ctx, &sky_errno,
        1552, // sid
        45004, // nid
        37799, // bsid
        timestamp - 315, // timestamp
        -159, // pilot-power
        0); // serving
    if (ret_status == SKY_SUCCESS)
        printk("Cell CDMA added\n");
    else
        printk("Error adding CDMA cell: '%s'\n", sky_perror(sky_errno));

    sky_add_gnss(
        ctx, &sky_errno, 36.740028, 3.049608, 108, 219.0, 40, 10.0, 270.0, 5, timestamp - 100);
    if (ret_status == SKY_SUCCESS)
        printk("GNSS added\n");
    else
        printk("Error adding GNSS: '%s'\n", sky_perror(sky_errno));

    /* Add LTE cell */
    ret_status = sky_add_cell_lte_beacon(ctx, &sky_errno,
        12345, // tac
        27907073, // eucid
        311, // mcc
        480, // mnc
        SKY_UNKNOWN_ID5, SKY_UNKNOWN_ID6,
        timestamp - 315, // timestamp
        -100, // rssi
        1); // serving

    if (ret_status == SKY_SUCCESS)
        printk("Cell added\n");
    else
        printk("Error adding LTE cell: '%s'\n", sky_perror(sky_errno));

    /* Add NBIOT cell */
    ret_status = sky_add_cell_nb_iot_beacon(ctx, &sky_errno,
        311, // mcc
        480, // mnc
        209979678, // eucid
        25187, // tac
        SKY_UNKNOWN_ID5, SKY_UNKNOWN_ID6,
        timestamp - 315, // timestamp
        -143, // rssi
        0); // serving

    if (ret_status == SKY_SUCCESS)
        printk("Cell added\n");
    else
        printk("Error adding NBIOT cell: '%s'\n", sky_perror(sky_errno));

    /* Add 5G cell */
    ret_status = sky_add_cell_nr_beacon(ctx, &sky_errno,
        600, // mcc
        10, // mnc
        6871947673, // nci
        25187, // tac
        400, // pci
        4000, // nrarfcn
        timestamp - 315, // timestamp
        -50, // rscp
        1); // serving
    if (ret_status == SKY_SUCCESS)
        printk("Cell nr added\n");
    else
        printk("Error adding nr cell: '%s'\n", sky_perror(sky_errno));

    /* Add 5G neighbor cell */
    ret_status = sky_add_cell_nr_neighbor_beacon(ctx, &sky_errno,
        1006, // pci
        653333, // earfcn
        timestamp - 315, // timestamp
        -49); // rscp

    if (ret_status == SKY_SUCCESS)
        printk("Cell neighbor lte added\n");
    else
        printk("Error adding lte neighbor cell: '%s'\n", sky_perror(sky_errno));

    /* Add LTE cell */
    ret_status = sky_add_cell_lte_beacon(ctx, &sky_errno,
        310, // tac
        268435454, // e_cellid
        201, // mcc
        700, // mnc
        502, // pci
        45500, // nrarfcn
        timestamp - 315, // timestamp
        -50, // rscp
        1); // serving
    if (ret_status == SKY_SUCCESS)
        printk("Cell nr added\n");
    else
        printk("Error adding lte cell: '%s'\n", sky_perror(sky_errno));

    /* Add LTE neighbor cell */
    ret_status = sky_add_cell_lte_neighbor_beacon(ctx, &sky_errno,
        502, // pci
        44, // earfcn
        timestamp - 315, // timestamp
        -100); // rscp

    if (ret_status == SKY_SUCCESS)
        printk("Cell neighbor lte added\n");
    else
        printk("Error adding lte neighbor cell: '%s'\n", sky_perror(sky_errno));

    /* Determine how big the network request buffer must be, and allocate a */
    /* buffer of that length. This function must be called for each request. */
    ret_status = sky_sizeof_request_buf(ctx, &request_size, &sky_errno);

    if (ret_status == SKY_ERROR) {
        printk("Error getting size of request buffer: %s\n", sky_perror(sky_errno));
        return;
    } else
        printk("Required buffer size = %d\n", request_size);

    prequest = k_malloc(request_size);

    /* Finalize the request. This will return either SKY_FINALIZE_LOCATION, in */
    /* which case the loc parameter will contain the location result which was */
    /* obtained from the cache, or SKY_FINALIZE_REQUEST, which means that the */
    /* request buffer must be sent to the Skyhook server. */
    Sky_finalize_t finalize =
        sky_finalize_request(ctx, &sky_errno, prequest, request_size, &loc, &response_size);

    if (finalize == SKY_FINALIZE_ERROR) {
        printk("sky_finalize_request sky_errno contains '%s'", sky_perror(sky_errno));
        if (sky_close(&sky_errno, &pstate))
            printk("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));
        return;
    }

    switch (finalize) {
    case SKY_FINALIZE_REQUEST:
        /* Need to send the request to the server. */
        response = k_malloc(response_size * sizeof(uint8_t));
        printk("server=%s, port=%d\n", server, port);
        printk("Sending request of length %d to server\nResponse buffer length %d %s\n",
            request_size, response_size, response != NULL ? "alloc'ed" : "bad alloc");
        if (response == NULL)
            return;

        int32_t rc = 0;//send_request((char *)prequest, (int)request_size, response, response_size, server, port);

        if (rc > 0)
            printk("Received response of length %d from server\n", rc);
        else {
            printk("Bad response from server\n");
            ret_status = sky_close(&sky_errno, &pstate);
            if (ret_status != SKY_SUCCESS)
                printk("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));
            return;
        }

        /* Decode the response from server or cache */
        ret_status = sky_decode_response(ctx, &sky_errno, response, response_size, &loc);

        if (ret_status != SKY_SUCCESS)
            printk("sky_decode_response error: '%s'\n", sky_perror(sky_errno));
        break;
    case SKY_FINALIZE_LOCATION:
        /* Location was found in the cache. No need to go to server. */
        printk("Location found in cache\n");
        break;
    case SKY_FINALIZE_ERROR:
        printk("Error finalizing request\n");
        return;
        break;
    }

    printk("Skyhook location: status: %s, lat: %d.%06d, lon: %d.%06d, hpe: %d, source: %d\n",
        sky_pserver_status(loc.location_status), (int)loc.lat,
        (int)fabs(round(1000000 * (loc.lat - (int)loc.lat))), (int)loc.lon,
        (int)fabs(round(1000000 * (loc.lon - (int)loc.lon))), loc.hpe, loc.location_source);

    ret_status = sky_close(&sky_errno, &pstate);

    if (ret_status != SKY_SUCCESS)
        printk("sky_close sky_errno contains '%s'\n", sky_perror(sky_errno));

    printk("Done.\n\n");
}

void main(void)
{
	printk("Hello, world. We meet again.\n");

	skyhook_embedded_lib_test();
}
