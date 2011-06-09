/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>

#include "netutil.h"
#include "compat.h"

#include "debug.h"
#include "ue9.h"
#include "ue9error.h"
#include "util.h"
#include "netutil.h"
#include "ethstream.h"

/* Fill checksums in data buffers, with "normal" checksum format */
void ue9_checksum_normal(uint8_t * buffer, size_t len)
{
	uint16_t sum = 0;

	if (len < 1) {
		fprintf(stderr, "ue9_checksum_normal: len too short\n");
		exit(1);
	}

	while (--len >= 1)
		sum += (uint16_t) buffer[len];
	sum = (sum / 256) + (sum % 256);
	sum = (sum / 256) + (sum % 256);
	buffer[0] = (uint8_t) sum;
}

/* Fill checksums in data buffers, with "extended" checksum format */
void ue9_checksum_extended(uint8_t * buffer, size_t len)
{
	uint16_t sum = 0;

	if (len < 6) {
		fprintf(stderr, "ue9_checksum_extended: len too short\n");
		exit(1);
	}

	/* 16-bit extended checksum */
	while (--len >= 6)
		sum += (uint16_t) buffer[len];
	buffer[4] = (uint8_t) (sum & 0xff);
	buffer[5] = (uint8_t) (sum >> 8);

	/* 8-bit normal checksum over first 6 bytes */
	ue9_checksum_normal(buffer, 6);
}

/* Verify checksums in data buffers, with "normal" checksum format. */
int ue9_verify_normal(uint8_t * buffer, size_t len)
{
	uint8_t saved, new;

	if (len < 1) {
		fprintf(stderr, "ue9_verify_normal: len too short\n");
		exit(1);
	}

	saved = buffer[0];
	ue9_checksum_normal(buffer, len);
	new = buffer[0];
	buffer[0] = saved;

	if (new != saved) {
		verb("got %02x, expected %02x\n", saved, new);
		return 0;
	}

	return 1;
}

/* Verify checksums in data buffers, with "extended" checksum format. */
int ue9_verify_extended(uint8_t * buffer, size_t len)
{
	uint8_t saved[3], new[3];

	if (len < 6) {
		fprintf(stderr, "ue9_verify_extended: len too short\n");
		exit(1);
	}

	saved[0] = buffer[0];
	saved[1] = buffer[4];
	saved[2] = buffer[5];
	ue9_checksum_extended(buffer, len);
	new[0] = buffer[0];
	new[1] = buffer[4];
	new[2] = buffer[5];
	buffer[0] = saved[0];
	buffer[4] = saved[1];
	buffer[5] = saved[2];

	if (saved[0] != new[0] || saved[1] != new[1] || saved[2] != new[2]) {
		verb("got %02x %02x %02x, expected %02x %02x %02x\n",
		     saved[0], saved[1], saved[2], new[0], new[1], new[2]);
		return 0;
	}

	return 1;
}

/* Data conversion.  If calib is NULL, use uncalibrated conversions. */
double
ue9_binary_to_analog(struct ue9Calibration *calib,
		     int gain, uint8_t resolution, uint16_t data)
{
	double slope = 0, offset;

	if (calib == NULL) {
		double uncal[9] = { 5.08, 2.54, 1.27, 0.63, 0, 0, 0, 0, 10.25 };
		if (gain >= ARRAY_SIZE(uncal) || uncal[gain] == 0) {
			fprintf(stderr, "ue9_binary_to_analog: bad gain\n");
			exit(1);
		}
		return data * uncal[gain] / 65536.0;
	}

	if (resolution < 18) {
		switch (gain) {
			case 1:
				slope = calib->unipolarSlope[0];
				offset = calib->unipolarOffset[0];
				break;
			case 2:
				slope = calib->unipolarSlope[1];
				offset = calib->unipolarOffset[1];
				break;
			case 4:
				slope = calib->unipolarSlope[2];
				offset = calib->unipolarOffset[2];
				break;
			case 8:
				slope = calib->unipolarSlope[3];
				offset = calib->unipolarOffset[3];
				break;
			default:
				slope = calib->bipolarSlope;
				offset = calib->bipolarOffset;
				}
	} else {
		if (gain == 0) {
			slope = calib->hiResUnipolarSlope;
			offset = calib->hiResUnipolarOffset;
		} else if (gain == 8) {
			slope = calib->hiResBipolarSlope;
			offset = calib->hiResBipolarOffset;
		}
	}

	if (slope == 0) {
		fprintf(stderr, "ue9_binary_to_analog: bad gain\n");
		exit(1);
	}

	return data * slope + offset;
}

/* Execute a command on the UE9.  Returns -1 on error.  Fills the
   checksums on the outgoing packets, and verifies them on the
   incoming packets.  Data in "out" is transmitted, data in "in" is
   received. */
int ue9_command(int fd, uint8_t * out, uint8_t * in, int inlen)
{
	int extended = 0, outlen;
	uint8_t saved_1, saved_3;
	ssize_t ret;

	if ((out[1] & 0x78) == 0x78)
		extended = 1;

	/* Figure out length of data payload, and fill checksums. */
	if (extended) {
		outlen = 6 + (out[2]) * 2;
		ue9_checksum_extended(out, outlen);
	} else {
		outlen = 2 + (out[1] & 7) * 2;
		ue9_checksum_normal(out, outlen);
	}

	/* Send request */
	ret = send_all_timeout(fd, out, outlen, 0, &(struct timeval) {
			       .tv_sec = TIMEOUT});
	if (ret < 0 || ret != outlen) {
		verb("short send %d\n", (int)ret);
		return -1;
	}

	/* Save a few bytes that we'll want to compare against later,
	   in case the caller passed the same buffer twice. */
	saved_1 = out[1];
	if (extended)
		saved_3 = out[3];

	/* Receive result */
	ret = recv_all_timeout(fd, in, inlen, 0, &(struct timeval) {
			       .tv_sec = TIMEOUT});
	if (ret < 0 || ret != inlen) {
		verb("short recv %d\n", (int)ret);
		return -1;
	}

	/* Verify it */
	if ((in[1] & 0xF8) != (saved_1 & 0xF8))
		verb("returned command doesn't match\n");
	else if (extended && (in[3] != saved_3))
		verb("extended command doesn't match\n");
	else if (extended && (inlen != (6 + (in[2]) * 2)))
		verb("returned extended data is the wrong len\n");
	else if (!extended && (inlen != (2 + (in[1] & 7) * 2)))
		verb("returned data is the wrong len\n");
	else if (extended && !ue9_verify_extended(in, inlen))
		verb("extended checksum is invalid\n");
	else if (!ue9_verify_normal(in, extended ? 6 : inlen))
		verb("normal checksum is invalid\n");
	else
		return 0;	/* looks good */

	return -1;
}

/* Read a memory block from the device.  Returns -1 on error. */
int ue9_memory_read(int fd, int blocknum, uint8_t * buffer, int len)
{
	uint8_t sendbuf[8], recvbuf[136];

	if (len != 128) {
		fprintf(stderr, "ue9_memory_read: buffer length must be 128\n");
		exit(1);
	}

	/* Request memory block */
	sendbuf[1] = 0xf8;
	sendbuf[2] = 0x01;
	sendbuf[3] = 0x2a;
	sendbuf[6] = 0x00;
	sendbuf[7] = blocknum;

	if (ue9_command(fd, sendbuf, recvbuf, sizeof(recvbuf)) < 0) {
		verb("command failed\n");
		return -1;
	}

	/* Got it */
	memcpy(buffer, recvbuf + 8, len);

	return 0;
}

/* Convert 64-bit fixed point to double type */
double ue9_fp64_to_double(uint8_t * data)
{
	int32_t a;
	uint32_t b;

	a = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
	b = (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];

	return (double)a + (double)b / (double)4294967296.0L;
}

/* Retrieve calibration data from the device.  Returns -1 on error. */
int ue9_get_calibration(int fd, struct ue9Calibration *calib)
{
	uint8_t buf[128];

	/* Block 0 */
	if (ue9_memory_read(fd, 0, buf, 128) < 0)
		return -1;
	calib->unipolarSlope[0] = ue9_fp64_to_double(buf + 0);
	calib->unipolarOffset[0] = ue9_fp64_to_double(buf + 8);
	calib->unipolarSlope[1] = ue9_fp64_to_double(buf + 16);
	calib->unipolarOffset[1] = ue9_fp64_to_double(buf + 24);
	calib->unipolarSlope[2] = ue9_fp64_to_double(buf + 32);
	calib->unipolarOffset[2] = ue9_fp64_to_double(buf + 40);
	calib->unipolarSlope[3] = ue9_fp64_to_double(buf + 48);
	calib->unipolarOffset[3] = ue9_fp64_to_double(buf + 56);

	/* Block 1 */
	if (ue9_memory_read(fd, 1, buf, 128) < 0)
		return -1;
	calib->bipolarSlope = ue9_fp64_to_double(buf + 0);
	calib->bipolarOffset = ue9_fp64_to_double(buf + 8);

	/* Block 2 */
	if (ue9_memory_read(fd, 2, buf, 128) < 0)
		return -1;
	calib->DACSlope[0] = ue9_fp64_to_double(buf + 0);
	calib->DACOffset[0] = ue9_fp64_to_double(buf + 8);
	calib->DACSlope[1] = ue9_fp64_to_double(buf + 16);
	calib->DACOffset[1] = ue9_fp64_to_double(buf + 24);
	calib->tempSlope = ue9_fp64_to_double(buf + 32);
	calib->tempSlopeLow = ue9_fp64_to_double(buf + 48);
	calib->calTemp = ue9_fp64_to_double(buf + 64);
	calib->Vref = ue9_fp64_to_double(buf + 72);
	calib->VrefDiv2 = ue9_fp64_to_double(buf + 88);
	calib->VsSlope = ue9_fp64_to_double(buf + 96);

	/* Block 3 */
	if (ue9_memory_read(fd, 3, buf, 128) < 0)
		return -1;
	calib->hiResUnipolarSlope = ue9_fp64_to_double(buf + 0);
	calib->hiResUnipolarOffset = ue9_fp64_to_double(buf + 8);

	/* Block 4 */
	if (ue9_memory_read(fd, 4, buf, 128) < 0)
		return -1;
	calib->hiResBipolarSlope = ue9_fp64_to_double(buf + 0);
	calib->hiResBipolarOffset = ue9_fp64_to_double(buf + 8);

	/* All done */
	return 1;
}

/* Retrieve comm config, returns -1 on error */
int ue9_get_comm_config(int fd, struct ue9CommConfig *config)
{
	uint8_t sendbuf[18];
	uint8_t recvbuf[24];

	memset(sendbuf, 0, sizeof(sendbuf));
	memset(config, 0, sizeof(struct ue9CommConfig));

	sendbuf[1] = 0xf8;
	sendbuf[2] = 0x09;
	sendbuf[3] = 0x08;
	if (ue9_command(fd, sendbuf, recvbuf, sizeof(recvbuf)) < 0) {
		verb("command failed\n");
		return -1;
	}
	verb("todo\n");
	return -1;
}

/* Retrieve control config, returns -1 on error */
int ue9_get_control_config(int fd, struct ue9ControlConfig *config)
{
	uint8_t sendbuf[18];
	uint8_t recvbuf[24];

	memset(sendbuf, 0, sizeof(sendbuf));
	memset(config, 0, sizeof(struct ue9ControlConfig));

	sendbuf[1] = 0xf8;
	sendbuf[2] = 0x06;
	sendbuf[3] = 0x08;
	if (ue9_command(fd, sendbuf, recvbuf, sizeof(recvbuf)) < 0) {
		verb("command failed\n");
		return -1;
	}
	verb("todo\n");
	return -1;
}

/* Open TCP/IP connection to the UE9 */
int ue9_open(const char *host, int port)
{
	int fd;
	struct sockaddr_in address;
	struct hostent *he;
	int window_size = 128 * 1024;

	net_init();

	/* Create socket */
	fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) {
		verb("socket returned %d\n", fd);
		return -1;
	}

	/* Set nonblocking */
	if (soblock(fd, 0) < 0) {
		verb("can't set nonblocking\n");
		return -1;
	}

	/* Set initial window size hint to workaround LabJack firmware bug */
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *)&window_size,
		   sizeof(window_size));
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *)&window_size,
		   sizeof(window_size));

	/* Resolve host */
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	he = gethostbyname(host);
	if (he == NULL) {
		verb("gethostbyname(\"%s\") failed\n", host);
		return -1;
	}
	address.sin_addr = *((struct in_addr *)he->h_addr);

	debug("Resolved %s -> %s\n", host, inet_ntoa(address.sin_addr));

	/* Connect */
	if (connect_timeout(fd, (struct sockaddr *)&address, sizeof(address),
			    &(struct timeval) {
			    .tv_sec = TIMEOUT}) < 0) {
		verb("connection to %s:%d failed: %s\n",
		     inet_ntoa(address.sin_addr), port, compat_strerror(errno));
		return -1;
	}
	
	debug("Connected to port %d\n", port);

	return fd;
}

/* Close connection to the UE9 */
void ue9_close(int fd)
{
	/* does anyone actually call shutdown these days? */
	shutdown(fd, 2 /* SHUT_RDWR */ );
	close(fd);
}

/* Compute scanrate based on the provided values. */
double ue9_compute_rate(uint8_t scanconfig, uint16_t scaninterval)
{
	double clock;

	/* A "scan" is across all channels.  Each scan is triggered at
	   a fixed rate, and not affected by the number of channels.
	   Channels are scanned as quickly as possible. */

	switch ((scanconfig >> 3) & 3) {
	case 0:
		clock = 4e6;
		break;
	case 1:
		clock = 48e6;
		break;
	case 2:
		clock = 750e3;
		break;
	case 3:
		clock = 24e6;
		break;
	}

	if (scanconfig & 0x2)
		clock /= 256;

	if (scaninterval == 0)
		return 0;

	return clock / scaninterval;
}

/* Choose the best ScanConfig and ScanInterval parameters for the
   desired scanrate.  Returns -1 if no valid config found */
int
ue9_choose_scan(double desired_rate, double *actual_rate,
		uint8_t * scanconfig, uint16_t * scaninterval)
{
	int i;
	struct {
		double clock;
		uint8_t config;
	} valid[] = {
		{
		48e6, 0x08}, {
		24e6, 0x18}, {
		4e6, 0x00}, {
		750e3, 0x10}, {
		48e6 / 256, 0x0a}, {
		24e6 / 256, 0x1a}, {
		4e6 / 256, 0x02}, {
		750e3 / 256, 0x12}, {
	0, 0}};

	/* Start with the fastest clock frequency.  If the
	   scaninterval would be too large, knock it down until it
	   fits. */
	for (i = 0; valid[i].clock != 0; i++) {
		double interval = valid[i].clock / desired_rate;

		debug("Considering clock %lf (interval %lf)\n",
		      valid[i].clock, interval);

		if (interval >= 0.5 && interval < 65535.5) {

			*scaninterval = floor(interval + 0.5);

			*scanconfig = valid[i].config;
			*actual_rate =
			    ue9_compute_rate(*scanconfig, *scaninterval);

			debug("Config 0x%02x, desired %lf, actual %lf\n",
			      *scanconfig, desired_rate, *actual_rate);

			return 0;
		}
	}

	return -1;
}

/* Flush data buffers */
void ue9_buffer_flush(int fd)
{
	uint8_t sendbuf[2], recvbuf[2];

	sendbuf[1] = 0x08;	/* FlushBuffer */

	if (ue9_command(fd, sendbuf, recvbuf, sizeof(recvbuf)) < 0) {
		verb("command failed\n");
	}
}

/* Stop stream.  Returns < 0 on failure. */
int ue9_stream_stop(int fd)
{
	uint8_t sendbuf[2], recvbuf[4];

	sendbuf[1] = 0xB0;

	if (ue9_command(fd, sendbuf, recvbuf, sizeof(recvbuf)) < 0) {
		verb("command failed\n");
		return -1;
	}

	if (recvbuf[2] == STREAM_NOT_RUNNING || recvbuf[2] == 0)
		return 0;

	debug("error %s\n", ue9_error(recvbuf[2]));
	return -recvbuf[2];
}

/* Start stream.  Returns < 0 on failure. */
int ue9_stream_start(int fd)
{
	uint8_t sendbuf[2], recvbuf[4];

	sendbuf[1] = 0xA8;

	if (ue9_command(fd, sendbuf, recvbuf, sizeof(recvbuf)) < 0) {
		verb("command failed\n");
		return -1;
	}

	if (recvbuf[2] == 0)
		return 0;

	debug("error %s\n", ue9_error(recvbuf[2]));
	return -recvbuf[2];
}

/* "Simple" stream configuration, assumes the channels are all 
   configured with the same gain. */
int
ue9_streamconfig_simple(int fd, int *channel_list, int channel_count,
			uint8_t scanconfig, uint16_t scaninterval, uint8_t gain)
{
	int i;
	uint8_t buf[256];

	/* Set up StreamConfig command with channels and scan options */
	buf[1] = 0xF8;		/* Extended command */
	buf[2] = channel_count + 3;	/* Command data words */
	buf[3] = 0x11;		/* StreamConfig */
	buf[6] = channel_count;	/* Number of channels */
	buf[7] = 12;		/* Bit resolution */
	buf[8] = 0;		/* Extra settling time */
	buf[9] = scanconfig;
	buf[10] = scaninterval & 0xff;
	buf[11] = scaninterval >> 8;

	for (i = 0; i < channel_count; i++) {
		buf[12 + 2 * i] = channel_list[i];	/* Channel number */
		buf[13 + 2 * i] = gain;	/* Gain/bipolar setup */
	}

	/* Send StreamConfig */
	if (ue9_command(fd, buf, buf, 8) < 0) {
		debug("command failed\n");
		return -1;
	}

	if (buf[6] != 0) {
		verb("returned error %s\n", ue9_error(buf[6]));
		return -1;
	}

	return 0;
}

/* Stream configuration, each Analog Input channel can have its own gain. */
int
ue9_streamconfig(int fd, int *channel_list, int channel_count,
			uint8_t scanconfig, uint16_t scaninterval, int *gain_list, int gain_count)
{
	int i;
	uint8_t buf[256];

	/* Set up StreamConfig command with channels and scan options */
	buf[1] = 0xF8;		/* Extended command */
	buf[2] = channel_count + 3;	/* Command data words */
	buf[3] = 0x11;		/* StreamConfig */
	buf[6] = channel_count;	/* Number of channels */
	buf[7] = 12;		/* Bit resolution */
	buf[8] = 0;		/* Extra settling time */
	buf[9] = scanconfig;
	buf[10] = scaninterval & 0xff;
	buf[11] = scaninterval >> 8;

	for (i = 0; i < channel_count; i++) {
		buf[12 + 2 * i] = channel_list[i];	/* Channel number */
		if (i < gain_count) {
			switch (gain_list[i]) {
				case 0:
					buf[13 + 2 * i] = UE9_BIPOLAR_GAIN1;
				break;
					
				case 1:
					buf[13 + 2 * i] = UE9_UNIPOLAR_GAIN1;
				break;
			
				case 2:
					buf[13 + 2 * i] = UE9_UNIPOLAR_GAIN2;
				break;
				
				case 4:
					buf[13 + 2 * i] = UE9_UNIPOLAR_GAIN4;
				break;
			
				case 8:
					buf[13 + 2 * i] = UE9_UNIPOLAR_GAIN8;
				break;
				
				default:
					buf[13 + 2 * i] = UE9_BIPOLAR_GAIN1;
			}
		}
		else
		{
			buf[13 + 2 * i] = UE9_BIPOLAR_GAIN1;
		}
	}

	/* Send StreamConfig */
	if (ue9_command(fd, buf, buf, 8) < 0) {
		debug("command failed\n");
		return -1;
	}

	if (buf[6] != 0) {
		verb("returned error %s\n", ue9_error(buf[6]));
		return -1;
	}

	return 0;
}


/* Timer configuration */
int ue9_timer_config(int fd, int *mode_list, int *value_list, int count, int divisor)
{
	int i;
	uint8_t buf[256];

	if (count < 0 || count > 6) {
		verb("invalid count\n");
		return -1;
	}

	/* Set up TimerConfig command */
	buf[1] = 0xF8;		/* Extended command */
	buf[2] = 0x0C;		/* Command data words */
	buf[3] = 0x18;		/* TimerConfig */
	buf[6] = divisor;	/* TimerClockDivisor */
	buf[7] = 0x80 | count;	/* Number of timers enabled, UpdateConfig=1 */
	buf[8] = 0x01;		/* TimerClockBase = System 48MHz */
	buf[9] = 0x00;		/* Don't reset */

	for (i = 0; i < 6; i++) {
		if (i < count) {
			buf[10 + 3 * i] = mode_list[i];
			buf[11 + 3 * i] = value_list[i] & 0xff;
			buf[12 + 3 * i] = value_list[i] >> 8;
		}
		else {
			buf[10 + 3 * i] = 0;
			buf[11 + 3 * i] = 0;
			buf[12 + 3 * i] = 0;
		}
	}

	buf[28] = 0;
	buf[29] = 0;

	/* Send StreamConfig */
	if (ue9_command(fd, buf, buf, 40) < 0) {
		debug("command failed\n");
		return -1;
	}

	if (buf[6] != 0) {
		verb("returned error %s\n", ue9_error(buf[6]));
		return -1;
	}

	debug("timer EnableStatus=0x%02x\n", buf[7]);

	return 0;
}

/* Stream data and pass it to the data callback.  If callback returns
   negative, stops reading and returns 0.  Returns < 0 on error. */
int
ue9_stream_data(int fd, int channels, int *channel_list, int gain_count, int *gain_list, ue9_stream_cb_t callback, void *context)
{
	int ret;
	uint8_t buf[46];
	uint8_t packet = 0;
	int channel = 0;
	int i;
	uint16_t data[channels];

	for (;;) {
		/* Receive data */
		ret = recv_all_timeout(fd, buf, 46, 0, &(struct timeval) {
				       .tv_sec = TIMEOUT});

		/* Verify packet format */
		if (ret != 46) {
			verb("short recv %d\n", (int)ret);
			return -1;
		}

		if (!ue9_verify_extended(buf, 46) || !ue9_verify_normal(buf, 6)) {
			verb("bad checksum\n");
			return -2;
		}

		if (buf[1] != 0xF9 || buf[2] != 0x14 || buf[3] != 0xC0) {
			verb("bad command bytes\n");
			return -3;
		}

		if (buf[11] != 0) {
			verb("stream error: %s\n", ue9_error(buf[11]));
			return -4;
		}

		/* Check for dropped packets. */
		if (buf[10] != packet) {
			verb("expected packet %d, but received packet %d\n",
			     packet, buf[10]);
			return -5;
		}
		packet++;

		/* Check comm processor backlog (up to 512 kB) */
		if (buf[45] & 0x80) {
			verb("buffer overflow in CommBacklog, aborting\n");
			return -6;
		}
		if ((buf[45] & 0x7f) > 112)
			debug("warning: CommBacklog is high (%d bytes)\n",
			      (buf[45] & 0x7f) * 4096);

		/* Check control processor backlog (up to 256 bytes). */
		if (buf[44] == 255) {
			verb("ControlBacklog is maxed out, aborting\n");
			return -7;
		}
		if (buf[44] > 224)
			debug("warning: ControlBacklog is high (%d bytes)\n",
			      buf[44]);

		/* Read samples from the buffer */
		for (i = 12; i <= 42; i += 2) {
			data[channel++] = buf[i] + (buf[i + 1] << 8);
			if (channel < channels)
				continue;

			/* Received a full scan, send to callback */
			channel = 0;
			if ((*callback) (channels, channel_list, gain_count, gain_list, data, context) < 0) {
				/* We're done */
				return 0;
			}
		}
	}
}

/*
Local variables:
c-basic-offset: 2
End:
*/
