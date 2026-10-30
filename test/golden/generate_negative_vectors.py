"""Regenerate v0.4.2 rejection fixtures from existing frozen positive wire bytes.

Usage: python test/golden/generate_negative_vectors.py
No codec or encoder is used to derive expected bytes or rejection results.
"""
from copy import deepcopy
import json
from pathlib import Path


PATH = Path(__file__).with_name('optical_frame_vectors.json')
PREFIX = 'v042_'


def generate(data):
    original = [v for v in data['vectors'] if not v['name'].startswith(PREFIX)]
    sources = {v['name']: v for v in original}
    added = []

    def negative(name, source, field, value, offset, decode_only=False):
        v = deepcopy(sources[source])
        assert v['expected_result'] == v['decode_result'] == 'OK'
        v['name'] = PREFIX + name
        if field is not None:
            v['input'][field] = value
        wire = bytearray.fromhex(v['expected_hex'])
        wire[offset:offset + 2] = value.to_bytes(2, 'little')
        v['expected_hex'] = wire.hex()
        v['expected_result'] = v['decode_result'] = 'INVALID'
        if decode_only:
            # Reserved prefix words have no public C value field. Only the
            # malformed wire is tested; the encoder always emits zero there.
            v['encode'] = False
        added.append(v)
        return v

    opt = 'full_20_slots_verified_normal'
    raw = 'raw_outside_ff'
    negative('opt2_reserved_nonzero', opt, None, 1, 22, decode_only=True)
    negative('raw_reserved_nonzero', raw, None, 1, 14, decode_only=True)
    negative('raw_engineering_bit_clear', raw, 'raw_flags', 2, 10)
    for bit in range(2, 16):
        negative('raw_reserved_flag_bit_' + str(bit), raw,
                 'raw_flags', 3 | (1 << bit), 10)
    empty = negative('raw_sample_count_zero', raw, 'sample_count', 0, 8)
    empty['input']['sample'] = []
    empty['expected_hex'] = empty['expected_hex'][:32]
    negative('gated_flag_with_zero_gate_id', opt, 'gate_config_id', 0, 20)
    negative('engineering_flag_with_nonzero_gate_id',
             'engineering_values_gate_off', 'gate_config_id', 1, 20)
    for field, offset in [('n_on_used', 28), ('n_off_used', 29)]:
        for count in (0, 1):
            v = deepcopy(sources[opt])
            v['name'] = PREFIX + 'verified_' + field + '_' + str(count)
            v['input']['slot'][0][field] = count
            v['input']['stab_q'][:4] = [255] * 4
            wire = bytearray.fromhex(v['expected_hex'])
            wire[offset] = count
            # Avoid rejection by the independent n<4 stability invariant;
            # this case exercises the verified-detector n<2 check.
            wire[1304:1308] = bytes([255] * 4)
            v['expected_hex'] = wire.hex()
            v['expected_result'] = v['decode_result'] = 'INVALID'
            added.append(v)
    data['contract'] = 'optical-draft-v0.4.2-explicit-receiver-rules'
    data['vectors'] = original + added
    return data


if __name__ == '__main__':
    data = generate(json.loads(PATH.read_text(encoding='utf-8')))
    PATH.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('Generated', sum(v['name'].startswith(PREFIX) for v in data['vectors']),
          'negative vectors;', len(data['vectors']), 'total')
