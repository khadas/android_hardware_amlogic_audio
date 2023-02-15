package audio_hidl_7_0

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
    android.RegisterModuleType("audio_hidl_7_0_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {

    PlatformVndkVersion := ctx.DeviceConfig().PlatformVndkVersion()
    //fmt.Println("PlatformVndkVersion:", PlatformVndkVersion)
    IntPlatformVndkVersion,err := strconv.Atoi(PlatformVndkVersion)
    // For An Android letter, before freeze API PlatformVndkVersion return code name, like
    // "Tiramisu", after freeze API it has been changed to number.

    if err != nil {
        type propsE struct {
            Enabled *bool
            Cflags []string
            Defaults []string
        }
        p := &propsE{}
        //fmt.Println("Enable HIDL 7.0 Impl")
        p.Enabled = proptools.BoolPtr(true)
        p.Defaults = append(p.Defaults, "latest_android_media_audio_common_types_cpp_export_shared")
        ctx.AppendProperties(p)
    } else {
        SDKVERSION := "-DANDROID_PLATFORM_SDK_VERSION=" + PlatformVndkVersion
        // Android R can't support defaults export, so we use different propsE
        if IntPlatformVndkVersion == 30 {
            type propsE struct {
                Enabled *bool
                Cflags []string
            }
            p := &propsE{}
            p.Cflags  = append(p.Cflags,SDKVERSION)
            //fmt.Println("Disable HIDL 7.0 Impl")
            p.Enabled = proptools.BoolPtr(false)
            ctx.AppendProperties(p)
        } else {
            type propsE struct {
                Enabled *bool
                Cflags []string
                Defaults []string
            }
            p := &propsE{}
            p.Cflags  = append(p.Cflags,SDKVERSION)
            //fmt.Println("Enable HIDL 7.0 Impl")
            p.Enabled = proptools.BoolPtr(true)
            // Android U changed the API
            if (IntPlatformVndkVersion > 33) {
                p.Defaults = append(p.Defaults, "latest_android_media_audio_common_types_cpp_export_shared")
            }
            ctx.AppendProperties(p)
        }
    }

}

func audio_hidl_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, audio_hidl_Defaults)
    return module
}

