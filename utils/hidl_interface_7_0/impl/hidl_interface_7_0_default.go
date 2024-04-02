package audio_hidl_7_0

import (
	//"fmt"
	//"reflect"
	"android/soong/android"
	"android/soong/cc"

	"github.com/google/blueprint/proptools"
	//"runtime/debug"
)

func init() {
	android.RegisterModuleType("audio_hidl_7_0_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {

	PlatformSdkVersion := ctx.Config().PlatformSdkVersion().String()

	SDKVERSION := "-DANDROID_PLATFORM_SDK_VERSION=" + PlatformSdkVersion

	type propsE struct {
		Enabled  *bool
		Cflags   []string
		Defaults []string
	}
	p := &propsE{}
	p.Cflags = append(p.Cflags, SDKVERSION)
	//fmt.Println("Enable HIDL 7.0 Impl")
	p.Enabled = proptools.BoolPtr(true)
	// Android U changed the API
	p.Defaults = append(p.Defaults, "latest_android_media_audio_common_types_cpp_export_shared")
	ctx.AppendProperties(p)

}

func audio_hidl_DefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, audio_hidl_Defaults)
	return module
}
