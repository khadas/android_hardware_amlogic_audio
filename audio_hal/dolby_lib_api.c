/*
 * Copyright (C) 2017 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_hal_api"
//#define LOG_NDEBUG 0

#include <stdio.h>
#include <unistd.h>
#include <cutils/log.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "dolby_lib_api.h"

#ifndef RET_OK
#define RET_OK 0
#endif

#ifndef RET_FAIL
#define RET_FAIL -1
#endif

#ifndef MS12_V24_ENABLE
    #define MS12_VERSION    "1.3"
#else
    #define MS12_VERSION    "2.4"
#endif

#define SOURCE_FILE    DOLBY_MS12_LIB_PATH_A
#define MID_DEV         "/dev/audio_utils"
#define FINAL_SO        "/dev/audio_utils"

#define AUDIO_UTILS_IOC_MAGIC        'T'
#define AUDIO_UTILS_IOC_SET_LIB_SIZE       _IOW(AUDIO_UTILS_IOC_MAGIC, 0x00, uint32_t)
#define AUDIO_UTILS_IOC_WRITE_LIB          _IOW(AUDIO_UTILS_IOC_MAGIC, 0x01, uint32_t)
#define AUDIO_UTILS_IOC_FREE_LIB           _IOW(AUDIO_UTILS_IOC_MAGIC, 0x02, uint32_t)
static bool b_dolby_written = 0;
static aml_so_type_t s_aml_so_type = AML_SO_TYPE_NONE;

static bool get_dev_audio_utils_node()
{
    int ret = false;
    int fd = 0;

    if (property_get_bool("ro.vendor.platform.disable.audio_utils", false)) {
        ALOGI("%s disabled", __func__);
        return false;
    }


    fd = open(MID_DEV, O_RDONLY);
    if (fd < 0) {
        ALOGE("DEV(%s) do not exit, Error opening because %s\n", MID_DEV, strerror(errno));
        ret = false;
    }
    else {
        ALOGI("DEV(%s) exits!\n", MID_DEV);
        ret = true;
        close(fd);
    }

    return ret;
}

/*
 *@brief file_accessible
 */
static int file_accessible(char *path)
{
    // file is readable or not
    if (path && access(path, R_OK) == 0) {
        ALOGI("%s path: %s\n", __func__, path);
        return RET_OK;
    } else {
        return RET_FAIL;
    }
}

char * get_ms12_path (void)
{
#ifndef MS12_V24_ENABLE
    ALOGI("%s return lib %s", __func__, DOLBY_MS12_LIB_PATH_A);
    return DOLBY_MS12_LIB_PATH_A;
#else
    if (get_dev_audio_utils_node() == false) {
        ALOGI("%s return lib %s", __func__, DOLBY_MS12_LIB_PATH_A);
        return DOLBY_MS12_LIB_PATH_A;
    }
    else {
        ALOGI("%s return lib %s", __func__, FINAL_SO);
        return FINAL_SO;
    }
#endif
}

bool is_ms12_lib_match(void *hDolbyMS12LibHandle) {
    bool b_match = false;
    char * (*FunDolbMS12GetVersion)(void) = NULL;

    /*get dolby version*/
    if (hDolbyMS12LibHandle) {
        FunDolbMS12GetVersion = (char * (*)(void)) dlsym(hDolbyMS12LibHandle, "ms12_get_version");
        if (FunDolbMS12GetVersion) {
            if (strstr((*FunDolbMS12GetVersion)(), MS12_VERSION) != NULL) {
                b_match = true;
            }
            if (b_match == false) {
                ALOGE("ms12 doesn't match build version =%s lib %s", MS12_VERSION, (*FunDolbMS12GetVersion)());
            } else {
                ALOGI("ms12 match build version =%s lib %s", MS12_VERSION, (*FunDolbMS12GetVersion)());
            }
        } else {
            b_match = false;
            ALOGE("ms12 version not found, try ddp lib");
        }
    }
    return b_match;

}


int file_size(char *name)
{
    struct stat statbuf;
    int ret;

    ret = stat(name, &statbuf);
    if (ret != 0)
        return -1;

    return statbuf.st_size;
}

int write_so_to_dev(void)
{
    int fsize;
    void *buffer;
    int audio_utils_fd, source_file;
    int ret = 0;
    int total_read = 0;

    if (b_dolby_written) {
        ALOGI("%s already written", __func__);
        return 0;
    }

    fsize = file_size(SOURCE_FILE);
    if (fsize < 0) {
        ALOGE("%s line %d fsize %d return -1!\n", __func__, __LINE__, fsize);
        return -1;
    }

    buffer = malloc(fsize);
    if (!buffer) {
        ALOGE("%s line %d malloc failed, return -1!\n", __func__, __LINE__);
        return -1;
    }

    source_file = open(SOURCE_FILE, O_RDONLY);
    if (source_file < 0) {
        ALOGE("%s line %d (%s) open failed because %s, return -1\n", __func__, __LINE__, SOURCE_FILE, strerror(errno));
        if (buffer) {
            free(buffer);
            buffer = NULL;
        }
        return -1;
    }
    do {
        ret = read(source_file, (char *)buffer + total_read, fsize - total_read);
        if (ret <= 0)
            break;
        total_read += ret;
        ALOGV("%s total read =%d left=%d", __func__, total_read, fsize - total_read);
    } while (total_read < fsize);
    close(source_file);
    if (total_read != fsize) {
        ALOGE("%s read error total_read=%d need =%d", __func__, total_read, fsize);
        goto exit;
    }

    audio_utils_fd = open(MID_DEV, O_RDWR);
    if (audio_utils_fd < 0) {
        ALOGE("%s line %d (%s) open failed because %s\n", __func__, __LINE__, MID_DEV, strerror(errno));
        if (buffer) {
            free(buffer);
            buffer = NULL;
        }
        return -1;
    }
    ioctl(audio_utils_fd, AUDIO_UTILS_IOC_SET_LIB_SIZE, fsize);
    ret = ioctl(audio_utils_fd, AUDIO_UTILS_IOC_WRITE_LIB, buffer);
    close(audio_utils_fd);

    if (ret < 0) {
        ALOGE("%s write lib error=%d", __func__, ret);
        goto exit;
    }
    b_dolby_written = true;
exit:
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }
    return 0;
}


void release_dolby_dev() {
    int audio_utils_fd = 0;
    if (!b_dolby_written) {
        return;
    }
    audio_utils_fd = open(MID_DEV, O_RDONLY);
    if (audio_utils_fd >= 0) {
        ALOGI("%s DEV(%s) release!\n", __func__, MID_DEV);
        ioctl(audio_utils_fd, AUDIO_UTILS_IOC_FREE_LIB);
        close(audio_utils_fd);
    }
    else if (audio_utils_fd < 0) {
        ALOGE("%s line %d (%s) open failed because %s\n", __func__, __LINE__, MID_DEV, strerror(errno));
    }

    b_dolby_written = false;
    return;
}


/*
 *@brief detect_dolby_lib_type
 */
enum eDolbyLibType detect_dolby_lib_type(void) {
    enum eDolbyLibType retVal = eDolbyNull;

    void *hDolbyMS12LibHandle = NULL;
    void *hDolbyDcvLibHandle = NULL;

    // the priority would be "MS12 > DCV" lib
    ALOGI("%s return lib %s", __func__, DOLBY_MS12_LIB_PATH_A);
    if (RET_OK == file_accessible(DOLBY_MS12_LIB_PATH_A)) {
        ALOGI("%s file_accessible got ok", __func__);
        retVal = eDolbyMS12Lib;
    }

    // MS12 is first priority
    if (eDolbyMS12Lib == retVal)
    {
        ALOGI("%s line %d try to dlopen the Dolby MS12 Library!", __func__, __LINE__);
#ifndef MS12_V24_ENABLE
        //MS12 V1
        hDolbyMS12LibHandle = dlopen(DOLBY_MS12_LIB_PATH_A, RTLD_NOW);
#else
        //MS12 V2
        if (get_dev_audio_utils_node() == true) {
            if (write_so_to_dev() == 0) {
                ALOGI("%s,Write %s to %s success\n", __FUNCTION__, DOLBY_MS12_LIB_PATH_A, FINAL_SO);
                hDolbyMS12LibHandle = dlopen(FINAL_SO, RTLD_NOW);
            }
            else {
                ALOGE("%s,Write %s to %s failed\n", __FUNCTION__, DOLBY_MS12_LIB_PATH_A, FINAL_SO);
            }
        }
        else {
            hDolbyMS12LibHandle = dlopen(DOLBY_MS12_LIB_PATH_A, RTLD_NOW);
        }
#endif

        if (hDolbyMS12LibHandle != NULL)
        {
            bool b_match = is_ms12_lib_match(hDolbyMS12LibHandle);
            dlclose(hDolbyMS12LibHandle);
            hDolbyMS12LibHandle = NULL;

            /*check ms12 version*/
            if (b_match) {
                ALOGI("%s,FOUND libdolbyms12 lib\n", __FUNCTION__);
                return eDolbyMS12Lib;
            }
        }
        else {
            ALOGE("%s, failed to FIND libdolbyms12.so, %s\n", __FUNCTION__, dlerror());
        }
    }

    // dcv is second priority
    if (RET_OK == file_accessible(DOLBY_DCV_LIB_PATH_A) || RET_OK == file_accessible(DOLBY_DCV_LIB64_PATH_A)) {
        retVal = eDolbyDcvLib;
    } else {
        retVal = eDolbyNull;
    }

    s_aml_so_type = AML_SO_TYPE_NONE;
    if (eDolbyDcvLib == retVal)
    {
        //try to open lib see if it's OK?
        hDolbyDcvLibHandle  = dlopen(DOLBY_DCV_LIB_PATH_A, RTLD_NOW);
        //ALOGI("%s, 32bit lib:%s, hDolbyDcvLibHandle:%p\n", __FUNCTION__, DOLBY_DCV_LIB_PATH_A, hDolbyDcvLibHandle);

        //open 32bit so failed, here try to open the 64bit dolby dcv so.
        if (hDolbyDcvLibHandle == NULL) {
            hDolbyDcvLibHandle = dlopen(DOLBY_DCV_LIB64_PATH_A, RTLD_NOW);
            if (hDolbyDcvLibHandle != NULL)
                s_aml_so_type = AML_SO_TYPE_64bit;
            ALOGI("%s, 64bit lib:%s, hDolbyDcvLibHandle:%p\n", __FUNCTION__, DOLBY_DCV_LIB64_PATH_A, hDolbyDcvLibHandle);
        } else {
            s_aml_so_type = AML_SO_TYPE_32bit;
        }
    }

    if (hDolbyDcvLibHandle != NULL)
    {
        dlclose(hDolbyDcvLibHandle);
        hDolbyDcvLibHandle = NULL;
        ALOGI("%s,FOUND libHwAudio_dcvdec lib\n", __FUNCTION__);
        return eDolbyDcvLib;
    }

    ALOGE("%s, failed to FIND libdolbyms12.so and libHwAudio_dcvdec.so, %s\n", __FUNCTION__, dlerror());
    return eDolbyNull;
}

int dolby_lib_decode_enable(eDolbyLibType_t lib_type) {
    int enable = 0;
    if (lib_type == eDolbyMS12Lib) {
        enable = 1;
    } else if (lib_type == eDolbyDcvLib) {
        unsigned int filesize = -1;
        struct stat stat_info;
        int ret = 0;

        switch (s_aml_so_type) {
            case AML_SO_TYPE_32bit:
                ret = stat(DOLBY_DCV_LIB_PATH_A, &stat_info);
                break;
            case AML_SO_TYPE_64bit:
                ret = stat(DOLBY_DCV_LIB64_PATH_A, &stat_info);
                break;
            default:
                ret = -1;//dlopen failed, so enable should be 0;
        }

        if (ret < 0) {
            enable = 0;
            ALOGE("%s %d, s_aml_so_type:%d errno:%s", __func__, __LINE__, s_aml_so_type, strerror(errno));
        } else {
            filesize = stat_info.st_size;
            if (filesize > 500*1024) {
                enable = 1;
            } else {
                enable = 0;
            }
        }
    } else {
        enable = 0;
    }

    ALOGI("%s %d, enable:%d\n", __FUNCTION__, __LINE__, enable);
    return enable;
}


#ifndef MS12_V24_ENABLE
typedef enum ms_dap_mode_t
{
    DAP_NO_PROC = 0,
    DAP_CONTENT_PROC = 1,
    DAP_DEVICE_PROC = 2,
    DAP_DEVICE_CONTENT_PROC = DAP_DEVICE_PROC | DAP_CONTENT_PROC,
    DAP_SI_PROC = 4,
} ms_dap_mode_t;

int get_ms12_dap_init_mode(bool is_tv)
{
    int dap_init_mode = 0;

    if (is_tv) {
        dap_init_mode = DAP_SI_PROC;
    }
    else {
        dap_init_mode = DAP_NO_PROC;
    }

    return dap_init_mode;
}

bool is_ms12_tuning_dat_in_dut() //Invalid in Dolby MS12 V1.3
{
    return false;
}
#else

typedef enum ms_dap_mode_t
{
    DAP_NO_PROC = 0,
    DAP_CONTENT_PROC = 1,
    DAP_CONTENT_PROC_DEVICE_PROC = 2
} ms_dap_mode_t;

int get_ms12_dap_init_mode(bool is_tv)
{
    int dap_init_mode = 0;

    if (is_tv) {
        dap_init_mode = DAP_CONTENT_PROC_DEVICE_PROC;
    }
    else {
        dap_init_mode = DAP_NO_PROC;
    }

    return dap_init_mode;
}

bool is_ms12_tuning_dat_in_dut() //available in Dolby MS12 V2.4 or later
{
    if (file_accessible(DOLBY_TUNING_DAT) == 0)
        return true;
    else
        return false;
}

#endif

eDTSLibType_t detect_dts_lib_type(void) {
    void *hDTSLibHanle = NULL;

    // the priority would be "DTSX > DTSHD" lib
    // DTSX is first priority
    if (file_accessible(DTS_X_LIB_PATH_A) == 0) {
        // try to open lib see if it's OK?
        hDTSLibHanle = dlopen(DTS_X_LIB_PATH_A, RTLD_NOW);
        if (hDTSLibHanle != NULL) {
            dlclose(hDTSLibHanle);
            hDTSLibHanle = NULL;
            ALOGI("[%s:%d] Found libHwAudio_dtsx lib", __func__, __LINE__);
            return eDTSXLib;
        }
    }

    // DTSHD is second priority
    if (file_accessible(DTS_HD_LIB_PATH_A) == 0) {
        // try to open lib see if it's OK?
        hDTSLibHanle = dlopen(DTS_HD_LIB_PATH_A, RTLD_NOW);
        if (hDTSLibHanle != NULL) {
            dlclose(hDTSLibHanle);
            hDTSLibHanle = NULL;
            ALOGI("[%s:%d] Found libHwAudio_dtshd lib", __func__, __LINE__);
            return eDTSHDLib;
        }
    }

    ALOGW("[%s:%d] Failed to find libHwAudio_dtsx.so and libHwAudio_dtshd.so, %s", __FUNCTION__, __LINE__, dlerror());
    return eDTSNull;
}

int dts_lib_decode_enable() {
    int enable = 0;
    unsigned int filesize = -1;
    struct stat stat_info = {0};
    void *hDtsLibHandle = NULL;
    int ret = 0;

    //try to open lib see if it's OK?
    s_aml_so_type = AML_SO_TYPE_NONE;
    hDtsLibHandle  = dlopen(DTS_HD_LIB_PATH_A, RTLD_NOW);
    ALOGI("%s, 32bit lib:%s, hDtsLibHandle:%p\n", __FUNCTION__, DTS_HD_LIB_PATH_A, hDtsLibHandle);

    //open 32bit so failed, here try to open the 64bit dolby dcv so.
    if (hDtsLibHandle == NULL) {
        hDtsLibHandle = dlopen(DTS_HD_LIB64_PATH_A, RTLD_NOW);
        if (hDtsLibHandle != NULL) {
            s_aml_so_type = AML_SO_TYPE_64bit;
        }
        ALOGI("%s, 64bit lib:%s, hDolbyDcvLibHandle:%p\n", __FUNCTION__, DTS_HD_LIB64_PATH_A, hDtsLibHandle);
    } else {
        s_aml_so_type = AML_SO_TYPE_32bit;
    }

    if (hDtsLibHandle != NULL)
    {
        dlclose(hDtsLibHandle);
        hDtsLibHandle = NULL;
        ALOGI("%s,FOUND libHwAudio_dtshd lib\n", __FUNCTION__);
    }

    //Here start to get stat info of matching so.
    switch (s_aml_so_type) {
        case AML_SO_TYPE_32bit:
            ret = stat(DTS_HD_LIB_PATH_A, &stat_info);
            break;
        case AML_SO_TYPE_64bit:
            ret = stat(DTS_HD_LIB64_PATH_A, &stat_info);
            break;
        default:
            ret = -1;//dlopen failed, so enable should be 0;
    }

    if (ret < 0) {
        enable = 0;
        ALOGE("%s %d, s_aml_so_type:%d errno:%s", __func__, __LINE__, s_aml_so_type, strerror(errno));
    } else {
        filesize = stat_info.st_size;
        if (filesize > 500*1024) {
            enable = 1;
        } else {
            enable = 0;
        }
    }

    ALOGI("%s %d, enable:%d\n", __FUNCTION__, __LINE__, enable);
    return enable;
}
