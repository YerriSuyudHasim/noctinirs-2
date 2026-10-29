"""Compile the production USB parser with a fake byte stream; no COM port access."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class UsbLineEndingsTests(unittest.TestCase):
    def test_production_parser(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp)
            (dest / 'driver').mkdir()
            (dest / 'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n'
                '#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n')
            (dest / 'driver/usb_serial_jtag.h').write_text('''
#include <stddef.h>
typedef struct { int tx_buffer_size, rx_buffer_size; } usb_serial_jtag_driver_config_t;
#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() {0,0}
int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *c);
int usb_serial_jtag_write_bytes(const void *p, size_t n, int wait);
int usb_serial_jtag_read_bytes(void *p, size_t n, int wait);
''')
            (dest / 'driver/usb_serial_jtag_vfs.h').write_text('void usb_serial_jtag_vfs_use_driver(void);\n')
            source = '#include "' + (ROOT / 'components/nn2_provision/nn2_provision.c').as_posix() + '"\n'
            source += r'''
#include <assert.h>
#include <stdio.h>
static const char *input;
static unsigned statuses, safeoffs, malformed, rejected;
int usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *c) { (void)c; return 0; }
void usb_serial_jtag_vfs_use_driver(void) {}
int usb_serial_jtag_write_bytes(const void *p, size_t n, int w) {
    (void)w;
    if (!strncmp(p,"ERR malformed",13)) ++malformed;
    return (int)n;
}
int usb_serial_jtag_read_bytes(void *p,size_t n,int w) {
    (void)w; size_t count=input?strlen(input):0;
    if(count>n) count=n;
    if(count) { memcpy(p,input,count); input+=count; }
    return (int)count;
}
nn2_status_t nn2_app_provision(const char *p,size_t n,const nn2_port_network_config_t *s,nn2_network_config_t *o) {
    (void)p;(void)n;(void)s;(void)o; ++rejected; return NN2_ERR_INVALID_ARG;
}
static void status(char *p,size_t n) { ++statuses; snprintf(p,n,"STATUS boot=1\n"); }
static nn2_status_t off(void) { ++safeoffs; return NN2_OK; }
static void feed(const char *s,uint64_t t) { input=s; (void)nn2_provision_poll(t); }
int main(void) {
    assert(nn2_provision_init(NULL,status,off)==ESP_OK);
    feed("STATUS\r",1); assert(statuses==1);
    feed("STATUS\n",2); assert(statuses==2);
    feed("STATUS\r\n",3); assert(statuses==3);
    feed("STA",4); feed("TUS\r",5); feed("\n",6); assert(statuses==4);
    feed("STATUS\rSTATUS\r",7); assert(statuses==6);
    feed("SAFE_OFF\r",8); feed("\n",9); assert(safeoffs==1);
    feed("STA",10); feed("",10000011); feed("TUS\r",10000012);
    assert(malformed==1 && statuses==6);
    feed("STATUS\r",10000013); assert(statuses==7);
    char oversized[196]; memset(oversized,'X',195); oversized[195]=0;
    feed(oversized,10000014); feed(input,10000015); feed(input,10000016); feed(input,10000017);
    feed("XX\r\nSTATUS\n",10000018); assert(malformed==2 && statuses==8);
    assert(rejected==0);
    puts("PASS: CR, LF, CRLF, split input, repeated commands, SAFE_OFF once, timeout recovery, overflow recovery");
    return 0;
}
'''
            (dest / 'parser.c').write_text(source)
            includes = [dest] + [ROOT / 'components' / name / 'include'
                for name in ('nn2_provision', 'nn2_app', 'nn2_ports', 'nn2_core')]
            if os.name == 'nt':
                setup = Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/VsDevCmd.bat')
                if not setup.exists():
                    self.skipTest('MSVC environment unavailable')
                command = 'cl /nologo /std:c11 /W3 ' + ' '.join('/I"' + str(p) + '"' for p in includes)
                command += ' parser.c /Fe:parser.exe'
                (dest / 'build.cmd').write_text('@echo off\ncall "' + str(setup) +
                    '" -arch=x64 -host_arch=x64 >nul\n' + command + '\n')
                build = ['cmd', '/c', str(dest / 'build.cmd')]
                binary = dest / 'parser.exe'
            else:
                cc = shutil.which('cc')
                if not cc:
                    self.skipTest('C compiler unavailable')
                binary = dest / 'parser'
                build = [cc, '-std=c11', 'parser.c', '-o', str(binary)] + ['-I'+str(p) for p in includes]
            result = subprocess.run(build, cwd=dest, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], cwd=dest, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS:', result.stdout)


if __name__ == '__main__':
    unittest.main()
