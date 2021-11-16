package audio_hidl

import (
    //"fmt"
    _"reflect"
    "android/soong/android"
    "android/soong/cc"
    //"github.com/google/blueprint/proptools"
    _ "runtime/debug"
    "strconv"
)

func init() {
    android.RegisterModuleType("audio_libamlaudioports_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {
    sdkVersion := ctx.DeviceConfig().PlatformVndkVersion()
    sdkVersionInt,err := strconv.Atoi(sdkVersion)
    if err != nil {
        //fmt.Printf("%v fail to convert", sdkVersionInt)
    } else {
        //fmt.Println("Audio HIDL sdkVersion:", sdkVersionInt)
    }
    type propsE struct {
        Shared_libs  []string
        Include_dirs []string
        Header_libs  []string
    }
    p := &propsE{}

    if sdkVersionInt > 30  {
        p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal.7.0")
        p.Header_libs =  append(p.Header_libs, "av-headers")
        p.Include_dirs =  append(p.Include_dirs, "hardware/amlogic/audio/utils/hidl_interface_7_0/include")
    } else if sdkVersionInt > 29 {
        p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal.6.0")
        p.Include_dirs =  append(p.Include_dirs, "frameworks/av/include")
        p.Include_dirs =  append(p.Include_dirs, "frameworks/av/media/libaudiohal/include")
    }
    ctx.AppendProperties(p)
}



func audio_hidl_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, audio_hidl_Defaults)
    return module
}

