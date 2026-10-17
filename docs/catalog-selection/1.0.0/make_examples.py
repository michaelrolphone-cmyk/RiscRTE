#!/usr/bin/env python3
"""Regenerate reviewable cross-language vectors using source-authored fixtures."""
import argparse
from pathlib import Path
import tempfile
from fixtures import payload_at, observation, choices_for
from selection import canonical, make_selection, validate_selection


def write_examples(output):
    output.mkdir(parents=True, exist_ok=True)
    for name in ('clean', 'mixed', 'all-existing', 'all-equal', 'selected-subset', 'customized-legacy'):
        with tempfile.TemporaryDirectory() as temporary:
            kind = 'update-after' if name == 'customized-legacy' else 'bootstrap'
            payload, contents = payload_at(temporary, kind)
            scenario = 'mixed' if name == 'selected-subset' else name
            obs = observation(payload, contents, scenario)
            choices = choices_for(payload, obs, skipped=(0, 2) if name == 'selected-subset' else ())
            selected = make_selection(payload, obs, choices)
            vector = {'fixture': True, 'base_fixture': kind,
                      'description': 'Synthetic source-authored input; not a device or installable release.',
                      'observation': obs, 'selection': selected,
                      'expected': validate_selection(payload, obs, selected)}
            (output / (name + '.json')).write_bytes(canonical(vector))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'examples')
    write_examples(parser.parse_args().output)
