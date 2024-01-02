package audio_hidl

import (
    //"fmt"
    //"reflect"
    "android/soong/android"
    "android/soong/cc"
    //"github.com/google/blueprint/proptools"
    //"runtime/debug"
    "strconv"
)

func init() {
    android.RegisterModuleType("audio_utils_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {
    type propsE struct {
        Shared_libs  []string
        Header_libs  []string
        Include_dirs []string
        Cflags []string
    }
    p := &propsE{}

    PlatformVndkVersion := ctx.DeviceConfig().PlatformVndkVersion()
    //fmt.Println("PlatformVndkVersion:", PlatformVndkVersion)
    IntPlatformVndkVersion,err := strconv.Atoi(PlatformVndkVersion)
    // For An Android letter, before freeze API PlatformVndkVersion return code name, like
    // "Tiramisu", after freeze API it has been changed to number.

    if err != nil {
        //fmt.Println("Add lib&include dir for HIDL 7.0")
        p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal.7.0")
        p.Header_libs =  append(p.Header_libs, "libamlaudiohal_headers@7.0")
        p.Header_libs =  append(p.Header_libs, "av-headers")
    } else {
        SDKVERSION := "-DANDROID_PLATFORM_SDK_VERSION=" + PlatformVndkVersion
        p.Cflags  = append(p.Cflags,SDKVERSION)
        if IntPlatformVndkVersion == 30 {
        //fmt.Println("Add lib&include dir for HIDL 6.0")
            p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal.6.0")
            p.Header_libs =  append(p.Header_libs, "libamlaudiohal_headers@6.0")
            p.Include_dirs =  append(p.Include_dirs, "frameworks/av/include")
        } else {
            //fmt.Println("Add lib&include dir for HIDL 7.0")
            p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal.7.0")
            p.Header_libs =  append(p.Header_libs, "libamlaudiohal_headers@7.0")
            p.Header_libs =  append(p.Header_libs, "av-headers")
        }
    }

    ctx.AppendProperties(p)
}

func audio_hidl_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, audio_hidl_Defaults)
    return module
}

