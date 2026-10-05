#include "Maintenance.h"
#include <cstdio>
#include <cstring>
namespace RiscProvision {
namespace {int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}}
void Maintenance::reset(){if(bytes)for(uint32_t i=0;i<capacity;++i)static_cast<volatile uint8_t*>(bytes)[i]=0;profile=time=used=length=challenge=0;receiving=discard=false;memset(line,0,sizeof(line));memset(digest,0,sizeof(digest));}
void Maintenance::poll(){if((challenge||receiving||length||discard)&&uint32_t(port.now(port.context)-started)>=30000u){reset();port.reply(port.context,"RTE_INSTALL TIMEOUT");}}
void Maintenance::feed(uint8_t value){
 poll();if(!receiving&&(!length||discard)){if(!discard)started=port.now(port.context);}
 if(receiving){bytes[used++]=value;if(used==profile+time)complete();return;}
 if(value=='\n'){if(discard){reset();port.reply(port.context,"RTE_INSTALL INVALID");return;}line[length]=0;command();length=0;return;}
 if(discard)return;
 if(value<' '||value>126||length+1>=sizeof(line)){discard=true;return;}line[length++]=char(value);
}
void Maintenance::command(){
 if(!strcmp(line,"HELLO")){reset();challenge=port.nonce(port.context);if(!challenge)challenge=1;started=port.now(port.context);
   char reply[128];snprintf(reply,sizeof(reply),"RTE_MAINTENANCE_V1 %s %08lx",port.source,static_cast<unsigned long>(challenge));port.reply(port.context,reply);return;}
 unsigned nonce=0,p=0,t=0;char sha[65]{},extra=0;
 if(!challenge||sscanf(line,"INSTALL %x %u %u %64s %c",&nonce,&p,&t,sha,&extra)!=4||nonce!=challenge||!p||p>ProfileInputBytes||t>384||p+t>capacity||strlen(sha)!=64){reset();port.reply(port.context,"RTE_INSTALL INVALID");return;}
 char canonical[192];snprintf(canonical,sizeof(canonical),"INSTALL %08x %u %u %s",nonce,p,t,sha);
 if(strcmp(canonical,line)){reset();port.reply(port.context,"RTE_INSTALL INVALID");return;}
 for(unsigned i=0;i<32;++i){int a=hex(sha[2*i]),b=hex(sha[2*i+1]);if(a<0||b<0){reset();port.reply(port.context,"RTE_INSTALL INVALID");return;}digest[i]=uint8_t(a*16+b);}
 profile=p;time=t;used=0;receiving=true;challenge=0;started=port.now(port.context);
}
void Maintenance::complete(){
 uint8_t actual[32];if(!port.hash(port.context,bytes,used,actual)||memcmp(actual,digest,32)){reset();port.reply(port.context,"RTE_INSTALL CORRUPT");return;}
 auto result=port.install(port.context,bytes,profile,bytes+profile,time);reset();
 const char* status=result==InstallResult::Installed?"RTE_INSTALL INSTALLED":result==InstallResult::Unchanged?"RTE_INSTALL UNCHANGED":result==InstallResult::SelectionUnknown?"RTE_INSTALL UNKNOWN":"RTE_INSTALL REJECTED";
 port.reply(port.context,status);
}
}
