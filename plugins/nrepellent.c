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

#include "../subprojects/libnrepel/include/nrepel.h"
#include "lv2/atom/atom.h"
#include "lv2/core/lv2.h"
#include "lv2/core/lv2_util.h"
#include "lv2/log/logger.h"
#include "lv2/state/state.h"
#include "lv2/urid/urid.h"
#include <stdlib.h>
#include <string.h>

#define NOISEREPELLENT_URI "https://github.com/lucianodato/noise-repellent"
#define NOISEREPELLENT_STEREO_URI                                              \
  "https://github.com/lucianodato/noise-repellent-stereo"

// TODO (luciano/todo): Use state mapping and unmapping instead of ladspa float
// arguments

typedef struct NoiseProfileState {
  uint32_t child_size;
  uint32_t child_type;
  float *elements;
} NoiseProfileState; // LV2 Atoms Vector Specification

static NoiseProfileState *
noise_profile_state_initialize(LV2_URID child_type,
                               const uint32_t noise_profile_size) {
  NoiseProfileState *self =
      (NoiseProfileState *)calloc(1U, sizeof(NoiseProfileState));
  self->child_type = (uint32_t)child_type;
  self->child_size = (uint32_t)sizeof(float);
  self->elements = (float *)calloc(noise_profile_size, sizeof(float));
  return self;
}

static void noise_profile_state_free(NoiseProfileState *self) {
  free(self->elements);
  free(self);
}

typedef struct URIs {
  LV2_URID atom_Int;
  LV2_URID atom_Float;
  LV2_URID atom_Vector;
  LV2_URID plugin;
  LV2_URID atom_URID;
} URIs;

typedef struct State {
  LV2_URID property_noise_profile_1;
  LV2_URID property_noise_profile_2;
  LV2_URID property_noise_profile_size;
  LV2_URID property_averaged_blocks;
} State;

static inline void map_uris(LV2_URID_Map *map, URIs *uris, const char *uri) {
  uris->plugin = strcmp(uri, NOISEREPELLENT_URI)
                     ? map->map(map->handle, NOISEREPELLENT_URI)
                     : map->map(map->handle, NOISEREPELLENT_STEREO_URI);
  uris->atom_Int = map->map(map->handle, LV2_ATOM__Int);
  uris->atom_Float = map->map(map->handle, LV2_ATOM__Float);
  uris->atom_Vector = map->map(map->handle, LV2_ATOM__Vector);
  uris->atom_URID = map->map(map->handle, LV2_ATOM__URID);
}

static inline void map_state(LV2_URID_Map *map, State *state, const char *uri) {
  if (!strcmp(uri, NOISEREPELLENT_URI)) {
    state->property_noise_profile_1 =
        map->map(map->handle, NOISEREPELLENT_STEREO_URI "#noiseprofile");
    state->property_noise_profile_2 =
        map->map(map->handle, NOISEREPELLENT_STEREO_URI "#noiseprofile");
    state->property_noise_profile_size =
        map->map(map->handle, NOISEREPELLENT_STEREO_URI "#noiseprofilesize");
    state->property_averaged_blocks = map->map(
        map->handle, NOISEREPELLENT_STEREO_URI "#noiseprofileaveragedblocks");

  } else {
    state->property_noise_profile_1 =
        map->map(map->handle, NOISEREPELLENT_URI "#noiseprofile");
    state->property_noise_profile_size =
        map->map(map->handle, NOISEREPELLENT_URI "#noiseprofilesize");
    state->property_averaged_blocks =
        map->map(map->handle, NOISEREPELLENT_URI "#noiseprofileaveragedblocks");
  }
}

typedef enum PortIndex {
  NOISEREPELLENT_AMOUNT = 0,
  NOISEREPELLENT_NOISE_OFFSET = 1,
  NOISEREPELLENT_RELEASE = 2,
  NOISEREPELLENT_MASKING = 3,
  NOISEREPELLENT_TRANSIENT_PROTECT = 4,
  NOISEREPELLENT_WHITENING = 5,
  NOISEREPELLENT_NOISE_LEARN = 6,
  NOISEREPELLENT_RESIDUAL_LISTEN = 7,
  NOISEREPELLENT_RESET_NOISE_PROFILE = 8,
  NOISEREPELLENT_ENABLE = 9,
  NOISEREPELLENT_LATENCY = 10,
  NOISEREPELLENT_INPUT_1 = 11,
  NOISEREPELLENT_OUTPUT_1 = 12,
  NOISEREPELLENT_INPUT_2 = 13,
  NOISEREPELLENT_OUTPUT_2 = 14,
} PortIndex;

typedef struct NoiseRepellentAdaptivePlugin {
  const float *input_1;
  const float *input_2;
  float *output_1;
  float *output_2;
  float sample_rate;
  float *report_latency;

  LV2_URID_Map *map;
  LV2_Log_Logger log;
  URIs uris;
  State state;
  char *plugin_uri;

  NoiseRepellentHandle lib_instance_1;
  NoiseRepellentHandle lib_instance_2;
  NrepelDenoiseParameters parameters;
  NoiseProfileState *noise_profile_state_1;
  NoiseProfileState *noise_profile_state_2;

  float *enable;
  float *learn_noise;
  float *residual_listen;
  float *reduction_amount;
  float *release_time;
  float *masking_ceiling_limit;
  float *whitening_factor;
  float *transient_threshold;
  float *noise_rescale;
  float *reset_noise_profile;

} NoiseRepellentAdaptivePlugin;

static void cleanup(LV2_Handle instance) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  if (self->noise_profile_state_1) {
    noise_profile_state_free(self->noise_profile_state_1);
  }

  if (self->noise_profile_state_2) {
    noise_profile_state_free(self->noise_profile_state_2);
  }

  if (self->lib_instance_1) {
    nrepel_free(self->lib_instance_1);
  }

  if (self->lib_instance_2) {
    nrepel_free(self->lib_instance_2);
  }

  if (self->plugin_uri) {
    free(self->plugin_uri);
  }

  free(instance);
}

static LV2_Handle instantiate(const LV2_Descriptor *descriptor,
                              const double rate, const char *bundle_path,
                              const LV2_Feature *const *features) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)calloc(
      1U, sizeof(NoiseRepellentAdaptivePlugin));

  // clang-format off
  const char *missing =
      lv2_features_query(features,
                         LV2_LOG__log, &self->log.log, false,
                         LV2_URID__map, &self->map, true,
                         NULL);
  // clang-format on

  lv2_log_logger_set_map(&self->log, self->map);

  if (missing) {
    lv2_log_error(&self->log, "Missing feature <%s>\n", missing);
    cleanup((LV2_Handle)self);
    return NULL;
  }

  if (strstr(descriptor->URI, NOISEREPELLENT_STEREO_URI)) {
    self->plugin_uri =
        (char *)calloc(strlen(NOISEREPELLENT_STEREO_URI) + 1, sizeof(char));
    strcpy(self->plugin_uri, descriptor->URI);
  } else {
    self->plugin_uri =
        (char *)calloc(strlen(NOISEREPELLENT_URI) + 1, sizeof(char));
    strcpy(self->plugin_uri, descriptor->URI);
  }

  map_uris(self->map, &self->uris, self->plugin_uri);
  map_state(self->map, &self->state, self->plugin_uri);

  self->sample_rate = (float)rate;

  self->lib_instance_1 = nrepel_initialize((uint32_t)self->sample_rate);
  if (!self->lib_instance_1) {
    lv2_log_error(&self->log, "Error initializing <%s>\n", self->plugin_uri);
    cleanup((LV2_Handle)self);
    return NULL;
  }

  self->noise_profile_state_1 = noise_profile_state_initialize(
      self->uris.atom_Float,
      nrepel_get_noise_profile_size(self->lib_instance_1));

  if (strstr(self->plugin_uri, NOISEREPELLENT_STEREO_URI)) {
    self->lib_instance_2 = nrepel_initialize((uint32_t)self->sample_rate);

    if (!self->lib_instance_2) {
      lv2_log_error(&self->log, "Error initializing <%s>\n", self->plugin_uri);
      cleanup((LV2_Handle)self);
      return NULL;
    }

    self->noise_profile_state_2 = noise_profile_state_initialize(
        self->uris.atom_Float,
        nrepel_get_noise_profile_size(self->lib_instance_2));
  }

  return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void *data) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  switch ((PortIndex)port) {
  case NOISEREPELLENT_AMOUNT:
    self->reduction_amount = (float *)data;
    break;
  case NOISEREPELLENT_NOISE_OFFSET:
    self->noise_rescale = (float *)data;
    break;
  case NOISEREPELLENT_RELEASE:
    self->release_time = (float *)data;
    break;
  case NOISEREPELLENT_MASKING:
    self->masking_ceiling_limit = (float *)data;
    break;
  case NOISEREPELLENT_WHITENING:
    self->whitening_factor = (float *)data;
    break;
  case NOISEREPELLENT_TRANSIENT_PROTECT:
    self->transient_threshold = (float *)data;
    break;
  case NOISEREPELLENT_NOISE_LEARN:
    self->learn_noise = (float *)data;
    break;
  case NOISEREPELLENT_RESET_NOISE_PROFILE:
    self->reset_noise_profile = (float *)data;
    break;
  case NOISEREPELLENT_RESIDUAL_LISTEN:
    self->residual_listen = (float *)data;
    break;
  case NOISEREPELLENT_ENABLE:
    self->enable = (float *)data;
    break;
  case NOISEREPELLENT_LATENCY:
    self->report_latency = (float *)data;
    break;
  case NOISEREPELLENT_INPUT_1:
    self->input_1 = (const float *)data;
    break;
  case NOISEREPELLENT_OUTPUT_1:
    self->output_1 = (float *)data;
    break;
  default:
    break;
  }
}

static void connect_port_stereo(LV2_Handle instance, uint32_t port,
                                void *data) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  connect_port(instance, port, data);

  switch ((PortIndex)port) {
  case NOISEREPELLENT_INPUT_2:
    self->input_2 = (const float *)data;
    break;
  case NOISEREPELLENT_OUTPUT_2:
    self->output_2 = (float *)data;
    break;
  default:
    break;
  }
}

static void activate(LV2_Handle instance) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  *self->report_latency = (float)nrepel_get_latency(self->lib_instance_1);
}

static void run(LV2_Handle instance, uint32_t number_of_samples) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  // clang-format off
  self->parameters = (NrepelDenoiseParameters){
      .enable = (bool)*self->enable,
      .learn_noise = (bool)*self->learn_noise,
      .residual_listen = (bool)*self->residual_listen,
      .masking_ceiling_limit = *self->masking_ceiling_limit,
      .reduction_amount = *self->reduction_amount,
      .noise_rescale = *self->noise_rescale,
      .release_time = *self->reduction_amount,
      .transient_threshold = *self->transient_threshold,
      .whitening_factor = *self->whitening_factor,
  };
  // clang-format on

  nrepel_load_parameters(self->lib_instance_1, self->parameters);

  if ((bool)*self->reset_noise_profile) {
    nrepel_reset_noise_profile(self->lib_instance_1);
  }

  nrepel_process(self->lib_instance_1, number_of_samples, self->input_1,
                 self->output_1);
}

static void run_stereo(LV2_Handle instance, uint32_t number_of_samples) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  run(instance, number_of_samples);

  nrepel_load_parameters(self->lib_instance_2, self->parameters);

  if ((bool)*self->reset_noise_profile) {
    nrepel_reset_noise_profile(self->lib_instance_2);
  }

  nrepel_process(self->lib_instance_2, number_of_samples, self->input_2,
                 self->output_2);
}

static LV2_State_Status save(LV2_Handle instance,
                             LV2_State_Store_Function store,
                             LV2_State_Handle handle, uint32_t flags,
                             const LV2_Feature *const *features) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  if (!nrepel_noise_profile_available(self->lib_instance_1)) {
    return LV2_STATE_SUCCESS;
  }

  uint32_t noise_profile_size =
      nrepel_get_noise_profile_size(self->lib_instance_1);

  store(handle, self->state.property_noise_profile_size, &noise_profile_size,
        sizeof(uint32_t), self->uris.atom_Int,
        LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

  uint32_t noise_profile_averaged_blocks =
      nrepel_get_noise_profile_blocks_averaged(self->lib_instance_1);

  store(handle, self->state.property_averaged_blocks,
        &noise_profile_averaged_blocks, sizeof(uint32_t), self->uris.atom_Int,
        LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

  memcpy(self->noise_profile_state_1->elements,
         nrepel_get_noise_profile(self->lib_instance_1),
         sizeof(float) * noise_profile_size);

  store(handle, self->state.property_noise_profile_1,
        (void *)self->noise_profile_state_1, sizeof(float) * noise_profile_size,
        self->uris.atom_Vector, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

  if (strstr(self->plugin_uri, NOISEREPELLENT_STEREO_URI)) {
    memcpy(self->noise_profile_state_2->elements,
           nrepel_get_noise_profile(self->lib_instance_2),
           sizeof(float) * noise_profile_size);

    store(handle, self->state.property_noise_profile_2,
          (void *)self->noise_profile_state_2,
          sizeof(float) * noise_profile_size, self->uris.atom_Vector,
          LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
  }

  return LV2_STATE_SUCCESS;
}

static LV2_State_Status restore(LV2_Handle instance,
                                LV2_State_Retrieve_Function retrieve,
                                LV2_State_Handle handle, uint32_t flags,
                                const LV2_Feature *const *features) {
  NoiseRepellentAdaptivePlugin *self = (NoiseRepellentAdaptivePlugin *)instance;

  size_t size = 0U;
  uint32_t type = 0U;
  uint32_t valflags = 0U;

  const uint32_t *fftsize = (const uint32_t *)retrieve(
      handle, self->state.property_noise_profile_size, &size, &type, &valflags);
  if (fftsize == NULL || type != self->uris.atom_Int) {
    return LV2_STATE_ERR_NO_PROPERTY;
  }

  const uint32_t *averagedblocks = (const uint32_t *)retrieve(
      handle, self->state.property_averaged_blocks, &size, &type, &valflags);
  if (averagedblocks == NULL || type != self->uris.atom_Int) {
    return LV2_STATE_ERR_NO_PROPERTY;
  }

  const void *saved_noise_profile_1 = retrieve(
      handle, self->state.property_noise_profile_1, &size, &type, &valflags);
  if (!saved_noise_profile_1 || size != sizeof(NoiseProfileState) ||
      type != self->uris.atom_Vector) {
    return LV2_STATE_ERR_NO_PROPERTY;
  }

  nrepel_load_noise_profile(self->lib_instance_1,
                            (float *)LV2_ATOM_BODY(saved_noise_profile_1),
                            *fftsize, *averagedblocks);

  if (strstr(self->plugin_uri, NOISEREPELLENT_STEREO_URI)) {
    const void *saved_noise_profile_2 = retrieve(
        handle, self->state.property_noise_profile_2, &size, &type, &valflags);
    if (!saved_noise_profile_2 || size != sizeof(NoiseProfileState) ||
        type != self->uris.atom_Vector) {
      return LV2_STATE_ERR_NO_PROPERTY;
    }

    nrepel_load_noise_profile(self->lib_instance_2,
                              (float *)LV2_ATOM_BODY(saved_noise_profile_2),
                              *fftsize, *averagedblocks);
  }

  return LV2_STATE_SUCCESS;
}

static const void *extension_data(const char *uri) {
  static const LV2_State_Interface state = {save, restore};
  if (strcmp(uri, LV2_STATE__interface) == 0) {
    return &state;
  }
  return NULL;
}

// clang-format off
static const LV2_Descriptor descriptor = {
    NOISEREPELLENT_URI,
    instantiate,
    connect_port,
    activate,
    run,
    NULL,
    cleanup,
    extension_data
};

static const LV2_Descriptor descriptor_stereo = {
    NOISEREPELLENT_STEREO_URI,
    instantiate,
    connect_port_stereo,
    activate,
    run_stereo,
    NULL,
    cleanup,
    extension_data
};
// clang-format on

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index) {
  switch (index) {
  case 0:
    return &descriptor;
  case 1:
    return &descriptor_stereo;
  default:
    return NULL;
  }
}