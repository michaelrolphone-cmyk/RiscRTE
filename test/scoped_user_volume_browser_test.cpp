/* Reuse the adapter's deterministic provider/fault model and assertions. */
#define main scoped_model_main
#include "scoped_user_volume_test.cpp"
#undef main
extern "C" {
bool scoped_browser_copy(const risc_storage_volume_api_v1*,const char*,const char*);
bool scoped_browser_copy_safe();
bool scoped_browser_retry_close();
const char *scoped_browser_copy_status();
unsigned scoped_browser_copy_yields();
bool scoped_browser_storage_layout(size_t,size_t,size_t,size_t,size_t);
}
int main(int argc,char**argv){
 assert(argc==2);const std::string mode=argv[1];ExtendedFixture f;
 assert(scoped_browser_storage_layout(sizeof(risc_storage_volume_api_v1),sizeof(risc_storage_volume_api_v1_ext),
  offsetof(risc_storage_volume_api_v1_ext,dir_close_checked),offsetof(risc_storage_volume_api_v1_ext,handle_error),offsetof(risc_storage_volume_api_v1_ext,rename)));
 auto&b=f.backend;auto&a=f.api;
 const std::string content=mode=="empty"?std::string():std::string(5000,'c');
 b.nodes.at("/user/hello.txt").data=content;
 if(mode=="collision")b.nodes.emplace("/user/docs/copy.txt",Backend::Node{false,"winner"});
 if(mode=="source_unavailable")b.fault=Backend::Fault::OpenReadUnavailable;
 if(mode=="writer_unavailable")b.fault=Backend::Fault::OpenWriteUnavailable;
 if(mode=="read_error")b.fault=Backend::Fault::ReadError;
 if(mode=="short_read")b.fault=Backend::Fault::ShortRead;
 if(mode=="write_error")b.fault=Backend::Fault::WriteError;
 if(mode=="short_write")b.fault=Backend::Fault::ShortWrite;
 if(mode=="sync_error")b.fault=Backend::Fault::Sync;
 if(mode=="source_close_before")b.fault=Backend::Fault::CloseBefore;
 if(mode=="source_close_after")b.fault=Backend::Fault::CloseAfter;
 if(mode=="rollback_before"){b.fault=Backend::Fault::WriteError;b.thenFault=Backend::Fault::AbortBefore;}
 if(mode=="rollback_after"){b.fault=Backend::Fault::WriteError;b.thenFault=Backend::Fault::AbortAfter;}
 if(mode=="publish_before")b.fault=Backend::Fault::RenameBefore;
 if(mode=="publish_after")b.fault=Backend::Fault::RenameAfter;
 if(mode=="growth")b.reenter=[&]{if(b.calls.back().operation=="stat" && b.count("read"))b.nodes.at("/user/hello.txt").data.push_back('g');};
 const bool succeeded=scoped_browser_copy(&a,"/hello.txt","/docs/copy.txt");b.reenter={};
 if(mode=="normal" || mode=="empty"){
  assert(succeeded && scoped_browser_copy_safe() && !f.volume.retained());
  assert(b.nodes.at("/user/docs/copy.txt").data==content);
  assert(b.nodes.at("/user/hello.txt").data==content && b.stages().empty());
  assert(b.files.empty() && b.directories.empty());
  assert(b.count("open_read")==1 && b.count("open_write")==1 && b.count("rename")==1);
  if(mode=="normal")assert(scoped_browser_copy_yields()>=10);
  // A repeated user action must preserve the existing result without writing.
  const auto writes=b.count("write");assert(!scoped_browser_copy(&a,"/hello.txt","/docs/copy.txt"));
  assert(b.count("write")==writes && scoped_browser_copy_safe());f.endClean();
 }else if(mode=="collision"){
  assert(!succeeded && scoped_browser_copy_safe() && b.count("open_write")==0);
  assert(b.nodes.at("/user/docs/copy.txt").data=="winner");f.endClean();
 }else if(mode=="source_unavailable" || mode=="writer_unavailable"){
  assert(!succeeded && scoped_browser_copy_safe() && !f.volume.retained());
  assert(b.files.empty() && b.stages().empty() && !b.nodes.count("/user/docs/copy.txt"));
  assert(b.count("file_close")==unsigned(mode=="writer_unavailable"));f.endClean();
 }else if(mode=="read_error" || mode=="short_read" || mode=="write_error" || mode=="short_write" || mode=="growth"){
  assert(!succeeded && scoped_browser_copy_safe() && !f.volume.retained());
  assert(!b.nodes.count("/user/docs/copy.txt") && b.stages().empty());
  assert(b.files.empty() && b.count("rename")==0 && b.count("file_close")==2);
  assert(strstr(scoped_browser_copy_status(),"aborted"));f.endClean();
 }else{
  assert(!succeeded && !scoped_browser_copy_safe());
  const size_t calls=b.calls.size();assert(!scoped_browser_retry_close());
  if(mode=="sync_error")assert(b.files.empty() && b.stages().empty());
  assert(f.volume.retained());assertExtendedRetained(f);
  assert(b.calls.size()==calls);
 }
 std::printf("Actual shared browser same-volume copy %s PASS\n",mode.c_str());
}
