#pragma once
#include <RiscStorageWorkerV1.h>
#include <RiscStorageVolumeAsyncV1.h>
#include <string.h>
/* Shared provider-side executor. Only the selected native worker calls volume
 * I/O. Owner methods inspect worker-published fields only after collect/cancel
 * proves completion. The caller must reject synchronous volume admission while
 * session!=0; this also serializes filesystem/export/sleep state. */
typedef struct {
 const risc_storage_worker_v1*worker;const risc_storage_volume_api_v1*volume;
 const risc_storage_volume_api_v1_ext*ext;const risc_storage_volume_api_v1_fs*fs;
 uint64_t serial,session,operation;bool ending,cleanup,retained;
 risc_storage_async_result_v1 cleanup_result; // provider-owned owner output, never app memory
 uint32_t directory,file; // worker-only until completion fence
} risc_storage_async_adapter_v1;
static inline int32_t risc_storage_async_execute(void*context,const void*input,uint32_t n,void*output,uint32_t cap,uint32_t*actual){
 risc_storage_async_adapter_v1*a=(risc_storage_async_adapter_v1*)context;
 if(n!=sizeof(risc_storage_async_request_v1)||cap<sizeof(risc_storage_async_result_v1))return RISC_STORAGE_WORK_INVALID;
 const risc_storage_async_request_v1*q=(const risc_storage_async_request_v1*)input;
 risc_storage_async_result_v1*r=(risc_storage_async_result_v1*)output;memset(r,0,sizeof(*r));*actual=sizeof(*r);r->status=RISC_STORAGE_ASYNC_IO;
 const risc_storage_volume_api_v1*v=a->volume;
 if(a->retained){r->status=RISC_STORAGE_ASYNC_RETAINED;return RISC_STORAGE_WORK_OK;}
 if(q->operation==0||q->operation==RISC_STORAGE_ASYNC_CLOSE_DIRECTORY){
  if(a->file){if(!v->file_close(v->context,a->file,false))a->retained=true;else a->file=0;}
  if(!a->retained&&a->directory){if(!a->ext->dir_close_checked(v->context,a->directory))a->retained=true;else a->directory=0;}
  r->status=a->retained?RISC_STORAGE_ASYNC_RETAINED:RISC_STORAGE_ASYNC_OK;
 }else if(q->operation==RISC_STORAGE_ASYNC_REMOVE_FILE){
  if(a->directory||a->file)r->status=RISC_STORAGE_ASYNC_BUSY;
  else if(!v->remove||!memchr(q->path,0,sizeof(q->path))||!q->path[0])r->status=RISC_STORAGE_ASYNC_INVALID;
  else {
   risc_storage_metadata_v1 m={sizeof(m),0,0,0};
   int32_t found=a->fs->metadata(v->context,q->path,&m);
   if(found==RISC_STORAGE_FS_NOT_FOUND)r->status=RISC_STORAGE_ASYNC_OK;
   else if(found==RISC_STORAGE_FS_OK&&!(m.flags&RISC_STORAGE_DIRECTORY)&&v->remove(v->context,q->path))r->status=RISC_STORAGE_ASYNC_OK;
  }
 }else if(q->operation==RISC_STORAGE_ASYNC_RENAME){
  if(a->directory||a->file)r->status=RISC_STORAGE_ASYNC_BUSY;
  else if(!a->ext->rename||!memchr(q->path,0,sizeof(q->path))||!q->path[0]||
          !memchr(q->destination,0,sizeof(q->destination))||!q->destination[0])r->status=RISC_STORAGE_ASYNC_INVALID;
  else if(a->ext->rename(v->context,q->path,q->destination))r->status=RISC_STORAGE_ASYNC_OK;
 }else if(q->operation==RISC_STORAGE_ASYNC_REFRESH){
  if(a->directory||a->file)r->status=RISC_STORAGE_ASYNC_BUSY;
  else if(v->ready(v->context)||(v->refresh(v->context)&&v->ready(v->context)))r->status=RISC_STORAGE_ASYNC_OK;
 }else if(q->operation==RISC_STORAGE_ASYNC_OPEN_DIRECTORY){
  if(a->directory)r->status=RISC_STORAGE_ASYNC_BUSY;
  else if((a->directory=v->dir_open(v->context,q->path))!=0)r->status=RISC_STORAGE_ASYNC_OK;
  else {risc_storage_metadata_v1 m={sizeof(m),0,0,0};if(a->fs->metadata(v->context,q->path,&m)==RISC_STORAGE_FS_NOT_FOUND)r->status=RISC_STORAGE_ASYNC_NOT_FOUND;}
 }else if(q->operation==RISC_STORAGE_ASYNC_NEXT_ENTRY){
  if(!a->directory)r->status=RISC_STORAGE_ASYNC_INVALID;
  else if(v->dir_next(v->context,a->directory,&r->entry))r->status=RISC_STORAGE_ASYNC_OK;
  else if(!a->ext->handle_error(v->context,a->directory,true))r->status=RISC_STORAGE_ASYNC_END;
 }else if(q->operation==RISC_STORAGE_ASYNC_READ_SMALL_FILE){
  uint64_t size=0;a->file=v->file_open_read(v->context,q->path,&size);
  if(!a->file){risc_storage_metadata_v1 m={sizeof(m),0,0,0};if(a->fs->metadata(v->context,q->path,&m)==RISC_STORAGE_FS_NOT_FOUND)r->status=RISC_STORAGE_ASYNC_NOT_FOUND;}
  else {
   if(size>sizeof(r->data)||!size)r->status=RISC_STORAGE_ASYNC_INVALID;
   else {while(r->size<size){size_t wanted=(size_t)size-r->size;if(wanted>512)wanted=512;size_t got=v->file_read(v->context,a->file,r->data+r->size,wanted);if(!got||got>wanted)break;r->size+=(uint32_t)got;}
    if(r->size==size&&!a->ext->handle_error(v->context,a->file,false))r->status=RISC_STORAGE_ASYNC_OK;
   }
   if(!v->file_close(v->context,a->file,false)){a->retained=true;r->status=RISC_STORAGE_ASYNC_RETAINED;}else a->file=0;
  }
 }else r->status=RISC_STORAGE_ASYNC_INVALID;
 if(r->status!=RISC_STORAGE_ASYNC_OK)r->size=0;
 return RISC_STORAGE_WORK_OK;
}
static inline int32_t risc_storage_async_begin(risc_storage_async_adapter_v1*a,uint64_t*out){
 if(out)*out=0;
 if(!out||!a->worker||!a->volume)return RISC_STORAGE_ASYNC_INVALID;
 if(a->session)return RISC_STORAGE_ASYNC_BUSY;
 if(a->retained)return RISC_STORAGE_ASYNC_RETAINED;
 if(a->serial==UINT64_MAX)return RISC_STORAGE_ASYNC_INVALID;
 a->session=++a->serial;a->ending=a->cleanup=false;*out=a->session;return RISC_STORAGE_ASYNC_OK;
}
static inline int32_t risc_storage_async_submit(risc_storage_async_adapter_v1*a,uint64_t session,const risc_storage_async_request_v1*q,uint64_t*out){
 if(out)*out=0;
 if(!out||!session||session!=a->session||!q||!q->operation||q->operation>RISC_STORAGE_ASYNC_REMOVE_FILE||!memchr(q->path,0,sizeof(q->path)))return RISC_STORAGE_ASYNC_INVALID;
 if(a->ending||a->operation)return RISC_STORAGE_ASYNC_BUSY;
 if(a->retained)return RISC_STORAGE_ASYNC_RETAINED;
 int32_t rc=a->worker->submit(a->worker->context,q,sizeof(*q),&a->operation);
 if(rc!=RISC_STORAGE_WORK_OK)return RISC_STORAGE_ASYNC_BUSY;
 *out=a->operation;return RISC_STORAGE_ASYNC_OK;
}
static inline int32_t risc_storage_async_collect(risc_storage_async_adapter_v1*a,uint64_t session,uint64_t operation,risc_storage_async_result_v1*out){
 if(!out||!session||session!=a->session||!operation||operation!=a->operation||a->ending)return RISC_STORAGE_ASYNC_INVALID;
 uint32_t size=0;int32_t status=0;int32_t rc=a->worker->collect(a->worker->context,operation,out,sizeof(*out),&size,&status);
 if(rc==RISC_STORAGE_WORK_PENDING)return RISC_STORAGE_ASYNC_PENDING;
 if(rc!=RISC_STORAGE_WORK_OK||status!=RISC_STORAGE_WORK_OK||size!=sizeof(*out)){a->retained=true;return RISC_STORAGE_ASYNC_RETAINED;}
 a->operation=0;if(out->status==RISC_STORAGE_ASYNC_RETAINED)a->retained=true;
 return out->status;
}
static inline int32_t risc_storage_async_end(risc_storage_async_adapter_v1*a,uint64_t session){
 if(!session||session!=a->session)return RISC_STORAGE_ASYNC_INVALID;
 a->ending=true;
 if(a->operation){
  if(!a->cleanup){int32_t rc=a->worker->cancel(a->worker->context,a->operation);
   if(rc==RISC_STORAGE_WORK_PENDING)return RISC_STORAGE_ASYNC_PENDING;
   if(rc!=RISC_STORAGE_WORK_CANCELLED){a->retained=true;return RISC_STORAGE_ASYNC_RETAINED;}
  }else {
   uint32_t size=0;int32_t status=0;
   int32_t rc=a->worker->collect(a->worker->context,a->operation,&a->cleanup_result,sizeof(a->cleanup_result),&size,&status);
   if(rc==RISC_STORAGE_WORK_PENDING)return RISC_STORAGE_ASYNC_PENDING;
   if(rc!=RISC_STORAGE_WORK_OK||status!=RISC_STORAGE_WORK_OK||size!=sizeof(a->cleanup_result)){a->retained=true;return RISC_STORAGE_ASYNC_RETAINED;}
  }
  a->operation=0;
 }
 if(a->retained)return RISC_STORAGE_ASYNC_RETAINED;
 if(a->file||a->directory){
  risc_storage_async_request_v1 q={0,{0},{0}};a->cleanup=true;
  if(a->worker->submit(a->worker->context,&q,sizeof(q),&a->operation)!=RISC_STORAGE_WORK_OK)return RISC_STORAGE_ASYNC_BUSY;
  return RISC_STORAGE_ASYNC_PENDING;
 }
 a->session=0;a->ending=a->cleanup=false;return RISC_STORAGE_ASYNC_OK;
}
