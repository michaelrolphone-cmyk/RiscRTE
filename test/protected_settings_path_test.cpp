#include <RiscProtectedSettingsPath.h>
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 for(const char*p:{"RTECFG","/RTECFG","/rtecfg/00000006/settings.index","/x/RtEcFg/row","/RTECFG./row","/RTECFG /row","\\RTECFG\\row"})assert(risc_protected_settings_path(p));
 for(const char*p:{"","/","/RTECFG2","/MYRTECFG","/RTECFG.TXT","/AppData/ordinary","/Documents/a"})assert(!risc_protected_settings_path(p));
 assert(!risc_protected_settings_path(nullptr));
 puts("Protected settings paths: case, component, FAT trailing aliases and allowed neighbors PASS");
}
