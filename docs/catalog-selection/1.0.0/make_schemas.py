#!/usr/bin/env python3
"""Generate the strict selection and observation schemas."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parent

def obj(properties, required=None):
    return {'type': 'object', 'additionalProperties': False,
            'required': list(properties) if required is None else required, 'properties': properties}

def schemas():
    digest = {'type': 'string', 'pattern': '^[a-f0-9]{64}$(?![\\s\\S])'}
    number = {'type': 'integer', 'minimum': 0, 'maximum': 9007199254740991}
    ident = {'type': 'string', 'minLength': 1, 'maxLength': 511}
    nsid = {'type': 'string', 'pattern': '^[1-9][0-9]{0,19}$(?![\\s\\S])'}
    binding = obj({'manifest_sha256': digest, 'container_sha256': digest,
                   'inventory_sha256': digest, 'context_sha256': digest,
                   'firmware_id': ident, 'session': {'type': 'integer', 'minimum': 1, 'maximum': 4294967295}})
    current = {'oneOf': [obj({'kind': {'const': 'absent'}, 'status': {'const': 1}}),
                        obj({'kind': {'const': 'directory'}, 'status': {'const': 0}, 'flags': number}),
                        obj({'kind': {'const': 'file'}, 'status': {'const': 0}, 'flags': number,
                             'bytes': number, 'sha256': digest}),
                        obj({'kind': {'const': 'error'}, 'status': {'type': 'integer', 'not': {'enum': [0, 1]}}}),
                        obj({'kind': {'const': 'uncertain'}})]}
    key = {'volume': ident, 'destination': ident, 'namespace_id': nsid}
    observed = obj(dict(key, current=current))
    namespace = obj({'namespace_id': nsid, 'writable': {'type': 'boolean'}})
    observation = obj({'observation_version': {'const': '1.0.0'}, 'binding': binding,
                       'session_usable': {'type': 'boolean'},
                       'namespaces': {'type': 'array', 'maxItems': 16, 'items': namespace},
                       'files': {'type': 'array', 'minItems': 1, 'maxItems': 4096, 'items': observed},
                       'parents': {'type': 'array', 'maxItems': 4096, 'items': observed}})
    decision = obj(dict(key, action={'enum': ['create', 'replace-observed', 'verify-equal', 'skip']},
                        current=current, consent={'type': 'boolean'}))
    selection = obj({'selection_version': {'const': '1.0.0'},
                     'mode': {'const': 'explicit-current-file-selection'}, 'binding': binding,
                     'observation_sha256': digest,
                     'files': {'type': 'array', 'minItems': 1, 'maxItems': 4096, 'items': decision}})
    for name, schema in [('observation.schema.json', observation), ('selection.schema.json', selection)]:
        yield name, dict({'$schema': 'https://json-schema.org/draft/2020-12/schema'}, **schema)

def write_schemas(output):
    output.mkdir(parents=True, exist_ok=True)
    for name, schema in schemas():
        (output / name).write_text(json.dumps(schema, indent=2) + '\n')

if __name__ == '__main__':
    write_schemas(ROOT)
