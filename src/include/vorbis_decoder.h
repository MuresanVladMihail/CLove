/*
#   clove
#
#   Copyright (C) 2016-2026 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#pragma once

#include <stdbool.h>

#include "audio.h"
#include "streamsource.h"

int audio_vorbis_load(ALuint buffer, char const *filename, float *outSeconds);
int audio_vorbis_loadStream(audio_vorbis_DecoderData* data, char const *filename);
void audio_vorbis_closeStream(audio_vorbis_DecoderData *data);
int audio_vorbis_preloadStreamSamples(audio_vorbis_DecoderData* decoderData, int sampleCount);
int audio_vorbis_uploadSreamSamples(audio_vorbis_DecoderData* decoderData, ALuint buffer);
void audio_vorbis_rewindStream(audio_vorbis_DecoderData *decoderData);
float audio_vorbis_getDuration(audio_vorbis_DecoderData *decoderData);
bool audio_vorbis_seekSample(audio_vorbis_DecoderData *decoderData, unsigned int sample);
int audio_vorbis_getChannelCount(audio_vorbis_DecoderData *decoderData);
int audio_vorbis_getSampleRate(audio_vorbis_DecoderData *decoderData);
void audio_vorbis_flushBuffer(audio_vorbis_DecoderData *decoderData);
