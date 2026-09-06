/*
#   clove
#
#   Copyright (C) 2016-2020 Muresan Vlad
#
#   This project is free software; you can redistribute it and/or modify it
#   under the terms of the MIT license. See LICENSE.md for details.
*/
#include "../include/wav_decoder.h"
#include "../include/utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h> // for int16_t and int32_t

// A RIFF chunk header: a four-character tag and the size of the payload that
// follows it. The file is a "RIFF" chunk whose payload starts with "WAVE",
// followed by any number of subchunks -- "fmt " and "data" are the two that
// matter, but real files also carry "LIST", "fact" and others, in any order.
// Walking the chunks is what makes those load; assuming the data always
// begins at byte 44 is what made them not.
struct riff_chunk {
    char     id[4];
    uint32_t size;
};

// The body of a "fmt " chunk, PCM flavour.
struct wav_format {
    uint16_t audioFormat;      // 1 = PCM
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

static int tag_is(const char id[4], const char *tag) {
    return memcmp(id, tag, 4) == 0;
}

int audio_wav_load(unsigned int buffer, char const * filename, float *outSeconds) {
    if (outSeconds) {
        *outSeconds = 0.0f;
    }

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        clove_error("Can't read input file %s\n", filename);
        return 0;
    }

    struct riff_chunk riff;
    char waveTag[4];
    if (fread(&riff, sizeof(riff), 1, file) < 1 || fread(waveTag, 1, 4, file) < 4) {
        clove_error("Can't read input file header %s\n", filename);
        fclose(file);
        return 0;
    }

    // memcmp, not strcmp: the tag is four characters with no terminator, and
    // writing one over the last byte turned "RIFF" into "RIF", so the check
    // below rejected every valid wav file.
    if (!tag_is(riff.id, "RIFF") || !tag_is(waveTag, "WAVE")) {
        clove_error("File: %s is not of type 'RIFF'/'WAVE'!\n", filename);
        fclose(file);
        return 0;
    }

    struct wav_format fmt;
    int haveFormat = 0;
    unsigned char *data = NULL;
    uint32_t dataSize = 0;

    struct riff_chunk chunk;
    while (fread(&chunk, sizeof(chunk), 1, file) == 1) {
        long next = ftell(file) + (long)chunk.size + (chunk.size & 1); // chunks pad to even

        if (tag_is(chunk.id, "fmt ") && chunk.size >= sizeof(fmt)) {
            if (fread(&fmt, sizeof(fmt), 1, file) < 1) {
                break;
            }
            haveFormat = 1;
        } else if (tag_is(chunk.id, "data") && chunk.size > 0) {
            data = malloc(chunk.size);
            if (data == NULL) {
                clove_error("Out of memory reading %s\n", filename);
                fclose(file);
                return 0;
            }
            dataSize = (uint32_t)fread(data, 1, chunk.size, file);
            if (dataSize == 0) {
                free(data);
                data = NULL;
            }
        }

        if (fseek(file, next, SEEK_SET) != 0) {
            break;
        }
    }

    fclose(file);

    if (!haveFormat || data == NULL) {
        clove_error("File: %s has no 'fmt ' or no 'data' chunk\n", filename);
        free(data);
        return 0;
    }

    if (fmt.audioFormat != 1) {
        clove_error("File: %s is compressed wav (format %d); only PCM is supported\n",
                    filename, (int)fmt.audioFormat);
        free(data);
        return 0;
    }

    // The bit depth is bitsPerSample. The old code read the "fmt " chunk's
    // *size* (16 for PCM) and treated it as the depth, which happened to pick
    // the 16-bit branch for everything and mislabelled every 8-bit file.
    ALenum format;
    if (fmt.bitsPerSample == 8) {
        format = (fmt.channels == 2) ? AL_FORMAT_STEREO8 : AL_FORMAT_MONO8;
    } else if (fmt.bitsPerSample == 16) {
        format = (fmt.channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    } else {
        clove_error("File: %s has %d bits per sample; only 8 and 16 are supported\n",
                    filename, (int)fmt.bitsPerSample);
        free(data);
        return 0;
    }

    // dataSize, not the RIFF chunk's total length: handing OpenAL a size
    // larger than the buffer made it read past the end of the allocation.
    alBufferData(buffer, format, data, (ALsizei)dataSize, (ALsizei)fmt.sampleRate);

    /* The length has to come from here rather than from alGetBufferi(AL_SIZE):
     * mojoAL reports that in its own float32 representation while AL_BITS
     * stays the source file's depth, so the two do not divide into each other
     * and the answer came out exactly twice too long. */
    if (outSeconds) {
        int frameBytes = (int) fmt.channels * ((int) fmt.bitsPerSample / 8);
        if (frameBytes > 0 && fmt.sampleRate > 0) {
            *outSeconds = (float) dataSize / (float) (frameBytes * (int) fmt.sampleRate);
        }
    }

    // alBufferData copies, so the decoded file does not have to stay around.
    // It used to leak in full, once per source.
    free(data);

    return 1;
}
