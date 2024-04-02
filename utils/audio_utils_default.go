package audio_hidl

import (
	//"fmt"
	//"reflect"
	"android/soong/android"
	"android/soong/cc"
	//"github.com/google/blueprint/proptools"
	//"runtime/debug"
)

func init() {
	android.RegisterModuleType("audio_utils_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {
	type propsE struct {
		Shared_libs  []string
		Header_libs  []string
		Include_dirs []string
		Cflags       []string
	}
	p := &propsE{}

	PlatformSdkVersion := ctx.Config().PlatformSdkVersion().String()

	SDKVERSION := "-DANDROID_PLATFORM_SDK_VERSION=" + PlatformSdkVersion
	p.Cflags = append(p.Cflags, SDKVERSION)

	//fmt.Println("Add lib&include dir for HIDL 7.0")
	p.Shared_libs = append(p.Shared_libs, "libamlaudiohal.7.0")
	p.Header_libs = append(p.Header_libs, "libamlaudiohal_headers@7.0")
	p.Header_libs = append(p.Header_libs, "av-headers")

	ctx.AppendProperties(p)
}

func audio_hidl_DefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, audio_hidl_Defaults)
	return module
}
