#!/usr/bin/env python3
"""Reproduce the bounded native-v5 authority prerequisite on Darwin.
Actual Graph/Module/worker code; modeled ELF, RTOS, physical retirement and exports.
No Runtime or DeviceProviderExecutorV2 startup/close caller is implemented.
"""
import argparse,hashlib,json,os,pathlib,platform,re,shlex,shutil,subprocess,sys,traceback
if not __debug__ or sys.flags.optimize:raise RuntimeError("Run unoptimized Python")
P=pathlib.Path
def H(p):return hashlib.sha256(P(p).read_bytes()).hexdigest()
def require(ok,detail):
 if not ok:raise RuntimeError(str(detail))
def read(p):return json.loads(P(p).read_text())
def inside(p,root):return p==root or root in p.parents
def diagnostic(argv,extra):
 out=[];i=0
 while i<len(argv):
  t=argv[i];i+=1
  if t in ("-o","-MF","-MT","-MQ"):i+=1;continue
  if t in ("-c","-MMD","-MD","-MP","-M","-fsyntax-only"):continue
  require(not t.startswith(("-MF","-MT","-MQ","-Wp,-M","@")),"unsupported diagnostic argument")
  out.append(t)
 return out+extra
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for name in ("repo","cc","cxx","sdk","openssl-root","output"):ap.add_argument("--"+name,type=P,required=True)
 a=ap.parse_args();require(platform.system()=="Darwin","The retained strict alias recipe is Darwin-only")
 repo=a.repo.resolve();fixture=P(__file__).resolve().parent/"native_worker_v5";out=a.output.resolve()
 require(not out.exists() and not inside(out,repo),"Use fresh output outside source repository")
 out.mkdir(parents=True)
 env=os.environ.copy()
 for name in ("PYTHONOPTIMIZE","CPATH","C_INCLUDE_PATH","CPLUS_INCLUDE_PATH","OBJC_INCLUDE_PATH","LIBRARY_PATH","SDKROOT"):env.pop(name,None)
 env["PYTHONDONTWRITEBYTECODE"]="1"
 evidence={"status":"preparing","commands":[],"cases":[],"observations":[],"objects":{},"binaries":{},"dependencies":{},"errors":[],"scope":{"Darwin_only":True,"actual_Graph_Module_worker":True,"modeled_ELF_kernel_physical_retirement_exports":True,"Runtime_or_DeviceProviderExecutor_startup_caller":False,"target_execution":False}}
 def save():(out/"RESULT.json").write_text(json.dumps(evidence,sort_keys=True,indent=2)+"\n")
 def run(label,argv,cwd=out,timeout=180):
  row={"label":label,"argv":list(map(str,argv)),"cwd":str(cwd)};evidence["commands"].append(row);save()
  p=out/(label+".stdout")
  with p.open("xb") as stream:
   try:row["returncode"]=subprocess.run(row["argv"],cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout).returncode
   except subprocess.TimeoutExpired:row["returncode"]=124
   except OSError as e:stream.write(repr(e).encode());row["returncode"]=125
  row.update(path=str(p),sha256=H(p),bytes=p.stat().st_size);save()
  require(row["returncode"]==0,(label,p.read_text(errors="replace")[-8192:]))
  return p.read_text()
 try:
  recipe=read(fixture/"RECIPE.json");pins=read(fixture/"PACKAGE_PINS.json");source_pins=read(fixture/"SOURCE_PINS.json")
  require(recipe["schema"]==1 and recipe["platform"]=="Darwin","recipe contract")
  snapshots={}
  def pin(p,h=None):
   p=P(p).resolve();require(p.is_file(),("missing file",str(p)));now=H(p)
   require(h is None or now==h,("input changed",str(p)));old=snapshots.setdefault(str(p),now);require(old==now,("input changed",str(p)));return now
  for rel,h in pins.items():pin(fixture/rel,h)
  support_members={str(p.relative_to(fixture)) for p in (fixture/"support").rglob("*") if p.is_file()}
  require(support_members=={p for p in pins if p.startswith("support/")},"support inventory must exactly match pinned members")
  require(not any(p.is_symlink() for p in (fixture/"support").rglob("*")),"support symlinks forbidden")
  pin(fixture/"PACKAGE_PINS.json");pin(P(__file__))
  for rel,h in source_pins.items():pin(repo/rel,h)
  sdk=a.sdk.resolve();openssl=a.openssl_root.resolve();require(sdk.is_dir() and (openssl/"include").is_dir(),"explicit SDK/OpenSSL roots")
  cc=str(a.cc.absolute());cxx=str(a.cxx.absolute());tools={}
  for label,path in (("cc",cc),("cxx",cxx)):
   real=P(path).resolve();tools[path]={"realpath":str(real),"sha256":pin(real),"version":run(label+"-version",[path,"--version"])}
  resource=P(run("cc-resource-dir",[cc,"-print-resource-dir"]).strip()).resolve()
  library=openssl/"lib/libcrypto.dylib";pin(library)
  external_roots=[sdk,resource,P(cc).resolve().parent.parent/"include",P(cxx).resolve().parent.parent/"include",(openssl/"include").resolve()]
  evidence["inputs"]={"repo":str(repo),"sdk":str(sdk),"openssl_root":str(openssl),"crypto_library":{"path":str(library),"realpath":str(library.resolve()),"sha256":H(library)},"tools":tools}
  # Exact exports are modeled placeholders. Alias targets use actual linked test code.
  aliases=[x.split(",")[-1] for x in recipe["modes"]["normal"]["links"][0]["argv"] if x.startswith("-Wl,-alias,")]
  require(len(aliases)==16 and len(set(aliases))==16,"exact16 alias contract")
  symbols=[]
  for v in (1,2,5):symbols+=re.findall(r"RISC_OS_CPU_SYMBOL\((\w+)\)",(repo/("lib/elf_loader/include/private/privileged_os_cpu_symbols_v%d.def"%v)).read_text())
  require(set(aliases)<=set(symbols),"alias inventory")
  expected="\n".join('const unsigned char fixture_%d[] __asm__("%s")={1};'%(i,n) for i,n in enumerate(sorted(set(symbols))) if n not in aliases)+"\n"
  require((fixture/"support/exports.c").read_text()==expected,"modeled exports mismatch")
  plan={};generated={}
  for mode in ("normal","ubsan"):
   work=out/mode;shutil.copytree(fixture/"support",work)
   template=work/"shims/module-native.h";text=template.read_text()
   require(text.count("@REPO@")==1,"module include template")
   template.write_text(text.replace("@REPO@",str(repo)))
   for p in work.rglob("*"):
    if p.is_file():generated[str(p.resolve())]=pin(p)
   substitutions={"{REPO}":str(repo),"{WORK}":str(work),"{FIXTURE}":str(fixture),"{CC}":cc,"{CXX}":cxx,"{OPENSSL}":str(openssl)}
   def mapped(argv):
    result=[]
    for token in argv:
     for old,new in substitutions.items():token=token.replace(old,new)
     require("{" not in token and "}" not in token and not token.startswith("@"),"unexpanded recipe token")
     result.append(token)
    # Make the formerly implicit host SDK an explicit reproducible input.
    result[1:1]=["-isysroot",str(sdk)]
    return result
   group={"cwd":str(work),"objects":[],"links":[]}
   for unit in recipe["modes"][mode]["objects"]:
    argv=mapped(unit["argv"]);require(argv.count("-c")==1 and argv.count("-o")==1,"one source/output")
    src=P(argv[argv.index("-c")+1]).resolve();require(str(src) in snapshots,"explicit source pin")
    group["objects"].append({"label":mode+"-"+unit["label"],"argv":argv,"source":str(src),"negative_module_profile":unit["label"]=="module-nonnative-candidate"})
   for link in recipe["modes"][mode]["links"]:
    argv=mapped(link["argv"])
    require(not any(any(bad in t for bad in ("dynamic_lookup","flat_namespace","multiply_defined","dead_strip","-undefined")) for t in argv),"strict link required")
    group["links"].append({"variant":link["variant"],"argv":argv})
   require(len(group["objects"])==29 and len(group["links"])==4,"recipe counts")
   plan[mode]=group
  evidence["generated"]=generated;evidence["plan"]=plan;save()
  # Dependency and effective-profile checks before any object compilation.
  for mode,g in plan.items():
   for unit in g["objects"]:
    name=unit["label"];argv=unit["argv"]
    text=run(name+"-dependencies",diagnostic(argv,["-M"]),g["cwd"]).replace("\\\n"," ")
    require(":" in text,"dependency syntax");consumed=set()
    for n in shlex.split(text.split(":",1)[1]):
     p=P(n);p=(P(g["cwd"])/p).resolve() if not p.is_absolute() else p.resolve();key=str(p)
     if key not in snapshots:
      require(any(inside(p,x) for x in external_roots),("unqualified dependency",key))
      pin(p)
     require(H(p)==snapshots[key],("dependency drift",key));consumed.add(key);evidence["dependencies"][key]=snapshots[key]
    require(unit["source"] in consumed,"source absent in dependency output")
    if P(unit["source"]).name=="pthread_local_storage.c":
     require({n for n in consumed if P(n).name=="pthread_internal.h"}=={str(P(g["cwd"])/"kernel-shims/pthread_internal.h")},"pthread quote-include route")
     require(not any("/esp-idf/components/pthread/" in n for n in consumed),"SDK sibling header not modeled shim")
    if P(unit["source"]).name=="ProviderModuleV2.cpp":
     require(str(P(g["cwd"])/"shims/module-native.h") in consumed and str(repo/"lib/hal/RuntimeFaultRetention.h") in consumed,"Module forced include")
    require(not any(re.search(r"privileged_os_cpu_symbols_v[34]\.def$",n) for n in consumed),"intermediate ABI inventories forbidden")
    run(name+"-syntax",diagnostic(argv,["-fsyntax-only"]),g["cwd"])
    macros=run(name+"-assertion-profile",diagnostic(argv,["-E","-dM"]),g["cwd"])
    names={m.group(1) for line in macros.splitlines() if (m:=re.match(r"^#define\s+(\w+)(?:\s|$)",line))}
    require(not names.intersection({"NDEBUG","RISC_INSTALLED_APP_CAPACITY","RISC_PROVIDER_CAPACITY"}),"assertions/default capacity changed")
    if unit["negative_module_profile"]:
     require("#define RISC_NATIVE_ESP_IDF 0\n" in macros and "#define RISC_CPU_SCOPE_V5_CANDIDATE 1\n" in macros,"negative native profile")
     body=run(name+"-attachment-profile",diagnostic(argv,["-E","-P"]),g["cwd"])
     require(not re.search(r"\b(?:esp_cpu_task_binding_\w+|nativeTaskBinding_|nativeTaskWindow_)\b",body),"nonnative attachment references")
  for name,h in snapshots.items():require(H(name)==h,("input drift before compilation",name))
  for mode,g in plan.items():
   for unit in g["objects"]:
    run(unit["label"]+"-object",unit["argv"],g["cwd"]);p=unit["argv"][unit["argv"].index("-o")+1];evidence["objects"][p]=H(p)
   for link in g["links"]:
    argv=link["argv"];variant=link["variant"];run(mode+"-"+variant+"-link",argv,g["cwd"]);binary=argv[argv.index("-o")+1];evidence["binaries"][mode+":"+variant]=H(binary)
    for row in recipe["cases"]:
     if row["variant"]!=variant:continue
     stdout=run(mode+"-"+variant+"-"+row["case"],[binary,row["case"]],g["cwd"],45)
     require(stdout==row["stdout"],"exact PASS identity")
     case=mode+":"+variant+":"+row["case"];evidence["cases"].append(case);evidence["observations"].append({"case":case,"stdout":stdout});save()
  require(len(evidence["objects"])==58 and len(evidence["binaries"])==8 and len(evidence["cases"])==20,"exact20 closure")
  for name,h in {**snapshots,**evidence["objects"]}.items():require(H(name)==h,("changed input/object",name))
  for mode,g in plan.items():
   for link in g["links"]:
    p=link["argv"][link["argv"].index("-o")+1];require(H(p)==evidence["binaries"][mode+":"+link["variant"]],"binary changed")
  for path,row in tools.items():require(str(P(path).resolve())==row["realpath"] and H(row["realpath"])==row["sha256"],"compiler changed")
  evidence["input_snapshots"]=snapshots;evidence["status"]="passed";save()
 except Exception as e:
  evidence.update(status="failed",error=repr(e),traceback=traceback.format_exc());save()
 finally:
  failed=[x for x in evidence["commands"] if x.get("returncode")]
  for row in failed:row["tail"]=P(row["path"]).read_text(errors="replace")[-8192:]
  save();print(json.dumps({"status":evidence["status"],"error":evidence.get("error"),"cases":evidence["cases"],"objects":len(evidence["objects"]),"binaries":len(evidence["binaries"]),"result_path":str(out/"RESULT.json"),"result_sha256":H(out/"RESULT.json"),"failed_commands":failed}),flush=True)
 return 0 if evidence["status"]=="passed" else 1
if __name__=="__main__":raise SystemExit(main())
