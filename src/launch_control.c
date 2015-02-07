/**
 * Race Capture Pro Firmware
 *
 * Copyright (C) 2014 Autosport Labs
 *
 * This file is part of the Race Capture Pro fimrware suite
 *
 * This is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details. You should have received a copy of the GNU
 * General Public License along with this code. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Stieg
 */

#include "launch_control.h"
#include "geoCircle.h"
#include "geopoint.h"
#include "gps.h"
#include "tracks.h"

#include <stdbool.h>

/*
 * Arbitrarily choosing 3MPH because if you are going faster than that,
 * you are driving/racing.
 */
#define LC_SPEED_THRESHOLD 3.0

static tiny_millis_t g_startTime;
static struct GeoCircle g_geoCircle;
static bool g_hasLaunched;

static bool isValidStartTime() {
   return g_startTime != 0;
}

static bool isConfigured() {
   return gc_isValidGeoCircle(g_geoCircle);
}

static bool isGeoPointInStartArea(const GeoPoint p) {
   return gc_isPointInGeoCircle(&p, g_geoCircle);
}

static bool isSpeedBelowThreshold(const float speed) {
   return speed < LC_SPEED_THRESHOLD;
}

bool lc_hasLaunched() {
   return g_hasLaunched;
}

tiny_millis_t lc_getLaunchTime() {
   return lc_hasLaunched() ? g_startTime : 0;
}

void lc_reset() {
   g_startTime = 0;
   g_geoCircle = (struct GeoCircle) {{0}};  // GCC Bug 53119.
   g_hasLaunched = false;
}

void lc_setup(const Track *track, const float targetRadius) {
   lc_reset();
   g_geoCircle = gc_createGeoCircle(getStartPoint(track), targetRadius);
}

void lc_supplyGpsSample(const GpsSample *sample) {
   if (!isConfigured() || lc_hasLaunched())
      return;

   if (isGeoPointInStartArea(sample->point)) {
      if (!isValidStartTime() || isSpeedBelowThreshold(sample->speed)) {
         /*
          * Use getMillisSinceFirstFix since this method accounts for
          * time drift between when the sample was taken and what the
          * actual time is when we get to this point.
          */
         g_startTime = getMillisSinceFirstFix();
      }
   } else {
      g_hasLaunched = isValidStartTime();
   }
}
