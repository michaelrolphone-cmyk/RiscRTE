/* Actual persisted record encoders from the selected Watch utility source. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "spectrum_temporal_store.h"
#include "rf_temporal_store.h"
size_t export_record_fixture(unsigned which,void*bytes,size_t capacity){
 if(which<2){st_library source={0},decoded={0};source.generation[which]=7;
  source.labels[which*4].present=true;source.labels[which*4].next_id=1;
  strcpy(source.labels[which*4].name,"Saved room");
  size_t n=st_bank_encode(&source,which,bytes,capacity);assert(n&&st_bank_decode(&decoded,which,bytes,n));return n;}
 rt_library source={0},decoded={0};source.identity=rf_identity_default();source.generation[0]=9;
 source.labels[0].present=true;source.labels[0].next_id=1;strcpy(source.labels[0].name,"Saved radio");
 size_t n=rt_bank_encode(&source,0,bytes,capacity);assert(n&&rt_bank_decode(&decoded,0,bytes,n));return n;
}
