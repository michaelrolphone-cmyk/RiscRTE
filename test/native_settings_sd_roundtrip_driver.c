/* Test-only transport imaging. The actual driver still formats the initial
 * synthetic sector medium, starts through Runtime and runs all I/O via CpuPort.
 * Imaging occurs only before mount or after all sessions/handles are released. */
#include "native_sd_port_fixture.c"
void native_settings_sd_load(const char *path) {
 assert(!mounted&&!card_opened&&!async_adapter.session&&!has_handles());
 FILE *in=fopen(path,"rb");assert(in);
 assert(fread(card_image,512,card_sectors,in)==card_sectors);
 assert(fgetc(in)==EOF&&!fclose(in));
}
void native_settings_sd_save(const char *path) {
 assert(!async_adapter.session&&!has_handles());
 FILE *out=fopen(path,"wb");assert(out);
 assert(fwrite(card_image,512,card_sectors,out)==card_sectors&&!fclose(out));
}
extern void native_settings_sd_media(void (*load)(const char*),void (*save)(const char*));
__attribute__((constructor)) static void register_media_fixture(void){
 native_settings_sd_media(native_settings_sd_load,native_settings_sd_save);
}
