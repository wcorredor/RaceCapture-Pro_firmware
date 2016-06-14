/*
 * Race Capture Firmware
 *
 * Copyright (C) 2016 Autosport Labs
 *
 * This file is part of the Race Capture firmware suite
 *
 * This is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details. You should
 * have received a copy of the GNU General Public License along with
 * this code. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGGERSAMPLEDATA_H_
#define LOGGERSAMPLEDATA_H_

#include "cpp_guard.h"
#include "loggerConfig.h"
#include "sampleRecord.h"

#include <stddef.h>

CPP_GUARD_BEGIN

/**
 * Populates a struct sample object with channel data.  Note this does not
 * handle the timestamping.  That is done by creation and association of
 * a LoggerMessage object.
 */
int populate_sample_buffer(struct sample *s, size_t logTick);

void init_channel_sample_buffer(LoggerConfig *loggerConfig,
                                struct sample *s);

float get_mapped_value(float value, ScalingMap *scalingMap);

typedef void logger_sample_sample_cb_t(struct Serial* const serial,
                                       const struct sample* sample,
                                       const int ticks);

bool logger_sample_register_sample_cb(logger_sample_sample_cb_t* cb,
                                      struct Serial* const serial);

void logger_sample_process_callbacks(const int sample_tick,
                                     const struct sample* sample);

bool logger_sample_enable_sample_cb(struct Serial* const serial,
                                    const int sample_rate);

bool logger_sample_disable_sample_cb(struct Serial* const serial);

CPP_GUARD_END

#endif /* LOGGERSAMPLEDATA_H_ */
