#pragma once
/* Pointer-free checkpoint encoding for optional scene clients. No storage or
 * UI service is acquired by this helper. Application payload encoding/schema
 * is caller-owned. Never put grants, callback addresses, frame/session handles,
 * or mount-local storage revisions in the payload. */
#include "RiscSceneV1.h"
#include <string.h>
#define RISC_SCENE_STATE_HEADER 96u
static inline void risc_scene_put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static inline uint32_t risc_scene_get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline uint32_t risc_scene_state_hash(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u;for(size_t i=0;i<n;i++)h=(h^p[i])*16777619u;return h;
}
static inline bool risc_scene_state_encode(uint8_t *out,size_t capacity,
        uint32_t schema,const risc_scene_navigation_v1 *nav,
        const uint8_t *payload,size_t length,size_t *written) {
    if(!out||!nav||!written||!schema||length>4096||(!payload&&length)||
       capacity<RISC_SCENE_STATE_HEADER+length||nav->api_version!=1||
       nav->struct_size!=sizeof(*nav)||nav->reserved||!nav->depth||
       nav->depth>RISC_SCENE_MAX_DEPTH)return false;
    memset(out,0,RISC_SCENE_STATE_HEADER+length);memcpy(out,"RSC1",4);
    risc_scene_put32(out+4,(uint32_t)(RISC_SCENE_STATE_HEADER+length));
    risc_scene_put32(out+8,schema);risc_scene_put32(out+12,nav->depth);
    for(unsigned i=0;i<RISC_SCENE_MAX_DEPTH;i++) {
        if((i<nav->depth&&!nav->routes[i])||
           (i>=nav->depth&&(nav->routes[i]||nav->focus[i])))return false;
        risc_scene_put32(out+16+i*4,nav->routes[i]);
        risc_scene_put32(out+48+i*4,nav->focus[i]);
    }
    if(length)memcpy(out+RISC_SCENE_STATE_HEADER,payload,length);
    /* Last four header bytes hold checksum; hash a canonical zero checksum. */
    risc_scene_put32(out+92,risc_scene_state_hash(out,RISC_SCENE_STATE_HEADER+length));
    *written=RISC_SCENE_STATE_HEADER+length;return true;
}
static inline bool risc_scene_state_decode(const uint8_t *in,size_t length,
        uint32_t schema,risc_scene_navigation_v1 *nav,
        uint8_t *payload,size_t capacity,size_t *read) {
    if(!in||!nav||!read||!schema||length<RISC_SCENE_STATE_HEADER||
       length>RISC_SCENE_STATE_HEADER+4096||length-RISC_SCENE_STATE_HEADER>capacity||
       (length>RISC_SCENE_STATE_HEADER&&!payload)||memcmp(in,"RSC1",4)||
       risc_scene_get32(in+4)!=length||risc_scene_get32(in+8)!=schema)return false;
    uint32_t h=2166136261u;for(size_t i=0;i<length;i++)
        h=(h^((i>=92&&i<96)?0:in[i]))*16777619u;
    if(h!=risc_scene_get32(in+92))return false;
    for(unsigned i=80;i<92;i++)if(in[i])return false;
    risc_scene_navigation_v1 n={.api_version=1,.struct_size=sizeof(n)};
    n.depth=risc_scene_get32(in+12);if(!n.depth||n.depth>RISC_SCENE_MAX_DEPTH)return false;
    for(unsigned i=0;i<RISC_SCENE_MAX_DEPTH;i++) {
        n.routes[i]=risc_scene_get32(in+16+i*4);n.focus[i]=risc_scene_get32(in+48+i*4);
        if((i<n.depth&&!n.routes[i])||(i>=n.depth&&(n.routes[i]||n.focus[i])))return false;
    }
    *nav=n;*read=length-RISC_SCENE_STATE_HEADER;
    if(*read)memcpy(payload,in+RISC_SCENE_STATE_HEADER,*read);
    return true;
}
