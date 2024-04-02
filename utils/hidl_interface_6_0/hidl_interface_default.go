package audio_hidl

import (
	//"fmt"
	//"reflect"
	"android/soong/android"
	"android/soong/cc"

	"github.com/google/blueprint/proptools"
	//"runtime/debug"
	//"strconv"
)

func init() {
	android.RegisterModuleType("audio_hidl_6_0_go_defaults", audio_hidl_DefaultsFactory)
}

func audio_hidl_Defaults(ctx android.LoadHookContext) {
	type propsE struct {
		Shared_libs []string
		Enabled     *bool
	}
	p := &propsE{}

	p.Shared_libs = append(p.Shared_libs, "libamlaudiohal@7.0")
	p.Enabled = proptools.BoolPtr(false)

	ctx.AppendProperties(p)
}

func audio_hidl_DefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, audio_hidl_Defaults)
	return module
}
