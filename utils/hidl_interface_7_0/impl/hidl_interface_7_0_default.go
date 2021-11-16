package audio_hidl_7_0

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
    android.RegisterModuleType("audio_hidl_7_0_go_defaults", audio_hidl_DefaultsFactory)
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
        Enabled *bool
    }
    p := &propsE{}

    if sdkVersionInt > 30 {
        p.Enabled = proptools.BoolPtr(true)
    } else if sdkVersionInt > 29 {
        p.Enabled = proptools.BoolPtr(false)
    }
    ctx.AppendProperties(p)
}



func audio_hidl_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, audio_hidl_Defaults)
    return module
}

