"""Translate shared JSON into C value fixtures; never calculate expected wire bytes.
Usage: python generate_c_vectors.py vectors.json output.c
"""
import json
from pathlib import Path
import sys

def initializer(value):
    if isinstance(value, dict): return '{'+','.join(initializer(x) for x in value.values())+'}'
    if isinstance(value, list): return '{'+','.join(initializer(x) for x in value)+'}'
    if value == -2147483648: return 'INT32_MIN'
    return str(value)

def checks(value, path):
    if isinstance(value, dict):
        return ''.join(checks(v,path+'.'+k) for k,v in value.items())
    if isinstance(value,list):
        return ''.join(checks(v,path+'['+str(i)+']') for i,v in enumerate(value))
    return 'CHECK('+path+'=='+initializer(value)+');\n'

def generate(path):
    data=json.loads(Path(path).read_text())
    out=['#include "nn2_core_optical_codec.h"\n#include <stdio.h>\n#include <string.h>\n',
         '#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\\n", __LINE__, #x); return 1; } } while(0)\n',
         'int main(void) { nn2_oc_stats_t stats={0}; uint8_t wire[1500]; size_t written;\n']
    results={'OK':'NN2_OC_OK','INVALID':'NN2_OC_INVALID','TOO_LARGE':'NN2_OC_TOO_LARGE','UNKNOWN_LAYOUT':'NN2_OC_UNKNOWN_LAYOUT'}
    for v in data['vectors']:
        layout=v['layout']; kind='opt2' if layout==2 else 'raw'; typ='nn2_oc_'+kind+'_t'
        out.append('{ /* '+v['name']+' */\n')
        h=v['expected_hex']
        if h is not None:
            b=bytes.fromhex(h);out.append('static const uint8_t expected[]={'+','.join(map(str,b))+'};\n')
        if layout in (2,3):
            # Oversized input only carries first 100 samples into fixed C storage;
            # the declared count must be rejected before any sample is inspected.
            f=dict(v['input'])
            if layout==3:f['sample']=f['sample'][:100]
            out.append(typ+' input='+initializer(f)+';\n')
            out.append('memset(wire,0xA5,sizeof(wire)); written=999;\n')
            out.append('CHECK(nn2_oc_encode_'+kind+'(&input,wire,sizeof(wire),&written,&stats)=='+results[v['expected_result']]+');\n')
            if v['expected_result']=='OK':
                out.append('CHECK(written==sizeof(expected)); CHECK(memcmp(wire,expected,written)==0);\n')
                out.append('CHECK(nn2_oc_encode_'+kind+'(&input,wire,0,&written,&stats)==NN2_OC_TRUNCATED);\n')
            else:out.append('CHECK(written==999); for(unsigned i=0;i<sizeof(wire);++i) CHECK(wire[i]==0xA5);\n')
        if v.get('decode_result') is not None:
            out.append('nn2_oc_opt2_t opt={0}; nn2_oc_raw_t raw={0};\n')
            out.append('CHECK(nn2_oc_decode(expected,sizeof(expected),&opt,&raw,&stats)=='+results[v['decode_result']]+');\n')
            if v['decode_result']=='OK':
                out.append(checks(v.get('decoded_input',v['input']),'opt' if layout==2 else 'raw'))
                out.append('CHECK(nn2_oc_decode(expected,sizeof(expected)-1,&opt,&raw,&stats)!=NN2_OC_OK);\n')
        out.append('}\n')
    out.append('CHECK(stats.oversized_payloads==2); CHECK(stats.unsupported_sources==1); CHECK(stats.unsupported_layouts==2); CHECK(stats.warning_slots==24); stats.oversized_payloads=UINT32_MAX; CHECK(nn2_oc_decode(wire,sizeof(wire),0,0,&stats)==NN2_OC_TOO_LARGE); CHECK(stats.oversized_payloads==UINT32_MAX);\n')
    out.append('printf("PASS: '+str(len(data['vectors']))+' shared golden codec vectors, field comparisons and rejection checks\\n"); return 0; }\n')
    return ''.join(out)

if __name__=='__main__':Path(sys.argv[2]).write_text(generate(sys.argv[1]),encoding='utf-8')
