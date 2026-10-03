#include "runtime/drivers/ProviderGraphV2.h"
#include "runtime/drivers/ProviderOwnedSpecV2.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace RuntimeProviders;

int main(int argc, char** argv) {
  assert(argc == 2);
  // Ordinary graph metadata MUST remain intact when caller buffers change or
  // go out of scope. The loaded ELF identity/capability still come from copies.
  char id[96] = "fixture-root";
  char cap[96] = "cap.root";
  char path[512]{};
  assert(std::strlen(argv[1]) < sizeof(path));
  std::strcpy(path, argv[1]);
  GraphV2 graph;
  assert(graph.addVerified({id, path, cap, 1, nullptr, 0}));
  std::strcpy(id, "forged-id");
  std::strcpy(cap, "cap.evil");
  std::strcpy(path, "/missing/tampered.so");
  assert(!graph.acquire("cap.evil", 1).slot);
  auto grant = graph.acquireFrom("fixture-root", "cap.root", 1);
  assert(grant.slot && graph.interfaceFor(grant));
  assert(graph.release(grant) && graph.shutdown());

  // Privileged metadata and the COMPLETE candidate image are independently
  // owned at registration, before the private loader takes its second copy.
  char privilegedId[96] = "physical-provider";
  char privilegedCap[96] = "cap.physical";
  char requirementName[96] = "cap.clock";
  RequirementV2 requirements[] = {{requirementName, 1}};
  char firstImport[128] = "esp_intr_alloc";
  char secondImport[128] = "malloc";
  const char* names[] = {firstImport, secondImport};
  uint8_t candidate[] = {0x7f, 'E', 'L', 'F', 1, 2, 3, 4};
  SpecV2 request{privilegedId, nullptr, privilegedCap, 1, requirements, 1};
  request.requiredOsCpuAbi = 1;
  request.verifiedElfBytes = candidate;
  request.verifiedElfLength = sizeof(candidate);
  request.declaredImports = names;
  request.declaredImportCount = 2;
  request.contentSha256[0] = 0xab;
  request.packageManifestSha256[0] = 0xcd;
  request.packageSourceStamp = {3,4,true};
  OwnedNodeV2 owned;
  assert(owned.snapshot(request));
  assert(owned.spec.id != request.id && owned.spec.provides != request.provides);
  assert(owned.spec.requirements != request.requirements);
  assert(owned.spec.declaredImports != request.declaredImports);
  assert(owned.spec.verifiedElfBytes != request.verifiedElfBytes);
  std::strcpy(privilegedId, "forged");
  std::strcpy(privilegedCap, "cap.forged");
  std::strcpy(requirementName, "cap.forged");
  std::strcpy(firstImport, "faked_import");
  std::strcpy(secondImport, "other_import");
  candidate[0] = 0;
  request.contentSha256[0] = 0;
  request.packageManifestSha256[0] = 0;
  request.packageSourceStamp = {};
  assert(std::strcmp(owned.spec.id, "physical-provider") == 0);
  assert(std::strcmp(owned.spec.provides, "cap.physical") == 0);
  assert(std::strcmp(owned.spec.requirements[0].capability, "cap.clock") == 0);
  assert(std::strcmp(owned.spec.declaredImports[0], "esp_intr_alloc") == 0);
  assert(std::strcmp(owned.spec.declaredImports[1], "malloc") == 0);
  assert(owned.spec.verifiedElfBytes[0] == 0x7f);
  assert(owned.spec.contentSha256[0] == 0xab);
  assert(owned.spec.packageManifestSha256[0] == 0xcd);
  assert(owned.spec.packageSourceStamp.mount == 3 && owned.spec.packageSourceStamp.mutation == 4 && owned.spec.packageSourceStamp.quiescent);

  // Reject overlong identifiers, paths, imports, and empty/malformed shapes.
  char overlongPath[513]{};
  std::memset(overlongPath, 'a', 512);
  overlongPath[512] = '\0';
  SpecV2 oversized{"id", overlongPath, "cap", 1, nullptr, 0};
  OwnedNodeV2 rejectedPath;
  assert(!rejectedPath.snapshot(oversized));
  char overlongImport[129]{};
  std::memset(overlongImport, 'a', 128);
  overlongImport[128] = '\0';
  const char* oversizedNames[] = {overlongImport};
  SpecV2 oversizedPriv{"id", nullptr, "cap", 1, nullptr, 0};
  oversizedPriv.requiredOsCpuAbi = 1;
  oversizedPriv.verifiedElfBytes = candidate;
  oversizedPriv.verifiedElfLength = sizeof(candidate);
  oversizedPriv.declaredImports = oversizedNames;
  oversizedPriv.declaredImportCount = 1;
  OwnedNodeV2 rejectedImport;
  assert(!rejectedImport.snapshot(oversizedPriv));
  std::puts("Provider ownership: copied ordinary identities and privileged imports, dependencies, digest and ELF snapshot PASS");
}
