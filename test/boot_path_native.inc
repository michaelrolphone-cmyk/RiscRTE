// Exercise the actual prepareBoot function. The flash/SDK counters observe
// calls at its I/O boundary; SDK image verification internals are not modeled.
static void clearBootCounters(){
 nativeFlashReads=nativeFlashBytes=nativePartitionReads=nativePartitionBytes=0;
 nativeFirmwareScanReads=nativeFirmwareScanBytes=0;
 nativeHashStarts=nativeHashUpdates=nativeHashBytes=nativeHashFinishes=0;
 nativeImageVerifications=nativeImageDescriptions=delays=0;
}
static void resetBoot(unsigned bank,esp_ota_img_states_t state){
 using namespace RiscBankStore;
 assert(!runtime && !mounted && !appFile && !provisionFiles);
 if(transaction){transaction->~Transaction();free(transaction);transaction=nullptr;}
 if(scratch){scratch->~Scratch();free(scratch);scratch=nullptr;}
 prepared=pending=confirmed=rollbackTrusted=false;
 active=bank;otaState=state;clearBootCounters();
}
static void constantBootCost(){
 assert(nativePartitionReads==1 && nativePartitionBytes==sizeof(RiscUpdate::Record));
 assert(!nativeFlashReads && !nativeFlashBytes && !nativeHashStarts && !nativeHashUpdates && !nativeHashBytes && !nativeHashFinishes);
 assert(!nativeImageVerifications && !nativeImageDescriptions && !delays);
 assert(!nativeFirmwareScanReads && !nativeFirmwareScanBytes);
}
static void bootCost(const char* loader,const char* native){
 using namespace RiscUpdate;
 std::ifstream input(loader,std::ios::binary);std::vector<uint8_t> boot((std::istreambuf_iterator<char>(input)),{});assert(boot.size()==15104);
 memcpy(flash.data(),boot.data(),boot.size());firmware(0,RISC_BUILD_VERSION);
 if(native){
  std::ifstream image(native,std::ios::binary);assert(image);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(image)),{});
  assert(bytes.size()>=32 && bytes.size()<=FirmwareBytes);imageSize=bytes.size();
  memcpy(flash.data()+FirmwareOffset[0],bytes.data(),bytes.size());
 }
 uint8_t fw[32],store[32];SHA256(flash.data()+FirmwareOffset[0],imageSize,fw);SHA256(flash.data()+StoreOffset[0],StoreBytes,store);
 const auto record=makeRecord(0,imageSize,fw,store);memcpy(flash.data()+JournalOffset,&record,sizeof(record));
 clearBootCounters();assert(RiscBankStore::prepareBoot(own,restartSafe,safe));
#ifdef RISC_TEST_BOOT_SCANS
 const size_t firmwareChunks=(imageSize+4095)/4096,storeChunks=StoreBytes/4096;
 assert(nativeFlashReads==4 && nativeFlashBytes==15104);
 assert(nativePartitionReads==1+2*firmwareChunks+storeChunks && nativePartitionBytes==sizeof(record)+2*imageSize+StoreBytes);
 assert(nativeHashStarts==3 && nativeHashFinishes==3 && nativeHashUpdates==4+firmwareChunks+storeChunks);
 assert(nativeHashBytes==15104+imageSize+StoreBytes && delays==4+2*firmwareChunks+storeChunks);
 assert(nativeImageVerifications==1 && nativeImageDescriptions==1);
 assert(nativeFirmwareScanReads==firmwareChunks && nativeFirmwareScanBytes==imageSize);
#else
 constantBootCost();
#endif
 assert(!writes && !confirms);
 std::cout<<"prepareBoot ABI="<<StoreAbi<<" firmware="<<imageSize<<" store="<<StoreBytes
  <<" explicit_reads="<<nativePartitionReads+nativeFlashReads<<" explicit_read_bytes="<<nativePartitionBytes+nativeFlashBytes
  <<" sha_starts="<<nativeHashStarts<<" sha_updates="<<nativeHashUpdates<<" sha_bytes="<<nativeHashBytes
  <<" firmware_marker_reads="<<nativeFirmwareScanReads<<" firmware_marker_bytes="<<nativeFirmwareScanBytes
  <<" sdk_image_verifications="<<nativeImageVerifications<<" sdk_image_descriptions="<<nativeImageDescriptions<<" yields="<<delays<<" PASS\n";
}
static void bootRecords(){
 using namespace RiscUpdate;
 // Existing records alone select the immutable matching store. No installed
 // content is read, including deliberately different loader/firmware/store
 // bytes. Real mount/ELF mapping bounds remain the loader's responsibility.
 uint8_t digest[32]{};
 for(unsigned bank=0;bank<2;++bank){
  const auto record=makeRecord(bank,imageSize,digest,digest);
  memcpy(flash.data()+JournalOffset+bank*4096,&record,sizeof(record));
 }
 for(unsigned bank=0;bank<2;++bank)for(auto state:{ESP_OTA_IMG_VALID,ESP_OTA_IMG_PENDING_VERIFY}){
  resetBoot(bank,state);assert(RiscBankStore::prepareBoot(own,restartSafe,safe));constantBootCost();
  assert(!strcmp(RiscBankStore::bootLabel(),bank?"bootfs1":"bootfs0"));
  assert(!RiscBankStore::confirmBoot()); // application health still required
  const auto before=rollbacks;RiscBankStore::rejectBoot();
  assert(rollbacks==before+(state==ESP_OTA_IMG_PENDING_VERIFY));
  // A deep timer wake takes the same fresh prepareBoot path and cost.
  resetBoot(bank,state);assert(RiscBankStore::prepareBoot(own,restartSafe,safe));constantBootCost();
 }
 for(unsigned bank=0;bank<2;++bank){
  const auto record=makeRecord(bank,imageSize,digest,digest);
  for(size_t cut:{size_t(0),size_t(12),size_t(48),sizeof(record)-1}){
   memset(flash.data()+JournalOffset+bank*4096,0xff,4096);
   memcpy(flash.data()+JournalOffset+bank*4096,&record,cut);
   resetBoot(bank,ESP_OTA_IMG_PENDING_VERIFY);assert(!RiscBankStore::prepareBoot(own,restartSafe,safe));constantBootCost();
   assert(!RiscBankStore::bootLabel());const auto before=rollbacks;RiscBankStore::rejectBoot();assert(rollbacks==before+1);
   // IDF rollback reselects the untouched prior pair; no inactive record scan.
   resetBoot(1-bank,ESP_OTA_IMG_VALID);assert(RiscBankStore::prepareBoot(own,restartSafe,safe));constantBootCost();
  }
  memcpy(flash.data()+JournalOffset+bank*4096,&record,sizeof(record));
 }
 // Complete but mismatched bank records cannot redirect the selected store.
 const auto wrong=makeRecord(1,imageSize,digest,digest);
 memcpy(flash.data()+JournalOffset,&wrong,sizeof(wrong));
 resetBoot(0,ESP_OTA_IMG_PENDING_VERIFY);assert(!RiscBankStore::prepareBoot(own,restartSafe,safe));constantBootCost();
 const auto restored=makeRecord(0,imageSize,digest,digest);memcpy(flash.data()+JournalOffset,&restored,sizeof(restored));
 for(auto state:{ESP_OTA_IMG_NEW,ESP_OTA_IMG_INVALID,ESP_OTA_IMG_ABORTED}){
  resetBoot(0,state);assert(!RiscBankStore::prepareBoot(own,restartSafe,safe));
  assert(!nativePartitionReads && !nativeFlashReads && !nativeHashStarts && !nativeImageVerifications && !delays);
 }
 resetBoot(0,ESP_OTA_IMG_PENDING_VERIFY);rollbackPossible=false;
 assert(RiscBankStore::prepareBoot(own,restartSafe,safe));constantBootCost();
 const auto before=rollbacks;RiscBankStore::rejectBoot();assert(rollbacks==before);rollbackPossible=true;
 assert(!writes && !confirms && !selectorCalls);
 std::cout<<"Committed-pair boot: both banks, pending/confirmed/deep-wake path, torn commit, prior-pair rollback, invalid OTA states; 96-byte record only PASS\n";
}
