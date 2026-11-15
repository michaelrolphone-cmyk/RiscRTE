#!/usr/bin/env python3
"""Darwin actual Runtime/native-owner caller tests with explicit modeled boundaries.
No target activation, physical-retirement proof, network I/O or operational deployment.
"""
import argparse,hashlib,json,os,pathlib,platform,re,shlex,shutil,subprocess,sys,traceback
P=pathlib.Path
def H(p):return hashlib.sha256(P(p).read_bytes()).hexdigest()
def require(ok,detail):
 if not ok:raise RuntimeError(str(detail))
def inside(p,root):return p==root or root in p.parents
def diagnostic(argv,extra):
 out=[];i=0
 while i<len(argv):
  t=argv[i];i+=1
  if t=="-o":i+=1;continue
  if t=="-c":continue
  require(not t.startswith("@"),"response files unsupported")
  out.append(t)
 return out+extra
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for n in ("repo","cc","cxx","sdk","openssl-root","output"):ap.add_argument("--"+n,type=P,required=True)
 a=ap.parse_args()
 require(__debug__ and not sys.flags.optimize,"Run unoptimized Python")
 require(platform.system()=="Darwin","Preserved alias/dlopen recipe is Darwin-only")
 repo=a.repo.resolve();fixture=P(__file__).resolve().parent/"native_runtime_v5"
 support=repo/"test/native_worker_v5";out=a.output.resolve()
 require(not out.exists() and not inside(out,repo),"Fresh output outside repository required")
 require(len(str(out))+80<255,"Output path too long for actual Runtime fixture")
 out.mkdir(parents=True)
 env={"PATH":"/usr/bin:/bin:/usr/sbin:/sbin","HOME":os.environ["HOME"],"LANG":"C","PYTHONDONTWRITEBYTECODE":"1","UBSAN_OPTIONS":"halt_on_error=1:print_stacktrace=1"}
 evidence={"schema":1,"status":"preparing","commands":[],"cases":[],"dependencies":{},"objects":{},"binaries":{},"scope":{"actual_Runtime_Graph_Module_worker":True,"modeled_ELF_kernel_scheduler_retirement_boundaries":True,"Darwin_only":True,"network_io":False,"target_execution":False}}
 def save():(out/"RESULT.json").write_text(json.dumps(evidence,sort_keys=True,indent=2)+"\n")
 def run(label,argv,cwd=out,timeout=180,negative=False):
  row={"label":label,"argv":list(map(str,argv)),"cwd":str(cwd),"expected_negative":negative};evidence["commands"].append(row);save()
  p=out/(label+".stdout")
  with p.open("xb") as stream:
   try:row["rc"]=subprocess.run(row["argv"],cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout).returncode
   except subprocess.TimeoutExpired:row["rc"]=124
   except OSError as e:stream.write(repr(e).encode());row["rc"]=125
  row.update(path=str(p),sha256=H(p),bytes=p.stat().st_size);save()
  text=p.read_text(errors="replace")
  require((row["rc"]!=0 and row["rc"] not in (124,125)) if negative else row["rc"]==0,(label,text[-8192:]))
  return text
 try:
  pins=json.loads((fixture/"PACKAGE_PINS.json").read_text())
  require({str(p.relative_to(fixture)) for p in fixture.rglob("*") if p.is_file()}==set(pins)|{"PACKAGE_PINS.json"},"exact fixture inventory")
  require(not any(p.is_symlink() for p in fixture.rglob("*")),"fixture symlinks forbidden")
  snapshots={}
  def pin(p,h=None):
   p=P(p).resolve();require(p.is_file(),("missing",str(p)));now=H(p)
   require(h is None or now==h,("changed input",str(p)))
   old=snapshots.setdefault(str(p),now);require(old==now,("changed input",str(p)));return now
  for rel,h in pins.items():pin(fixture/rel,h)
  pin(fixture/"PACKAGE_PINS.json");pin(P(__file__))
  source_pins=json.loads((fixture/"SOURCE_PINS.json").read_text())
  for rel,h in source_pins.items():pin(repo/rel,h)
  support_pins=json.loads((support/"PACKAGE_PINS.json").read_text())
  require(str((support/"PACKAGE_PINS.json").resolve()) in snapshots,"support manifest must be source-bound")
  members={str(p.relative_to(support)) for p in (support/"support").rglob("*") if p.is_file()}
  require(members=={p for p in support_pins if p.startswith("support/")},"exact inherited support inventory")
  require(not any(p.is_symlink() for p in (support/"support").rglob("*")),"support symlinks forbidden")
  for rel in members:pin(support/rel,support_pins[rel])
  recipe=json.loads((fixture/"RECIPE.json").read_text())
  require(recipe["schema"]==1 and recipe["platform"]=="Darwin","recipe contract")
  sdk=a.sdk.resolve();ssl=a.openssl_root.resolve();cc=str(a.cc.absolute());cxx=str(a.cxx.absolute())
  require(sdk.is_dir() and (ssl/"include").is_dir(),"explicit SDK/OpenSSL roots")
  tools={}
  for label,path in (("cc",cc),("cxx",cxx)):
   real=P(path).resolve();tools[path]={"realpath":str(real),"sha256":pin(real),"version":run(label+"-version",[path,"--version"])}
  resource=P(run("cc-resource",[cc,"-print-resource-dir"]).strip()).resolve()
  pin(ssl/"lib/libcrypto.dylib")
  external=[sdk,resource,(ssl/"include").resolve(),P(cc).resolve().parent.parent/"include",P(cxx).resolve().parent.parent/"include"]
  evidence["inputs"]={"repo":str(repo),"sdk":str(sdk),"openssl":str(ssl),"tools":tools,"libcrypto_sha256":H(ssl/"lib/libcrypto.dylib")}
  aliases=[x.split(",")[-1] for x in recipe["modes"]["normal"]["link"] if x.startswith("-Wl,-alias,")]
  require(len(aliases)==16 and len(set(aliases))==16,"exact16 alias contract")
  symbols=[]
  for v in (1,2,5):symbols+=re.findall(r"RISC_OS_CPU_SYMBOL\((\w+)\)",(repo/("lib/elf_loader/include/private/privileged_os_cpu_symbols_v%d.def"%v)).read_text())
  require(set(aliases)<=set(symbols),"alias inventory")
  expected="\n".join('const unsigned char fixture_%d[] __asm__("%s")={1};'%(i,n) for i,n in enumerate(sorted(set(symbols))) if n not in aliases)+"\n"
  require((support/"support/exports.c").read_text()==expected,"modeled export inventory")
  def recheck():
   for p,h in snapshots.items():require(H(p)==h,("input drift",p))
  for mode in ("normal","ubsan"):
   work=out/mode;shutil.copytree(support/"support",work)
   template=work/"shims/module-native.h";text=template.read_text();require(text.count("@REPO@")==1,"module template");template.write_text(text.replace("@REPO@",str(repo)))
   for p in work.rglob("*"):
    if p.is_file():pin(p)
   subs={"{REPO}":str(repo),"{WORK}":str(work),"{TASK}":str(fixture),"{FIXTURE}":str(fixture),"{CC}":cc,"{CXX}":cxx,"{OPENSSL}":str(ssl)}
   def mapped(argv):
    out=[]
    for x in argv:
     for k,v in subs.items():x=x.replace(k,v)
     require("{" not in x and "}" not in x and not x.startswith("@"),"unexpanded argument")
     out.append(x)
    out[1:1]=["-isysroot",str(sdk)];return out
   def preflight(argv,label):
    text=run(label+"-dependencies",diagnostic(argv,["-M"]),work).replace("\\\n"," ")
    require(":" in text,"dependency syntax")
    for value in shlex.split(text.split(":",1)[1]):
     p=P(value);p=(work/p).resolve() if not p.is_absolute() else p.resolve();key=str(p)
     if key not in snapshots:
      require(any(inside(p,x) for x in external),("unqualified dependency",key));pin(p)
     require(H(p)==snapshots[key],("dependency drift",key));evidence["dependencies"][key]=snapshots[key]
     require(not re.search(r"privileged_os_cpu_symbols_v[34]\.def$",key),"intermediate ABI forbidden")
    macros=run(label+"-macros",diagnostic(argv,["-E","-dM"]),work)
    require(not re.search(r"^#define\s+NDEBUG(?:\s|$)",macros,re.M),"assertions disabled")
    run(label+"-syntax",diagnostic(argv,["-fsyntax-only"]),work)
   if mode=="normal":
    evidence["default_profiles"]=[]
    profiles=[("default",[]),("nonnative-candidate",["-DRISC_NATIVE_ESP_IDF=0","-DRISC_CPU_SCOPE_V5_CANDIDATE=1"]),("native-off",["-DRISC_NATIVE_ESP_IDF=1","-DRISC_CPU_SCOPE_V5_CANDIDATE=0"])]
    for name,defines in profiles:
     argv=[cxx,"-isysroot",str(sdk),"-std=c++17"]+defines+["-I"+str(repo/p) for p in ["src","sdk/app","sdk/driver","sdk/hardware","lib/ArduinoJson/src","test/drivers/stubs"]]+["-c",str(fixture/"storage_probe.cpp"),"-o",str(work/(name+"-probe.o"))]
     preflight(argv,"default-"+name)
     body=run("default-"+name+"-preprocess",diagnostic(argv,["-E","-P"]),work)
     require(not re.search(r"\b(?:nativeOwners_|nativeTerminal_|nativeTerminalAttempted_|RuntimeNativeCallerTestPeer)\b",body),"default storage or test peer survived")
     binary=work/(name+"-probe")
     sizeargv=diagnostic(argv,[])+["-o",str(binary)]
     run("default-"+name+"-size-link",sizeargv,work)
     sizes=run("default-"+name+"-size-run",[str(binary)],work)
     evidence["default_profiles"].append({"name":name,"storage_absent":True,"peer_absent":True,"sizes":sizes.strip()});save()
   group=recipe["modes"][mode];require(len(group["objects"])==24,"exact24 translation units")
   for unit in group["objects"]:
    argv=mapped(unit["argv"]);label=mode+"-"+unit["label"]
    require(argv.count("-c")==1 and argv.count("-o")==1,"one source/output")
    require(str(P(argv[argv.index("-c")+1]).resolve()) in snapshots,"explicit source pin")
    preflight(argv,label);recheck();run(label+"-compile",argv,work)
    obj=argv[argv.index("-o")+1];evidence["objects"][obj]=H(obj)
   negative=mapped(next(x["argv"] for x in group["objects"] if x["label"]=="graph-compile-4"))
   negative=[x for x in negative if x!="-DRISC_NATIVE_RUNTIME_CALLER_TEST=1"]
   negative[negative.index("-c")+1]=str(fixture/"peer_negative.cpp")
   text=run(mode+"-peer-absent",diagnostic(negative,["-fsyntax-only"]),work,negative=True)
   require("private member" in text and "nativeOwners_" in text and "stageNativeOwner" in text,"guarded peer negative identity")
   link=mapped(group["link"]);require(not any(any(b in x for b in ("dynamic_lookup","flat_namespace","multiply_defined","-undefined")) for x in link),"strict actual caller link")
   recheck();run(mode+"-link",link,work);binary=link[link.index("-o")+1];evidence["binaries"][mode]=H(binary)
   san=["-fsanitize=undefined","-fno-sanitize-recover=all"] if mode=="ubsan" else []
   fixtures={}
   for name in ("app","dependency"):
    target=work/(name+".elf")
    argv=[cc,"-isysroot",str(sdk)]+san+["-std=c11","-Wall","-Wextra","-Werror","-fPIC","-fvisibility=hidden","-shared","-undefined","dynamic_lookup","-I"+str(repo/"sdk/driver"),"-I"+str(repo/"sdk/hardware"),str(fixture/(name+".c")),"-o",str(target)]
    unit=[x for x in argv if x not in ("-shared","-undefined","dynamic_lookup")];unit.insert(unit.index(str(fixture/(name+".c"))),"-c")
    preflight(unit,mode+"-"+name+"-boundary");recheck()
    run(mode+"-"+name+"-boundary",argv,work);fixtures[name]=target;pin(target)
   cases=work/"cases";cases.mkdir()
   require(cases.resolve()==cases and not any(p.is_symlink() for p in [cases,*cases.parents]),"canonical fixture path")
   for name in recipe["cases"]:
    text=run(mode+"-case-"+name,[binary,name,str(cases/name),str(fixtures["app"]),str(fixtures["dependency"])],work,timeout=30)
    require(text.count("PASS "+name+": actual Runtime selected caller; hosted boundaries; hardware=false\n")==1,"exact PASS identity")
    require("BOUNDARY network_calls=0\n" in text,"zero network calls")
    evidence["cases"].append(mode+":"+name);save()
   recheck()
  require(len(evidence["cases"])==2*len(recipe["cases"]) and len(evidence["objects"])==48,"complete case/object accounting")
  for p,h in evidence["objects"].items():require(H(p)==h,("object changed",p))
  evidence["input_snapshots"]=snapshots;evidence["status"]="passed";save()
 except Exception as e:evidence.update(status="failed",error=repr(e),traceback=traceback.format_exc());save()
 finally:
  for row in evidence["commands"]:
   if row.get("rc") and not row.get("expected_negative"):row["tail"]=P(row["path"]).read_text(errors="replace")[-8192:]
  save();print(json.dumps({"status":evidence["status"],"error":evidence.get("error"),"cases":evidence["cases"],"result_path":str(out/"RESULT.json"),"result_sha256":H(out/"RESULT.json")}),flush=True)
 return 0 if evidence["status"]=="passed" else 1
if __name__=="__main__":raise SystemExit(main())
