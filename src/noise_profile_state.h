/*
noise-repellent -- Noise Reduction LV2

Copyright 2021 Luciano Dato <lucianodato@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/
*/

#ifndef NOISE_PROFILE_STATE_H
#define NOISE_PROFILE_STATE_H

#include "lv2/urid/urid.h"
#include <stdlib.h>
#include <string.h>

typedef struct NoiseProfileState NoiseProfileState;

NoiseProfileState *noise_profile_state_initialize(LV2_URID child_type,
                                                  uint32_t noise_profile_size);
void noise_profile_state_free(NoiseProfileState *self);
float *noise_profile_get_elements(NoiseProfileState *self);
size_t noise_profile_get_size();

#endif