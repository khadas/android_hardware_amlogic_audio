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

#define LOG_TAG "audio_hal_hidl"
//#define LOG_NDEBUG 0

#include <media/audiohal/DeviceHalInterface.h>
#include <media/audiohal/DevicesFactoryHalInterface.h>
#include <media/audiohal/FactoryHalHidl.h>
#include "aml_android_hidl_utils.h"

namespace android {


using namespace std;

class DevicesFactoryHalInterface;
class DeviceHalInterface;

sp<DevicesFactoryHalInterface> mDevicesFactoryHal;
sp<DeviceHalInterface> hwDevice;
static struct audio_patch gAudioPatch;
static audio_patch_handle_t gAudioPatchHandle = (audio_patch_handle_t)0;
static mutex gGetServiceLock;


#define CHECK_AUDIO_HAL_PROCESS(ret)                                                            \
{                                                                                               \
    lock_guard<mutex> lock(gGetServiceLock);                                                    \
    if (hwDevice == nullptr || mDevicesFactoryHal == nullptr) {                                 \
        getAudioHalService();                                                                   \
        if (hwDevice == nullptr || mDevicesFactoryHal == nullptr) {                             \
            ALOGE("[%s:%d] get audio hal service fail", __func__,__LINE__);                     \
            return ret;                                                                         \
        }                                                                                       \
    }                                                                                           \
}

static void serviceDied() {
    lock_guard<mutex> lock(gGetServiceLock);
    ALOGW("[%s:%d] The service of audio hal has died!!!", __func__, __LINE__);
    mDevicesFactoryHal = nullptr;
    hwDevice = nullptr;
}

static void getAudioHalService() {
    if (mDevicesFactoryHal == nullptr) {
        mDevicesFactoryHal = DevicesFactoryHalInterface::create();
        if (mDevicesFactoryHal == nullptr) {
            ALOGE("[%s:%d] get DevicesFactoryHal fail", __func__, __LINE__);
            return;
        } else {
            ALOGI("[%s:%d] get DevicesFactoryHal success", __func__, __LINE__);
        }
        mDevicesFactoryHal->registerAtExitHandler(mDevicesFactoryHal.get(), serviceDied);
        int rc = mDevicesFactoryHal->openDevice("primary", &hwDevice);
        if (rc == NO_ERROR) {
            ALOGI("[%s:%d] get hwDevice success", __func__, __LINE__);
        } else {
            mDevicesFactoryHal = nullptr;
            ALOGE("[%s:%d] openDevice error, get hwDevice fail", __func__, __LINE__);
            return;
        }

        hwDevice->initCheck();
     }
}

status_t initCheck() {
    CHECK_AUDIO_HAL_PROCESS(INVALID_OPERATION)
    return NO_ERROR;
}

status_t setParameters(const String8& keyValuePairs) {
    status_t err = NO_ERROR;

    CHECK_AUDIO_HAL_PROCESS(INVALID_OPERATION)
    err = hwDevice->setParameters(keyValuePairs);
    ALOGI("setParameters:%s, err=%d", keyValuePairs.string(), err);
    return err;
}

String8 getParameters(const String8& keys) {
    status_t err = NO_ERROR;
    String8 mString = String8("");

    CHECK_AUDIO_HAL_PROCESS(mString)
    err = hwDevice->getParameters(keys, &mString);
    if (err != NO_ERROR) {
        ALOGW("getParameters err: err=%d", err);
    }
    ALOGD("[%s:%d] key:%s", __func__, __LINE__, keys.c_str());

    return mString;
}

status_t openOutputStream(
        audio_io_handle_t handle,
        audio_devices_t deviceType,
        audio_output_flags_t flags,
        struct audio_config *config,
        const char *address,
        sp<StreamOutHalInterface> *outStream) {

    CHECK_AUDIO_HAL_PROCESS(UNKNOWN_ERROR)
    status_t ret = hwDevice->openOutputStream(
             handle,
             deviceType,
             flags,
             config,
             address,
             outStream);
    return ret;
}

status_t createAudioPatch(
        unsigned int num_sources,
        const struct audio_port_config *sources,
        unsigned int num_sinks,
        const struct audio_port_config *sinks,
        audio_patch_handle_t *patch) {
    status_t ret = NO_ERROR;

    CHECK_AUDIO_HAL_PROCESS(UNKNOWN_ERROR)
    ret = hwDevice->createAudioPatch(num_sources,
                            sources,
                            num_sinks,
                            sinks,
                            patch);
    return ret;
}

status_t releaseAudioPatch(audio_patch_handle_t patch) {
    status_t ret = NO_ERROR;

    CHECK_AUDIO_HAL_PROCESS(UNKNOWN_ERROR)
    ret = hwDevice->releaseAudioPatch(patch);
    return ret;
}

}
