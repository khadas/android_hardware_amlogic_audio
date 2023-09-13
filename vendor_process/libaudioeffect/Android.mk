# Copyright (C) 2021 Amlogic Corporation.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# !!! Note: Starting with Android U libaudioeffect will only be compiled at Audio HAL
# Another libaudioeffect copies still exist and will only be build before Android U
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 33 && echo OK),OK)
LOCAL_CFLAGS += -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)
include $(call all-subdir-makefiles)
endif

