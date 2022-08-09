/*
 * Copyright (C) 2019 Amlogic Corporation.
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
 *
 * DESCRIPTION:
 *     to get/set eARC RX/TX latency and Capability Data Structure.
 *
 */
#define LOG_TAG "earc_utils"
//#define LOG_NDEBUG 0


#include <earc_utils.h>

#define CDS_VERSION 0x1
#define CDS_MAX  256

static void earc_cds_conf_to_str(char *earc_cds, char *cds_str, int hex)
{
    char *cds_blocks;
    char *audio_blocks;
    char cds_blockid;
    int blen = 0, dlen = 0, i, j, n = 0, m, index = 0;
    int tag_code = 0;

    if (!cds_str || !earc_cds)
        return;

    cds_str[0] = '\0';
    /* bypass version */
    cds_blocks = &earc_cds[1];
    for (i = 0; i < CDS_MAX - 1;) {
        /* block id */
        cds_blockid = cds_blocks[i];
        if (cds_blockid == 1 || cds_blockid == 2) {
                blen = cds_blocks[1 + i]; /* block length */
                audio_blocks = &cds_blocks[2 + i];
                for (j = 0; j < blen;) {
                    /* CTA-861-G Audio Data Block */
                    ALOGI("%s, tagl:%#x\n", __FUNCTION__, audio_blocks[j]);
                    dlen = audio_blocks[j] & 0x1f; /* length of audio data block */
                    tag_code = (audio_blocks[j] & 0xe0) >> 5;

                    /* so far only get Audio Data Block Tag(tag_code = 1) */
                    for (m = 0; m < dlen && tag_code == 1; m ++) {
                        /* skip 3 bytes which is for pcm format */
                        if (m % 3 == 0 && ((audio_blocks[1 + j + m] >> 3) & 0xf) == 0x1) {
                            m += 2;
                            continue;
                        }
                        if (hex) {
                            cds_str[index++] = audio_blocks[1 + j + m];
                        } else {
                            sprintf(cds_str, "%s%d, ", cds_str, audio_blocks[1 + j + m]);
                        }
                    }
                    if (tag_code == 1)
                        n += dlen;
                    j += dlen + 1;
                    ALOGV("%s, j:%d, cds_str:%s\n", __FUNCTION__, j, cds_str);
                }

                i += blen + 2;
        } else if (cds_blockid == 3) {
            /* ignore now */
        }
        break;
    }

    if (!hex) {
        int length = strlen(cds_str);
        cds_str[length - 2] = '\0';
    }

    ALOGI("%s, bytes:%d, cds_str:%s:end\n", __FUNCTION__, n, cds_str);

}

/*
 * CDS Blocks, Now we support only Block Id = 1
 */
static void earc_cds_str_to_conf(char *cds_str, char *earc_cds)
{
    char *start = cds_str;
    char *end;
    long int data;
    int len = 0;

    if (!cds_str || !earc_cds)
        return;

    /* data for Block ID=1 */
    while (start) {
        data = strtol(start, &end, 16);
        earc_cds[4 + len] = data;
        len ++;
        start = end;
        if (!(*end))
            break;
    }
    ALOGV("%s, cds:%s len:%d\n", __FUNCTION__, cds_str, len);

    /* CDS version */
    earc_cds[0] = CDS_VERSION;

    /* Block ID = 0x1 */
    earc_cds[1] = 0x1;
    /* Block len */
    earc_cds[2] = len + 1;
    /* Tag = 1, length of SADs */
    earc_cds[3] =  (0x1 << 5) | (len & 0x1f);

}

/*
 * fetch CDS from eARC_RX, and will update CDS to EDID
 * cds_str: CTA short audio descriptor
 */
int earctx_fetch_cds(struct aml_mixer_handle *amixer, char *cds_str, int hex)
{
    char earc_cds[CDS_MAX] = {0};

    aml_mixer_ctrl_get_array(amixer, AML_MIXER_ID_EARCTX_CDS, earc_cds, CDS_MAX);

    earc_cds_conf_to_str(earc_cds, cds_str, hex);

    return 0;
}
