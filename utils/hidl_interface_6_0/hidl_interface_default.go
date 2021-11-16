package audio_hidl

import (
     //"fmt"
    _"reflect"
    "android/soong/android"
    "android/soong/cc"
    "github.com/google/blueprint/proptools"
    _ "runtime/debug"
    "strconv"
)

func init() {
    android.RegisterModuleType("audio_hidl_6_0_go_defaults", audio_hidl_DefaultsFactory)
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
        Enabled *bool
    }
    p := &propsE{}

    if sdkVersionInt > 30  {
        p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal@7.0")
        p.Enabled = proptools.BoolPtr(false)
    } else if sdkVersionInt > 29 {
        p.Shared_libs =  append(p.Shared_libs, "libamlaudiohal@6.0")
        p.Enabled = proptools.BoolPtr(true)
    }
    ctx.AppendProperties(p)
}



func audio_hidl_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, audio_hidl_Defaults)
    return module
}

