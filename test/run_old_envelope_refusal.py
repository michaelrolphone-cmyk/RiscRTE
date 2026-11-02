#!/usr/bin/env python3
"""Execute untouched a27 production Runtime/bank admission with test-only probes."""
import argparse, hashlib, json, os, pathlib, subprocess
p=argparse.ArgumentParser()
p.add_argument('--baseline',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args(); root=a.baseline.resolve(); out=a.output.resolve(); out.mkdir(parents=True,exist_ok=True)
pin='a27c7541e0fcaa40290d7bb8dc6db07b2ea92f21'
def git(*args): return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()
assert git('rev-parse','HEAD')==pin
assert not git('status','--porcelain','--','src','sdk','lib')
fixture=root/'test/native_bank_provider_admission.inc'
original=fixture.read_bytes(); text=original.decode()
runner=root/'test/run_native_bank_test.sh'; original_runner=runner.read_bytes()
helper="static std::vector<uint8_t> bankEnvelope(const std::vector<uint8_t>& bytes){\n std::vector<uint8_t> out(32,0);memcpy(out.data(),\"RTEIMG1\\0\",8);\n const auto word=[&](size_t at,uint32_t value){for(unsigned i=0;i<4;++i)out[at+i]=uint8_t(value>>(8*i));};\n uint32_t crc=~uint32_t(0);for(uint8_t b:bytes){crc^=b;for(unsigned k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&uint32_t(0-int32_t(crc&1)));}\n word(8,32);word(12,1);word(16,uint32_t(bytes.size()));word(24,~crc);\n out.push_back(uint8_t(std::min<size_t>(15,bytes.size())<<4));\n if(bytes.size()>=15){size_t left=bytes.size()-15;while(left>=255){out.push_back(255);left-=255;}out.push_back(uint8_t(left));}\n out.insert(out.end(),bytes.begin(),bytes.end());word(20,uint32_t(out.size()-32));return out;\n}\n\nstatic void oldEnvelopeRefusal(const std::string& root,const ProviderAdmissionSources& sources){\n using namespace RiscBankStore;\n std::ofstream(root+\"/boot.json\")<<R\"({\"board\":\"board.json\",\"default_app\":\"old-refusal.elf\",\"drivers\":[]})\";\n auto candidate=std::make_unique<RiscBoot::Runtime>(RiscBoot::Port{own,nullptr,nullptr,nullptr});\n assert(sources.bind(*candidate) && candidate->prepare(root.c_str()));\n const auto raw=elf(\"memcpy\"),encoded=bankEnvelope(raw);const std::string path=root+\"/old-refusal.elf\";\n auto profile=std::make_unique<RiscProvision::Profile>();profile->count=1;strcpy(profile->files[0].path,\"old-refusal.elf\");\n ProvisionState state({},admissionHardware(),nullptr,nullptr);provisionState=&state;provisionProfile=profile.get();\n for(bool represented:{false,true}){\n   const auto& bytes=represented?encoded:raw;\n   {std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(out.good());}\n   auto& file=profile->files[0];file.bytes=bytes.size();SHA256(bytes.data(),bytes.size(),file.sha256);\n   AdmissionImages context{root.c_str(),ticks,candidate.get()};\n   assert(inspectProvisionedImage(context,path.c_str(),ElfRole::Application)==!represented);\n   assert(context.seen[0]==!represented && !provisionReadRetained && sources.sources()->exitSafe());\n   assert(admitFile(candidate.get(),path.c_str(),false)==!represented);\n }\n provisionState=nullptr;provisionProfile=nullptr;\n assert(operationSafe());\n std::cout<<\"Exact a27 old native admission: raw app accepted; identical logical ELF in envelope refused by provisioning and paired cohort callbacks PASS\\n\";\n}\n"
anchor='static void nativeAdmissionBudgets('
call=' nativeAdmissionBudgets(provisionRoot,sources);'
assert text.count(anchor)==1 and text.count(call)==1
text=text.replace(anchor,helper+'\n'+anchor).replace(call,' oldEnvelopeRefusal(provisionRoot,sources);\n'+call)
production=['src/ports/esp32s3/NativeBankStore.cpp','src/bootstrap/Runtime.cpp','src/bootstrap/RuntimeFileImage.h','src/runtime/update/PairedBank.cpp']
hashes={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in production}
rc=1
try:
 fixture.write_text(text)
 runner_text=original_runner.decode()
 assert runner_text.count('crypto_native=0;')==1
 runner.write_text(runner_text.replace('crypto_native=0;', 'if [[ "${APP_DATA_EXPANDED_TEST:-0}" == 1 ]];then extra+=(-DRISC_PAIRED_APP_DATA=1 -DRISC_PAIRED_APP_DATA_EXPANDED=1);fi\ncrypto_native=0;'))
 env=dict(os.environ,NATIVE_PROVIDER_ADMISSION_ONLY='1')
 with (out/'result.log').open('w') as log:
  for selected in ('0','1'):
   env['NATIVE_PROVIDER_TEST_SELECTION']=selected
   result=subprocess.run(['bash',str(root/'test/run_native_bank_test.sh')],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
   log.write(result.stdout);log.flush();print(result.stdout,end='')
   if result.returncode: rc=result.returncode;break
  else: rc=0
finally:
 fixture.write_bytes(original)
 runner.write_bytes(original_runner)
 assert not git('status','--porcelain','--','src','sdk','lib')
 assert all(hashlib.sha256((root/name).read_bytes()).hexdigest()==digest for name,digest in hashes.items())
 (out/'receipt.json').write_text(json.dumps({'baseline':pin,'production_sha256':hashes,'test_injection_sha256':hashlib.sha256(helper.encode()).hexdigest(),'returncode':rc,'production_unchanged':True},indent=2)+'\n')
raise SystemExit(rc)
