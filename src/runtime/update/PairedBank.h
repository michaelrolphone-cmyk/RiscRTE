#pragma once
#include <RiscBankStoreV1.h>
#include <cstddef>
#include <cstdint>
namespace RiscUpdate {
#ifdef RISC_PAIRED_APP_DATA
constexpr uint32_t FirmwareBytes=0x260000, StoreAbi=2;
constexpr uint32_t StoreBytes=0x510000;
constexpr uint32_t StoreOffset[2]={0x2f0000,0xae0000};
constexpr const char* Layout="riscrte-paired-appdata-v2";
#define RISC_PAIRED_ABI_MARKER "RISC_PAIRED_STORE_ABI:2"
#else
constexpr uint32_t FirmwareBytes=0x300000, StoreAbi=1;
constexpr uint32_t StoreBytes=0x4f0000;
constexpr uint32_t StoreOffset[2]={0x310000,0xb00000};
constexpr const char* Layout="riscrte-paired-16m-v1";
#define RISC_PAIRED_ABI_MARKER "RISC_PAIRED_STORE_ABI:1"
#endif
constexpr uint32_t SectorBytes=4096;
constexpr uint32_t FirmwareOffset[2]={0x10000,0x800000};
constexpr uint32_t JournalOffset=0xff2000;
/* Little-endian, fixed 96-byte commit record; one independent erase sector per
 * bank. CRC covers first 92 bytes. Written only after BOTH images read back.
 * It records readiness, never an independent active-store selector. */
struct Record {
  uint32_t magic, format, bank, firmwareSize, storeSize, storeAbi;
  uint8_t firmwareSha[32], storeSha[32];
  uint32_t reserved, crc;
};
static_assert(sizeof(Record)==96,"bank record format");
constexpr uint32_t RecordMagic=0x314b4252;
uint32_t crc32(const void*,size_t);
bool validRecord(const Record&,unsigned);
Record makeRecord(unsigned,uint32_t,const uint8_t*,const uint8_t*);
/* Trusted backend. All functions are owner-task-only, never retain input
 * pointers. read/write <=4096, erase exactly one sector; region 0=app, 1=store.
 * File admission/paths are resolved by the native backend before begin(). */
struct Backend {
  void* context;
  uint32_t (*now)(void*);
  bool (*read)(void*,unsigned,unsigned,uint32_t,void*,uint32_t);
  bool (*erase)(void*,unsigned,unsigned,uint32_t);
  bool (*write)(void*,unsigned,unsigned,uint32_t,const void*,uint32_t);
  bool (*invalidate)(void*,unsigned);
  bool (*record)(void*,unsigned,const Record&);
  bool (*hashBegin)(void*);
  bool (*hashAdd)(void*,const void*,uint32_t);
  bool (*hashEnd)(void*,uint8_t*);
  bool (*openApp)(void*,unsigned);
  bool (*writeApp)(void*,const void*,uint32_t);
  bool (*finishApp)(void*,unsigned);
  bool (*cleanup)(void*);
  bool (*validateFirmware)(void*,unsigned,uint32_t);
  bool (*select)(void*,unsigned);
};
class Transaction {
 public:
  explicit Transaction(Backend b):io_(b){}
  bool initialize(unsigned active,const Record& record);
  int32_t begin(bool app,const risc_bank_image_v1&,uint64_t* token);
  int32_t step(uint64_t,risc_bank_status_v1*);
  int32_t write(uint64_t,const void*,uint32_t);
  int32_t finish(uint64_t);
  int32_t activate(uint64_t);
  int32_t abort(uint64_t);
  bool status(risc_bank_status_v1*) const;
  bool exitSafe() const {return state_==RISC_BANK_IDLE || state_==RISC_BANK_ACTIVATED;}
  bool activated(uint64_t t) const {return t && t==token_ && (state_==RISC_BANK_ACTIVATED || state_==RISC_BANK_ACTIVATION_UNKNOWN);}
 private:
  int32_t fail(int32_t e){error_=e;state_=RISC_BANK_FAILED;return e;}
  bool live(uint64_t t) const {return initialized_ && t && t==token_;}
  bool timedOut() const;
  bool hashStart();
  bool writeRaw(unsigned,uint32_t,const void*,uint32_t);
  Backend io_;
  Record activeRecord_{}, targetRecord_{};
  risc_bank_image_v1 image_{};
  uint64_t serial_=0,token_=0;
  unsigned active_=0,target_=1;
  uint32_t state_=RISC_BANK_IDLE,offset_=0,received_=0,started_=0;
  int32_t error_=0;
  bool initialized_=false,app_=false;
  alignas(4) uint8_t buffer_[RISC_BANK_CHUNK_MAX]{};
};
}
