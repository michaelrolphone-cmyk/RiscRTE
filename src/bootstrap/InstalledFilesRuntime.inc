bool Runtime::configureInstalledFiles(JsonObjectConst boot) {
 bool required=false;for(size_t i=0;i<policyCount_;i++)for(size_t j=0;j<policies_[i].count;j++)required=required||policies_[i].grants[j].installedFiles;
 if(!required)return true;
 const size_t maximum=2*policyCount_+2*driverCount_;
 auto names=metadataArray<InstalledFiles::Name>(maximum);
 if(!maximum || !names)return fail("installed inventory allocation failed");
 size_t count=0;const size_t prefix=strlen(root_)+1;
 auto add=[&](const char*relative)->bool{
  if(!InstalledFiles::valid(relative,false))return false;
  for(size_t i=0;i<count;i++)if(!strcmp(names[i].value,relative))return true;
  if(count>=maximum)return false;
  strcpy(names[count++].value,relative);return true;
 };
 // Only paths whose selected manifests were already admitted above. Never
 // enumerate the filesystem root or infer that a .json file is public.
 for(size_t i=0;i<policyCount_;i++) {
  const char*manifest=boot["app_capabilities"][i]["manifest"];
  if(!manifest || !add(manifest) || !add(policies_[i].elf+prefix))return fail("invalid installed app inventory");
 }
 for(size_t i=0;i<driverCount_;i++) {
  const char*manifest=boot["drivers"][i]["manifest"];
  if(!manifest || !add(manifest) || !add(drivers_[i].elf+prefix))return fail("invalid installed driver inventory");
 }
 installedFiles_.reset(new(std::nothrow) InstalledFiles());
 if(!installedFiles_ || !installedFiles_->configure(root_,names.get(),count))return fail("installed volume unavailable");
 return true;
}
Runtime* Runtime::volumeContext(void*context) {
 Runtime*r=currentRuntime;
 if(!r || !r->active() || r->retained_ || !context || r->installedVolumeContext_!=context || !r->installedFiles_ || !r->providerStorageSafe())return nullptr;
 bool live=false;for(const auto&g:r->appGrants_)if(g.live && g.api==&r->installedVolume_)live=true;
 return live?r:nullptr;
}
risc_storage_volume_api_v1 Runtime::volumeTable(void*context) {
 return {1,sizeof(risc_storage_volume_api_v1),context,
 [](void*c){auto*r=volumeContext(c);return r && r->installedFiles_->refresh();},
 [](void*c){auto*r=volumeContext(c);return r && r->installedFiles_->ready();},
 [](void*c,char*out,size_t n){auto*r=volumeContext(c);return r && r->installedFiles_->label(out,n);},
 [](void*c,const char*p,uint64_t*s,bool*d){auto*r=volumeContext(c);return r && r->installedFiles_->stat(p,s,d);},
 [](void*c,const char*p)->risc_storage_dir_t{auto*r=volumeContext(c);return r?r->installedFiles_->dirOpen(p):0;},
 [](void*c,risc_storage_dir_t h,risc_storage_dirent_v1*out){auto*r=volumeContext(c);return r && r->installedFiles_->dirNext(h,out);},
 [](void*c,risc_storage_dir_t h){auto*r=volumeContext(c);if(r)r->installedFiles_->dirClose(h);},
 [](void*c,const char*p,uint64_t*s)->risc_storage_file_t{auto*r=volumeContext(c);return r?r->installedFiles_->fileOpen(p,s):0;},
 [](void*c,risc_storage_file_t h,void*out,size_t n)->size_t{auto*r=volumeContext(c);return r?r->installedFiles_->fileRead(h,out,n):0;},
 nullptr,nullptr,
 [](void*c,risc_storage_file_t h,bool){auto*r=volumeContext(c);return r && r->installedFiles_->fileClose(h);},
 nullptr,
 [](void*c,char*out,size_t n){auto*r=volumeContext(c);return r && r->installedFiles_->error(out,n);}};
}
