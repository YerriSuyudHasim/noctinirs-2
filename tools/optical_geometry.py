"""Offline geometry access. No firmware imports or runtime binding.

validate_schema implements only the vocabulary used by the bundled schemas;
unknown keywords fail closed. The files are also standard Draft 2020-12 schemas.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def ain_to_pd(ain, geometry):
    """The sole AIN-to-PD function: consult the geometry, never a second map."""
    if type(ain) is not int or not 0 <= ain < 4:
        raise ValueError('AIN must be 0..3')
    return geometry['adc']['ain_to_pd'][str(ain)]

def validate_schema(value, spec):
    known = {'$schema', 'type', 'const', 'enum', 'required', 'additionalProperties',
             'properties', 'minimum', 'maximum', 'minItems', 'maxItems',
             'items', 'prefixItems'}
    if spec is False: raise ValueError('forbidden item')
    if set(spec) - known: raise ValueError('unsupported schema vocabulary')
    types = {'object': lambda x: type(x) is dict, 'array': lambda x: type(x) is list,
             'string': lambda x: type(x) is str, 'integer': lambda x: type(x) is int,
             'number': lambda x: type(x) in (int, float), 'boolean': lambda x: type(x) is bool}
    if 'type' in spec and not types[spec['type']](value): raise ValueError('type')
    if 'const' in spec and (value != spec['const'] or type(value) is bool and type(spec['const']) is not bool): raise ValueError('const')
    if 'enum' in spec and value not in spec['enum']: raise ValueError('enum')
    for key, op in [('minimum', lambda x,y:x<y), ('maximum', lambda x,y:x>y)]:
        if key in spec and op(value,spec[key]): raise ValueError(key)
    if type(value) is dict:
        if set(spec.get('required',[]))-set(value): raise ValueError('required')
        props=spec.get('properties',{})
        if spec.get('additionalProperties') is False and set(value)-set(props): raise ValueError('extra property')
        for k,v in value.items():
            if k in props: validate_schema(v,props[k])
    if type(value) is list:
        if len(value)<spec.get('minItems',0) or len(value)>spec.get('maxItems',len(value)): raise ValueError('array length')
        prefix=spec.get('prefixItems',[])
        for i,v in enumerate(value): validate_schema(v,prefix[i] if i<len(prefix) else spec.get('items',{}))
