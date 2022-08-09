package audio_hidl

import (
    //"fmt"
    //"reflect"
    "android/soong/android"
    "android/soong/cc"
    //"github.com/google/blueprint/proptools"
    //"runtime/debug"
    //"strconv"
)

func init() {
    android.RegisterModuleType("audio_libamlaudioports_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {
    type propsE struct {
        Shared_libs  []string
        Include_dirs []string
        Header_libs  []string
    }
    p := &propsE{}

    // After Android T, PlatformVndkVersion return string like "Tiramisu", not string number like "32"
    PlatformVndkVersion := ctx.DeviceConfig().PlatformVndkVersion()
    //fmt.Println("PlatformVndkVersion:", PlatformVndkVersion)

    if PlatformVndkVersion == "30" {
        //fmt.Println("Add lib&inclue dir for HIDL 6.0")
        p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal.6.0")
        p.Include_dirs =  append(p.Include_dirs, "frameworks/av/include")
        p.Include_dirs =  append(p.Include_dirs, "frameworks/av/media/libaudiohal/include")
    } else {
        //fmt.Println("Add lib&inclue dir for HIDL 7.0")
        p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal.7.0")
        p.Header_libs =  append(p.Header_libs, "av-headers")
        p.Include_dirs =  append(p.Include_dirs, "hardware/amlogic/audio/utils/hidl_interface_7_0/include")
    }

    ctx.AppendProperties(p)
}

func audio_hidl_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, audio_hidl_Defaults)
    return module
}

