/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

#ifndef UE9_H
#define UE9_H

#include <stdint.h>
#include <stdlib.h>

#include "netutil.h"

/* Calibration data */
struct ue9Calibration {
	double unipolarSlope[4];
	double unipolarOffset[4];
	double bipolarSlope;
	double bipolarOffset;
	double DACSlope[2];
	double DACOffset[2];
	double tempSlope;
	double tempSlopeLow;
	double calTemp;
	double Vref;
	double VrefDiv2;
	double VsSlope;
	double hiResUnipolarSlope;
	double hiResUnipolarOffset;
	double hiResBipolarSlope;
	double hiResBipolarOffset;
};

/* Comm config */
struct ue9CommConfig {
	uint8_t local_id;
	uint8_t power_level;
	in_addr_t address;
	in_addr_t gateway;
	in_addr_t subnet;
	in_port_t portA;
	in_port_t portB;
	uint8_t dhcp_enabled;
	uint8_t product_id;
	uint8_t mac_address[6];
	double hw_version;
	double comm_fw_version;
};

/* Control config */
struct ue9ControlConfig {
	uint8_t power_level;
	uint8_t reset_source;
	double control_fw_version;
	double control_bl_version;
	uint8_t hires;
	uint8_t fio_dir;
	uint8_t fio_state;
	uint8_t eio_dir;
	uint8_t eio_state;
	uint8_t cio_dirstate;;
	uint8_t mio_dirstate;
	uint16_t dac0;
	uint16_t dac1;
};

#define UE9_UNIPOLAR_GAIN1 0x00
#define UE9_UNIPOLAR_GAIN2 0x01
#define UE9_UNIPOLAR_GAIN4 0x02
#define UE9_UNIPOLAR_GAIN8 0x03
#define UE9_BIPOLAR_GAIN1 0x08

#define UE9_MAX_CHANNEL_COUNT 128
#define UE9_MAX_CHANNEL 255
#define UE9_MAX_ANALOG_CHANNEL 13
#define UE9_TIMERS 6

/* Fill checksums in data buffers */
void ue9_checksum_normal(uint8_t * buffer, size_t len);
void ue9_checksum_extended(uint8_t * buffer, size_t len);

/* Verify checksums in data buffers.  Returns 0 on error. */
int ue9_verify_normal(uint8_t * buffer, size_t len);
int ue9_verify_extended(uint8_t * buffer, size_t len);

/* Open/close TCP/IP connection to the UE9 */
int ue9_open(const char *host, int port);
void ue9_close(int fd);

/* Read a memory block from the device.  Returns -1 on error. */
int ue9_memory_read(int fd, int blocknum, uint8_t * buffer, int len);

/* Convert 64-bit fixed point to double type */
double ue9_fp64_to_double(uint8_t * data);

/* Retrieve calibration data or configuration from the device */
int ue9_get_calibration(int fd, struct ue9Calibration *calib);
int ue9_get_comm_config(int fd, struct ue9CommConfig *config);
int ue9_get_control_config(int fd, struct ue9ControlConfig *config);

/* Data conversion.  If calib is NULL, use uncalibrated conversions. */
double ue9_binary_to_analog(struct ue9Calibration *calib,
			    int gain, uint8_t resolution, uint16_t data);

/* Compute scanrate based on the provided values. */
double ue9_compute_rate(uint8_t scanconfig, uint16_t scaninterval);

/* Choose the best ScanConfig and ScanInterval parameters for the
   desired scanrate.  Returns 0 if nothing can be chosen. */
int ue9_choose_scan(double desired_rate, double *actual_rate,
		    uint8_t * scanconfig, uint16_t * scaninterval);

/* Flush data buffers */
void ue9_buffer_flush(int fd);

/* Stop stream.  Returns < 0 on failure. */
int ue9_stream_stop(int fd);

/* Start stream.  Returns < 0 on failure. */
int ue9_stream_start(int fd);

/* Execute a command on the UE9.  Returns -1 on error.  Fills the
   checksums on the outgoing packets, and verifies them on the
   incoming packets.  Data in "out" is transmitted, data in "in" is
   received. */
int ue9_command(int fd, uint8_t * out, uint8_t * in, int inlen);

/* "Simple" stream configuration, assumes the channels are all 
   configured with the same gain. */
int ue9_streamconfig_simple(int fd, int *channel_list, int channel_count,
			    uint8_t scanconfig, uint16_t scaninterval,
			    uint8_t gain);
				
/* Stream configuration, each Analog Input channel can have its own gain. */
int ue9_streamconfig(int fd, int *channel_list, int channel_count,
			    uint8_t scanconfig, uint16_t scaninterval,
			    int *gain_list, int gain_count);

/* Timer configuration */
int ue9_timer_config(int fd, int *mode_list, int mode_count, int divisor);

/* Stream data and pass it to the data callback.  If callback returns
   negative, stops reading and returns 0.  Returns < 0 on error. */
typedef int (*ue9_stream_cb_t) (int channels, int *channel_list, int gain_count, int *gain_list, uint16_t * data, void *context);
int ue9_stream_data(int fd, int channels, int *channel_list, int gain_count, int *gain_list,
		    ue9_stream_cb_t callback, void *context);

#endif
