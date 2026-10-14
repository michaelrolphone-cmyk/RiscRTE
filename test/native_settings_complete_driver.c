/* Test-only transport imaging. The actual driver still formats the initial
 * synthetic sector medium, starts through Runtime and runs all I/O via CpuPort.
 * Normal imaging requires closed sessions. The separate power-cut snapshot reads
 * physical model sectors after the queued device operation completed, without
 * cleanup/flush/retry, including a live uncollected publication session. */
#include "native_sd_port_fixture.c"
#include <unistd.h>
void native_settings_sd_load(const char *path) {
 assert(!mounted&&!card_opened&&!async_adapter.session&&!has_handles());
 FILE *in=fopen(path,"rb");assert(in);
 assert(fread(card_image,512,card_sectors,in)==card_sectors);
 assert(fgetc(in)==EOF&&!fclose(in));
}
static void snapshot_sectors(const char *path) {
 FILE *out=fopen(path,"wb");assert(out);
 for(uint32_t i=0;i<card_sectors;++i){const unsigned char*sector=card_image+512*i;unsigned nonzero=0;for(unsigned j=0;j<512;++j)nonzero|=sector[j];if(nonzero){assert(!fseek(out,512L*i,SEEK_SET));assert(fwrite(sector,1,512,out)==512);}}
 assert(!fflush(out)&&!ftruncate(fileno(out),512L*card_sectors)&&!fclose(out));
}
void native_settings_sd_save(const char*path){assert(!async_adapter.session&&!has_handles());snapshot_sectors(path);}
extern void native_settings_sd_crash_media(void (*snapshot)(const char*));
extern void native_settings_sd_media(void (*load)(const char*),void (*save)(const char*));
__attribute__((constructor)) static void register_media_fixture(void){
 native_settings_sd_media(native_settings_sd_load,native_settings_sd_save);native_settings_sd_crash_media(snapshot_sectors);
}
