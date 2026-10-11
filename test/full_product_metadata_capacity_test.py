#!/usr/bin/env python3
"""Full X4 product metadata boundary, without a firmware/ELF qualification claim.

The committed fixture is a byte-exact metadata snapshot of immutable delivered
.50 input plus the three existing shared-UI provider manifests. Only temporary
copies are changed. All image slots contain a non-executable marker. Runtime's
retained-wake policy requires a schema-valid cohort identity: both test stores
therefore use a clearly synthetic metadata-test-only identity, never .50's real
firmware receipt. Versions in application/provider manifests are source metadata,
not a claim that binaries with the new declarations exist or were released.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

FIXTURE = Path(__file__).with_name("fixtures") / "x4_shared_ui_metadata.json"
MARKER = b"METADATA-ONLY: NOT AN EXECUTABLE OR FIRMWARE RECEIPT\n"
REQUEST = {"capability": "ui.text-input", "api": 1}
GRANT = dict(REQUEST, instance_id=0)
CLIENTS = ("points_in_time.json", "ble_scanner.json")
UI = (
    ("Services/scene_profile/portrait-monochrome.json", "ui-profile/manifest.json"),
    ("Services/scene_host/manifest.json", "ui-scene/manifest.json"),
    ("Services/text_input/manifest.json", "ui-text/manifest.json"),
)


def read(path):
    return json.loads(path.read_text())


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n")


def verify_records(records, root=None):
    for name, record in records.items():
        raw = record["text"].encode()
        assert hashlib.sha256(raw).hexdigest() == record["sha256"], name
        json.loads(raw)
        if root is not None:
            assert (root / name).read_bytes() == raw, f"Source snapshot differs: {name}"


def metadata_identity(original):
    identity = copy.deepcopy(original)
    identity.update(
        product="metadata-test-only", version="0.0.0", runtime_version="0.0.0",
        source_repo="test-fixture/not-a-release", source_revision="0" * 40,
        firmware_size=len(MARKER), firmware_sha256=hashlib.sha256(MARKER).hexdigest(),
    )
    return identity


def add_images(root):
    boot = read(root / "boot.json")
    for selection in boot["drivers"] + boot["app_capabilities"]:
        manifest_path = root / selection["manifest"]
        image = manifest_path.parent / read(manifest_path)["file_name"]
        image.write_bytes(MARKER)


def build_stores(parent, fixture):
    baseline, expanded = parent / "baseline", parent / "expanded"
    for name, record in fixture["baseline"].items():
        path = baseline / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(record["text"])
    original_identity = read(baseline / "cohort.json")
    write(baseline / "cohort.json", metadata_identity(original_identity))
    add_images(baseline)
    shutil.copytree(baseline, expanded)
    original_boot = read(baseline / "boot.json")
    assert len(original_boot["drivers"]) == 23
    assert len(original_boot["app_capabilities"]) == 21
    assert max(len(p["grants"]) for p in original_boot["app_capabilities"]) == 17
    boot = copy.deepcopy(original_boot)
    for source, destination in UI:
        record = fixture["ui_providers"][source]
        path = expanded / destination
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(record["text"])
        boot["drivers"].append({"manifest": destination})
    for name in CLIENTS:
        manifest = read(expanded / name)
        assert REQUEST not in manifest["requires"]
        manifest["requires"].append(REQUEST)
        write(expanded / name, manifest)
        policy = next(p for p in boot["app_capabilities"] if p["manifest"] == name)
        assert GRANT not in policy["grants"]
        policy["grants"].append(GRANT)
    write(expanded / "boot.json", boot)
    add_images(expanded)
    # Prove the additive transformation. No catalog thinning, profile rewrite,
    # changed app/provider version, namespace reassignment, or boot-policy change.
    assert boot["drivers"][:23] == original_boot["drivers"]
    restored_boot = copy.deepcopy(boot)
    restored_boot["drivers"] = restored_boot["drivers"][:23]
    for policy in restored_boot["app_capabilities"]:
        if policy["manifest"] in CLIENTS:
            assert policy["grants"].pop() == GRANT
    assert restored_boot == original_boot
    for name, record in fixture["baseline"].items():
        if name in ("boot.json", "cohort.json"):
            continue
        if name in CLIENTS:
            restored = read(expanded / name)
            assert restored["requires"].pop() == REQUEST
            assert restored == json.loads(record["text"])
        else:
            assert (expanded / name).read_bytes() == record["text"].encode(), name
    for source, destination in UI:
        assert (expanded / destination).read_bytes() == fixture["ui_providers"][source]["text"].encode()
    assert len(boot["drivers"]) == 26 and len(boot["app_capabilities"]) == 21
    assert read(expanded / "board.json")["board_id"] == "xteink-x4-pro"
    # Explicitly retain the installed X4 BLE profile and Points' file authority.
    ble = read(expanded / "ble_scanner.json")
    assert ble["version"] == "0.2.18"
    assert {"capability": "bluetooth.hci", "api": 1} in ble["requires"]
    assert {"capability": "bluetooth.sensors", "api": 1} in ble["requires"]
    points = next(p for p in boot["app_capabilities"] if p["manifest"] == CLIENTS[0])
    assert {"capability": "storage.app-data", "api": 1, "instance_id": 5} in points["grants"]
    print("PASS: preserved 23 provider selections, 21 apps, every original grant and X4 board/profile; added exactly 3 UI providers and 2 text-input grants")
    return baseline, expanded


def check(executable, baseline, candidate, case, expected, error=None):
    process = subprocess.run([str(executable), str(baseline), str(candidate)],
                             text=True, capture_output=True, check=False)
    assert process.returncode == 0, (case, process.returncode, process.stdout, process.stderr)
    result = json.loads(process.stdout)
    assert result["prepared"] == expected, (case, result, process.stderr)
    assert result["cohort_metadata_validated"] == expected, (case, result)
    assert all(result[key] == 0 for key in
               ("hardware_calls", "storage_calls", "module_calls", "cohort_rebound_platforms")), result
    if expected:
        assert result["prepared_apps"] == result["cohort_apps"] == 21, result
        assert result["prepared_providers"] == result["cohort_providers"] == 26, result
    else:
        # Reject before either the ordinary image-inspection callback or cohort
        # image-inventory callback is reached, not just before module execution.
        assert all(result[key] == 0 for key in
                   ("prepared_apps", "prepared_providers", "cohort_apps", "cohort_providers")), result
        if error:
            assert result["prepare_error"] == result["cohort_error"] == error, result
    print(json.dumps(dict(case=case, **result), sort_keys=True))


def mutate(path, transform):
    doc = read(path)
    transform(doc)
    write(path, doc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--source-store", type=Path)
    parser.add_argument("--source-system", type=Path)
    parser.add_argument("--legacy-capacity", action="store_true")
    args = parser.parse_args()
    def source_hashes():
        if args.source_store is None:
            return None
        return {str(p.relative_to(args.source_store)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(args.source_store.rglob("*")) if p.is_file()}

    before = source_hashes()
    fixture = read(FIXTURE)
    assert fixture["schema"] == "x4-shared-ui-metadata-capacity-test-v1"
    verify_records(fixture["baseline"], args.source_store)
    verify_records(fixture["ui_providers"], args.source_system)
    with tempfile.TemporaryDirectory(prefix="x4-metadata-") as temporary:
        root = Path(temporary)
        baseline, expanded = build_stores(root, fixture)
        check(args.executable, baseline, expanded, "26-provider-full-product",
              not args.legacy_capacity, "invalid driver list" if args.legacy_capacity else None)
        cases = []

        def case(name, path, transform, error=None):
            candidate = root / name
            shutil.copytree(expanded, candidate)
            mutate(candidate / path, transform)
            cases.append((candidate, name, error))
            return candidate

        overflow = case("27-provider-overflow", "boot.json", lambda d:
                        d["drivers"].append({"manifest": "overflow/manifest.json"}), "invalid driver list")
        extra = read(expanded / "ui-profile/manifest.json")
        extra["id"] = "metadata-overflow-only"
        extra["provides"][0]["capability"] = "test.metadata-overflow"
        write(overflow / "overflow/manifest.json", extra)
        (overflow / "overflow/driver.elf").write_bytes(MARKER)
        case("malformed-provider-26", "ui-text/manifest.json",
             lambda d: d.update(driver_abi=1))
        case("missing-dependency-provider-26", "ui-text/manifest.json", lambda d:
             d["requires"][0].update(capability="ui.missing-scene"), "missing dependency")
        case("malformed-dependency-provider-26", "ui-text/manifest.json", lambda d:
             d["requires"][0].update(api="1"))
        case("bad-provider-26-instance-binding", "boot.json", lambda d:
             d["drivers"][-1].update(instance_id=17))
        case("bad-last-app-binding", "boot.json", lambda d:
             d["app_capabilities"][-1]["grants"][4].update(instance_id=999),
             "app provider unavailable")
        for client in CLIENTS:
            stem = client.removesuffix(".json")
            case(f"bad-text-binding-{stem}", "boot.json", lambda d, name=client:
                 next(p for p in d["app_capabilities"] if p["manifest"] == name)["grants"][-1].update(instance_id=999),
                 "app provider unavailable")
            case(f"missing-text-grant-{stem}", "boot.json", lambda d, name=client:
                 next(p for p in d["app_capabilities"] if p["manifest"] == name)["grants"].pop(),
                 "app requirement not uniquely authorized")
            case(f"missing-text-declaration-{stem}", client,
                 lambda d: d["requires"].pop(), "undeclared app grant")
        for candidate, name, error in cases:
            if args.legacy_capacity and name != "27-provider-overflow":
                continue  # Capacity rejection cannot qualify deeper graph checks.
            check(args.executable, baseline, candidate, name, False, error)
        # Re-run the untouched full cohort after all negative fixtures.
        check(args.executable, baseline, expanded, "26-provider-recheck",
              not args.legacy_capacity, "invalid driver list" if args.legacy_capacity else None)
    verify_records(fixture["baseline"], args.source_store)
    verify_records(fixture["ui_providers"], args.source_system)
    assert source_hashes() == before, "Immutable source inventory or file hashes changed"
    if before is not None:
        print(f"PASS: all {len(before)} delivered .50 source files unchanged by SHA-256 and exact inventory")
    if args.legacy_capacity:
        print("PASS: original Runtime reproduces invalid driver list for the same 26/21 full-product metadata; no graph/binary qualification claimed")
    else:
        print("PASS: 26 accept; 27 and malformed suffix/provider/dependency/bindings reject before image inspection; zero module/hardware/storage execution")
    print("SCOPE: metadata-only host regression; no matching binary receipt, release, target build, publication, or hardware qualification")


if __name__ == "__main__":
    main()
