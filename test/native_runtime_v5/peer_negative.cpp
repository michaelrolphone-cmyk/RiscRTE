#include "bootstrap/Runtime.h"
namespace RiscBoot { class RuntimeNativeCallerTestPeer {
 static auto& inspect(Runtime& r){return r.nativeOwners_;}
 static bool stage(RuntimeProviders::GraphV2& g,RuntimeProviders::GrantV2& grant,RuntimePackages::DeviceProviderExecutorV2::NativeOwner& owner){return RuntimePackages::DeviceProviderExecutorV2::stageNativeOwner(g,"x","y",1,0,grant,owner);}
}; }
