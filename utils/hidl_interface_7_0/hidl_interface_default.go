package audio_hidl

import (
	//"fmt"
	//"reflect"
	"android/soong/android"
	"android/soong/cc"

	"github.com/google/blueprint/proptools"
	//"runtime/debug"
)

func init() {
	android.RegisterModuleType("audio_hidl_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {
	type propsE struct {
		Shared_libs []string
		Enabled     *bool
		Cflags      []string
	}
	p := &propsE{}

	PlatformSdkVersion := ctx.Config().PlatformSdkVersion().String()

	SDKVERSION := "-DANDROID_PLATFORM_SDK_VERSION=" + PlatformSdkVersion

	p.Cflags = append(p.Cflags, SDKVERSION)
	p.Enabled = proptools.BoolPtr(true)
	// Android U changed the API

	ctx.AppendProperties(p)
}

func audio_hidl_DefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, audio_hidl_Defaults)
	return module
}
