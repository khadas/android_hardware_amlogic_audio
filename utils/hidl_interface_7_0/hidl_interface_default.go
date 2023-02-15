package audio_hidl

import (
    //"fmt"
    //"reflect"
    "android/soong/android"
    "android/soong/cc"
    "github.com/google/blueprint/proptools"
    //"runtime/debug"
    "strconv"
)

func init() {
    android.RegisterModuleType("audio_hidl_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {
    type propsE struct {
        Shared_libs  []string
        Enabled *bool
        Cflags []string
    }
    p := &propsE{}

    PlatformVndkVersion := ctx.DeviceConfig().PlatformVndkVersion()
    //fmt.Println("PlatformVndkVersion:", PlatformVndkVersion)
    IntPlatformVndkVersion,err := strconv.Atoi(PlatformVndkVersion)
    // For An Android letter, before freeze API PlatformVndkVersion return code name, like
    // "Tiramisu", after freeze API it has been changed to number.

    if err != nil {
        //fmt.Println("Disable HIDL 7.0 Impl")
        p.Enabled = proptools.BoolPtr(true)
    } else {
        SDKVERSION := "-DANDROID_PLATFORM_SDK_VERSION=" + PlatformVndkVersion
        p.Cflags  = append(p.Cflags,SDKVERSION)
        if IntPlatformVndkVersion == 30 {
            //fmt.Println("Disable HIDL 7.0 Impl")
            p.Enabled = proptools.BoolPtr(false)
        } else {
            //fmt.Println("Enable HIDL 7.0 Impl")
            p.Enabled = proptools.BoolPtr(true)
            // Android U changed the API
        }
    }

    ctx.AppendProperties(p)
}

func audio_hidl_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, audio_hidl_Defaults)
    return module
}

