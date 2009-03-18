/*
 * Labjack Tools
 * Copyright (c) 2003-2007 Jim Paris <jim@jtan.com>
 *
 * This is free software; you can redistribute it and/or modify it and
 * it is provided under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation; see COPYING.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "ue9.h"
#include "compat.h"

int
main (int argc, char *argv[])
{
  int fd_cmd;
  struct ue9Calibration calib;

  verb_count = 2;

  fd_cmd = ue9_open ("192.168.1.209", 52360);
  if (fd_cmd < 0)
    {
      fprintf (stderr, "ue9_open: %s\n", compat_strerror (errno));
      return 1;
    }

  if (ue9_get_calibration (fd_cmd, &calib) < 0)
    {
      fprintf (stderr, "ue9_get_calibration: %s\n", compat_strerror (errno));
      return 1;
    }

  printf ("double unipolarSlope[0] = %lf\n", calib.unipolarSlope[0]);
  printf ("double unipolarSlope[1] = %lf\n", calib.unipolarSlope[1]);
  printf ("double unipolarSlope[2] = %lf\n", calib.unipolarSlope[2]);
  printf ("double unipolarSlope[3] = %lf\n", calib.unipolarSlope[3]);
  printf ("double unipolarOffset[0] = %lf\n", calib.unipolarOffset[0]);
  printf ("double unipolarOffset[1] = %lf\n", calib.unipolarOffset[1]);
  printf ("double unipolarOffset[2] = %lf\n", calib.unipolarOffset[2]);
  printf ("double unipolarOffset[3] = %lf\n", calib.unipolarOffset[3]);
  printf ("double bipolarSlope = %lf\n", calib.bipolarSlope);
  printf ("double bipolarOffset = %lf\n", calib.bipolarOffset);
  printf ("double DACSlope[0] = %lf\n", calib.DACSlope[0]);
  printf ("double DACSlope[1] = %lf\n", calib.DACSlope[1]);
  printf ("double DACOffset[0] = %lf\n", calib.DACOffset[0]);
  printf ("double DACOffset[1] = %lf\n", calib.DACOffset[1]);
  printf ("double tempSlope = %lf\n", calib.tempSlope);
  printf ("double tempSlopeLow = %lf\n", calib.tempSlopeLow);
  printf ("double calTemp = %lf\n", calib.calTemp);
  printf ("double Vref = %lf\n", calib.Vref);
  printf ("double VrefDiv2 = %lf\n", calib.VrefDiv2);
  printf ("double VsSlope = %lf\n", calib.VsSlope);
  printf ("double hiResUnipolarSlope = %lf\n", calib.hiResUnipolarSlope);
  printf ("double hiResUnipolarOffset = %lf\n", calib.hiResUnipolarOffset);
  printf ("double hiResBipolarSlope = %lf\n", calib.hiResBipolarSlope);
  printf ("double hiResBipolarOffset = %lf\n", calib.hiResBipolarOffset);

  ue9_close (fd_cmd);

  return 0;
}
