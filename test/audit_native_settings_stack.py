#!/usr/bin/env python3
"""Check two source-bound feasible linked call chains, never claim an upper bound."""
import argparse,json,pathlib
p=argparse.ArgumentParser();p.add_argument('calls',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
j=json.loads(a.calls.read_text());nodes=j['selected_functions']
fresh=[
'NativeSettingsWorker::(anonymous namespace)::run(void*)',
'NativeSettingsBootstrap::registerBeforeActivate',
'NativeSettingsBootstrap::workerStep()',
'NativeSettingsBootstrap::initializeNative()',
'NativeReplicaCore::initializeAdmitted',
'NativeReplicaCore::publish',
'backend()::{lambda(void*, unsigned long long)#5}::_FUN',
'CheckedNativeReplicaStore::seal',
'checkFreshReceiptAndArtifacts',
'inspectFreshArtifacts',
'CheckedLittleFsFreshInitArtifacts::inspect(',
'CheckedLittleFsFreshInitArtifacts::inventory(',
'CheckedLittleFsFreshInitArtifacts::file(',
'CheckedLittleFsFreshInitArtifacts::check(bool)',
'CheckedLittleFsFreshInitArtifacts::check(bool)',
'Backend::snapshot(',
'Backend::held(',
'NativeSettingsWorker::custody(',
'NativeSettingsCoordinator::workerCustody(']
atomic=[fresh[0],'NativeReplicaJobT<4096u>::run(', 'NativeAtomicKeyValue::execute(',
'NativeReplicaCore::apply(',fresh[5],fresh[6],fresh[7],
'AppDataFiles::replace(', 'AppDataFiles::quota(', 'snprintf', '_svfprintf_r']
source_edges={
(fresh[0],fresh[1]):'NativeSettingsBootstrap.cpp registerBeforeActivate installs lambda #1 as NativeClient.step; NativeSettingsCoordinator.h workerStep calls that accepted client.',
(fresh[5],fresh[6]):'CheckedNativeReplicaStore.h backend() installs seal lambda #5; NativeReplicaCore.h publish calls b_.seal.',
(fresh[14],fresh[15]):'NativeSettingsBackend.cpp Backend::port installs snapshot; CheckedLittleFsFreshInitArtifacts.h check calls port_.snapshot.',
(atomic[0],atomic[1]):'NativeReplicaJob.h installs run into the copied worker job, consumed by NativeSettingsCoordinator::workerStep/Worker::workerStep.',
(atomic[1],atomic[2]):'NativeAtomicKeyValue.h constructs job_(execute,this); NativeReplicaJob.h run calls self.fn_.'}
def chain(keys,condition):
 items=[];edges=[]
 for k in keys:
  matches=[(addr,n) for addr,n in nodes.items() if k in n['name']]
  if 'registerBeforeActivate' in k:matches=[x for x in matches if '#1}::_FUN' in x[1]['name']]
  if k in ['snprintf','_svfprintf_r']:matches=[x for x in matches if x[1]['name']==k]
  assert len(matches)==1,(k,len(matches));addr,n=matches[0];assert n['frame'] is not None
  items.append(dict(address=addr,name=n['name'],frame=n['frame']))
 for i,(prev,nxt) in enumerate(zip(items,items[1:])):
  caller=nodes[prev['address']];direct=[c for c in caller['calls'] if c['target']==int(nxt['address'],16)]
  if direct:edges.append(dict(kind='linked-direct',call_sites=[hex(c['pc']) for c in direct]))
  else:
   source=source_edges.get((keys[i],keys[i+1]));assert source,(prev,nxt)
   assert caller['unknown_calls'],caller['name']
   edges.append(dict(kind='source-bound-callback',source=source,unresolved_call_sites=[hex(x) for x in caller['unknown_calls']]))
 return dict(condition=condition,frames=items,edges=edges,entry_frame_lower_bound=sum(x['frame'] for x in items),worker_stack_bytes=8192)
result=dict(scope='Feasible static nested ENTRY-frame lower bounds. Source-bound callbacks are explicit. No dynamic stack, interrupt/window-spill, ROM or untraced callee overhead is counted. Not measured high-water and not a whole-program upper bound.',
 fresh_resume=chain(fresh,'Fresh-initialization resume with canonical replica-a.bin or .pending; file validation calls check(true), which nests check(false) and snapshot custody. inspectAttempt is inlined in this exact route.'),
 atomic_quota=chain(atomic,'Atomic saved settings mutation: publication seals a bank through AppDataFiles replace/quota, then snprintf("%s/%s"). Do not append floating-point dtoa or unreachable assert paths.'))
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
print({k:v['entry_frame_lower_bound'] for k,v in result.items() if isinstance(v,dict)})
