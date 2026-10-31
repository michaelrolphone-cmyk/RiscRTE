#!/usr/bin/env python3
"""Materialize the narrow Mbed TLS 4 compatibility fix for the pinned SDK.

Keep the SDK checkout immutable. Both the target and certificate regression
compile this same generated source, retaining upstream licensing/provenance.
"""
import hashlib
import pathlib
import re
import sys

source, output = map(pathlib.Path, sys.argv[1:])
data = source.read_bytes()
expected = 'a912969d617c93bf00efc73aaa464f5e276ed421ac78b5268e0630ec7f90cde9'
if hashlib.sha256(data).hexdigest() != expected:
    raise SystemExit('Pinned esp_crt_bundle.c changed; review compatibility fix')
text = data.decode()
text = text.replace('#include "mbedtls/psa_util.h"',
                    '#include "mbedtls/psa_util.h"\n#include "mbedtls/platform.h"')
# Mbed TLS 4 removed next_merged from mbedtls_asn1_named_data.
text = text.replace('    dst->next_merged = src->next_merged;\n', '')
# Candidate chains are freed by Mbed TLS. Allocate every candidate and subject
# node with that same allocator, preserving the native PSRAM-only TLS policy.
text = re.sub(r'\bcalloc\(', 'mbedtls_calloc(', text)
text = re.sub(r'\bfree\(', 'mbedtls_free(', text)
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != text:
    output.write_text(text)
