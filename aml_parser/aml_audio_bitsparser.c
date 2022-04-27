/*
 * Copyright (C) 2020 Amlogic Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "audio_bit_parser"

#include <cutils/log.h>

#include "aml_audio_bitsparser.h"


bool static inline fillReservoir(struct audio_bit_parser * bit_parser)
{
    if (bit_parser->data_size == 0) {
        bit_parser->is_over_read = true;
        return false;
    }

    bit_parser->num_reservoir = 0;
    size_t i;
    for (i = 0; bit_parser->data_size > 0 && i < 4; ++i) {
        bit_parser->num_reservoir = (bit_parser->num_reservoir << 8) | *bit_parser->data_buf;

        ++bit_parser->data_buf;
        --bit_parser->data_size;
    }

    bit_parser->num_bitsleft = 8 * i;
    bit_parser->num_reservoir <<= 32 - bit_parser->num_bitsleft;
    ALOGV("%s num_bitsleft %d num_reservoir %x\n", __FUNCTION__, bit_parser->num_bitsleft, bit_parser->num_reservoir);
    return true;
}

bool static inline getBitsGraceful(struct audio_bit_parser * bit_parser, size_t n, uint32_t *out)
{
    if (n > 32) {
        return false;
    }

    uint32_t result = 0;
    while (n > 0) {
        if (bit_parser->num_bitsleft == 0) {
            if (!fillReservoir(bit_parser)) {
                return false;
            }
        }

        size_t m = n;
        if (m > bit_parser->num_bitsleft) {
            m = bit_parser->num_bitsleft;
        }

        result = (result << m) | (bit_parser->num_reservoir >> (32 - m));
        bit_parser->num_reservoir <<= m;
        bit_parser->num_bitsleft -= m;
        ALOGV("%s num_bitsleft %d num_reservoir %x\n", __FUNCTION__, bit_parser->num_bitsleft, bit_parser->num_reservoir);

        n -= m;
    }
    *out = result;
    ALOGV("%s n %d *out 0x%x\n", __FUNCTION__, n, *out);
    return true;
}



int aml_audio_bitparser_init(struct audio_bit_parser * bit_parser, const uint8_t *buf, size_t size)
{
    if (bit_parser == NULL ||
        buf == NULL ||
        size == 0) {
        ALOGE("%s invalid parameter", __FUNCTION__);
        return -1;
    }

    bit_parser->data_buf       = buf;
    bit_parser->data_size      = size;
    bit_parser->num_reservoir  = 0;
    bit_parser->num_bitsleft   = 0;
    bit_parser->is_over_read   = false;


    return 0;
}

int aml_audio_bitparser_deinit(struct audio_bit_parser * bit_parser)
{
    bit_parser->data_buf       = NULL;
    bit_parser->data_size      = 0;
    bit_parser->num_reservoir  = 0;
    bit_parser->num_bitsleft   = 0;
    bit_parser->is_over_read   = false;

    return 0;
}

int aml_audio_bitparser_getBits(struct audio_bit_parser * bit_parser, size_t n)
{
    uint32_t ret = 0;
    getBitsGraceful(bit_parser, n, &ret);
    return ret;
}


bool aml_audio_bitparser_skipBits(struct audio_bit_parser * bit_parser, size_t n)
{
    uint32_t dummy;
    while (n > 32) {
        if (!getBitsGraceful(bit_parser, 32, &dummy)) {
            return false;
        }
        n -= 32;
    }

    if (n > 0) {
        return getBitsGraceful(bit_parser, n, &dummy);
    }
    return true;
}

void aml_audio_bitparser_putBits(struct audio_bit_parser * bit_parser, uint32_t x, size_t n)
{
    if (bit_parser->is_over_read) {
        return;
    }

    while (bit_parser->num_bitsleft + n > 32) {
        bit_parser->num_bitsleft -= 8;
        --bit_parser->data_buf;
        ++bit_parser->data_size;
    }

    bit_parser->num_reservoir = (bit_parser->num_reservoir >> n) | (x << (32 - n));
    bit_parser->num_bitsleft += n;
    return;
}

void aml_audio_bitparser_readBits_to_buffer(struct audio_bit_parser * bit_parser, char *buffer, int offset, int numBits)
{
    // Whole bytes.
    int to = offset + (numBits >> 3) /* numBits / 8 */;
    int byteOffset = bit_parser->num_bitsleft >> 3;
    int bitOffset = bit_parser->num_bitsleft & 7;
    char *data = (char *)bit_parser->data_buf;
    for (int i = offset; i < to; i++) {
        buffer[i] = (data[byteOffset++] << bitOffset);
        buffer[i] = (buffer[i] | ((data[byteOffset] & 0xFF) >> (8 - bitOffset)));
    }
    // Trailing bits.
    int bitsLeft = numBits & 7 /* numBits % 8 */;
    if (bitsLeft == 0) {
        return;
    }
    // Set bits that are going to be overwritten to 0.
    buffer[to] = (char) (buffer[to] & (0xFF >> bitsLeft));
    if (bitOffset + bitsLeft > 8) {
        // We read the rest of data[byteOffset] and increase byteOffset.
        buffer[to] = (char) (buffer[to] | ((data[byteOffset++] & 0xFF) << bitOffset));
        bitOffset -= 8;
    }
    bitOffset += bitsLeft;
    int lastDataByteTrailingBits = (data[byteOffset] & 0xFF) >> (8 - bitOffset);
    buffer[to] |= (char) (lastDataByteTrailingBits << (8 - bitsLeft));
    if (bitOffset == 8) {
        bitOffset = 0;
        byteOffset++;
    }
    //assertValidOffset();
}


/**
 * Returns the number of bits yet to be read.
 */
int aml_audio_bitparser_bitsLeft(struct audio_bit_parser * bit_parser) {
    //return (byteLimit - byteOffset) * 8 - bitOffset;
    return bit_parser->data_size * 8 - bit_parser->num_bitsleft;
}

/**
 * Returns the current bit offset.
 */
int aml_audio_bitparser_getPosition(struct audio_bit_parser * bit_parser) {
    //return byteOffset * 8 + bitOffset;
    return bit_parser->num_bitsleft;
}


/**
 * Sets the current bit offset.
 *
 * @param position The position to set.
 */
void aml_audio_bitparser_setPosition(struct audio_bit_parser * bit_parser, int position) {
    //byteOffset = position / 8;
    //bitOffset = position - (byteOffset * 8);
    //assertValidOffset();
    bit_parser->num_bitsleft = position;
}


