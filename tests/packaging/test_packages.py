"""Acceptance-based package fixtures; no SDK or connected device."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package', ROOT/'tools/firmware_package.py')
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)


class Packages(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='package test spaces ')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root/'repo'
        self.repo.mkdir()
        for name in ('sdkconfig.defaults', 'sdkconfig.ads131-diagnostic', 'docs/BOARD_V1_PACKAGE_GUIDE.md', 'tools/firmware_package.py'):
            dest = self.repo/name
            dest.parent.mkdir(exist_ok=True, parents=True)
            shutil.copyfile(ROOT/name, dest)
        def git(*args):
            return subprocess.check_output(['git', '-C', str(self.repo), *args], stderr=subprocess.DEVNULL).decode().strip()
        git('init'); git('add', '.'); git('-c', 'user.name=Package Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture')
        self.sha = git('rev-parse', 'HEAD')
        self.build = self.root/'build'
        self.build.mkdir()
        self.evidence = self.root/'evidence'
        self.evidence.mkdir()
        # Independently authored minimal generated IDF-shaped metadata.
        self.meta = {'write_flash_args': ['--flash-mode','dio','--flash-size','2MB','--flash-freq','80m'],
                     'flash_settings': {'flash_mode':'dio','flash_size':'2MB','flash_freq':'80m'},
                     'flash_files': {'0x0':'bootloader/bootloader.bin','0x8000':'partition_table/partition-table.bin','0x10000':'noctinirs2.bin'},
                     'bootloader': {'offset':'0x0','file':'bootloader/bootloader.bin','encrypted':'false'},
                     'partition-table': {'offset':'0x8000','file':'partition_table/partition-table.bin','encrypted':'false'},
                     'app': {'offset':'0x10000','file':'noctinirs2.bin','encrypted':'false'},
                     'extra_esptool_args': {'before':'default-reset','after':'hard-reset','chip':'esp32s3','stub':True}}
        self.write_meta()
        self.config = ('CONFIG_IDF_TARGET="esp32s3"\nCONFIG_NN2_PROFILE_FAKE=y\nCONFIG_ESPTOOLPY_FLASHSIZE="2MB"\n'
                       'CONFIG_ESPTOOLPY_FLASHMODE="dio"\nCONFIG_ESPTOOLPY_FLASHFREQ="80m"\nCONFIG_ESP_CONSOLE_UART_DEFAULT=y\n'
                       'CONFIG_ESP_CONSOLE_UART_NUM=0\nCONFIG_ESP_CONSOLE_UART_BAUDRATE=115200\n')
        (self.evidence/'sdkconfig').write_text(self.config)
        (self.evidence/'toolchain.txt').write_text('SOURCE_SHA='+self.sha+'\nIDF commit: '+pkg.IDF+'\nGITHUB_RUN_ID=42 attempt=1\nEVENT=push\n')
        for name, raw in [('bootloader/bootloader.bin',b'\xe9'+b'B'*100),('partition_table/partition-table.bin',b'\xaa\x50'+b'P'*200),('noctinirs2.bin',b'\xe9'+b'A'*300)]:
            dest=self.build/name; dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(raw)
        base='--flash-mode dio --flash-freq 80m --flash-size 2MB\n'
        lines=['0x0 bootloader/bootloader.bin\n','0x8000 partition_table/partition-table.bin\n','0x10000 noctinirs2.bin\n']
        for name, content in [('flash_args',''.join(lines)),('flash_project_args',''.join(lines)),('flash_app_args',lines[2]),('flash_bootloader_args',lines[0])]:
            (self.build/name).write_text(base+content)
        self.archive=self.root/'valid.zip'
        self.generate()
        self.files=pkg.inventory(self.archive)

    def write_meta(self):
        (self.build/'flasher_args.json').write_text(json.dumps(self.meta))

    def generate(self):
        return pkg.create(self.build,self.evidence,self.repo,self.sha,'fake',{'run_id':42,'attempt':1,'event':'push'},self.archive)

    def archive_files(self, files):
        path=self.root/'changed.zip'
        with zipfile.ZipFile(path,'w') as z:
            for n,v in files.items(): z.writestr(n,v)
        return path

    def reject(self, mutate, recompute=False):
        f=copy.deepcopy(self.files); mutate(f)
        if recompute:
            f['SHA256SUMS']=''.join(pkg.digest(v)+'  '+n+'\n' for n,v in sorted(f.items()) if n!='SHA256SUMS').encode()
        with self.assertRaises((ValueError,KeyError)):
            pkg.validate(self.archive_files(f))

    def test_positive_zip_and_clean_extraction_with_spaces(self):
        pkg.validate(self.archive,self.sha,'fake')
        dest=self.root/'clean extracted package with spaces'; dest.mkdir()
        with zipfile.ZipFile(self.archive) as z: z.extractall(dest)
        m=pkg.validate(dest,self.sha,'fake')
        self.assertEqual(len(m['images']),3)
        r=subprocess.run([sys.executable,str(dest/'validate_package.py'),'validate',str(dest),'--expect-sha',self.sha],capture_output=True)
        self.assertEqual(r.returncode,0,r.stderr)

    def test_missing_each_required_image_even_rehashed(self):
        for n in [x for x in self.files if x.startswith('images/')]:
            with self.subTest(n=n): self.reject(lambda f:f.pop(n),True)

    def test_tamper_and_truncation(self):
        for data in [b'\xe9'+b'X'*300,b'\xe9A',b'']:
            with self.subTest(size=len(data)):
                self.reject(lambda f:f.update({'images/noctinirs2.bin':data}))

    def test_tamper_recomputed_checksums_stale_manifest(self):
        self.reject(lambda f:f.update({'images/noctinirs2.bin':b'\xe9tampered'}),True)

    def test_manifest_drops_required_image_even_rehashed(self):
        def mutate(f):
            m=pkg.loads(f['manifest.json']); m['images']=m['images'][:-1]; f['manifest.json']=pkg.dumps(m)
            f.pop('images/noctinirs2.bin')
        self.reject(mutate,True)

    def test_duplicate_json_keys_before_normalization(self):
        for n in ['manifest.json','metadata/flasher_args.json','metadata/provenance.json']:
            with self.subTest(n=n): self.reject(lambda f:f.update({n:b'{"a":1,"a":2}'}),True)

    def test_wrong_expected_identities(self):
        for key,value in [('expect_sha','0'*40),('expect_profile','diagnostic'),('expect_config','0'*64),('expect_tool','0'*40)]:
            with self.subTest(key=key),self.assertRaises(ValueError): pkg.validate(self.archive,**{key:value})

    def test_wrong_target_profile_config_and_security(self):
        for old,new in [('esp32s3','esp32'),('CONFIG_NN2_PROFILE_FAKE=y','CONFIG_NN2_PROFILE_ADS131_DIAGNOSTIC=y'),('2MB','16MB')]:
            with self.subTest(new=new): self.reject(lambda f:f.update({'metadata/sdkconfig':self.config.replace(old,new).encode()}),True)
        for key in ['SPIRAM','SECURE_BOOT','FLASH_ENCRYPTION_ENABLED']:
            with self.subTest(key=key): self.reject(lambda f:f.update({'metadata/sdkconfig':(self.config+'CONFIG_'+key+'=y\n').encode()}),True)

    def test_unsafe_paths_and_no_command_execution(self):
        marker=self.root/'COMMAND_EXECUTED'
        for path in ['../evil.bin','/tmp/evil.bin','C:/evil.bin','//server/share','a\\b.bin','a/../b.bin','a//b.bin', 'NUL.bin', 'a:stream']:
            def mutate(f):
                m=copy.deepcopy(self.meta); m['flash_files']['0x10000']=path
                f['metadata/flasher_args.json']=pkg.dumps(m)
            with self.subTest(path=path): self.reject(mutate,True)
        self.reject(lambda f:f.update({'metadata/flash_args':('$(touch '+str(marker)+')').encode()}),True)
        self.assertFalse(marker.exists())

    def test_addresses_overlap_alias_out_of_capacity_and_missing_roles(self):
        for off in ['0x1','0x0000','0x200000']:
            def mutate(f):
                m=copy.deepcopy(self.meta); name=m['flash_files'].pop('0x10000'); m['flash_files'][off]=name
                f['metadata/flasher_args.json']=pkg.dumps(m)
            with self.subTest(off=off): self.reject(mutate,True)
        def drop(f):
            m=copy.deepcopy(self.meta); m['flash_files'].pop('0x8000'); f['metadata/flasher_args.json']=pkg.dumps(m)
            manifest=pkg.loads(f['manifest.json']); manifest['images']=[r for r in manifest['images'] if r['address']!=0x8000]; f['manifest.json']=pkg.dumps(manifest)
            f.pop('images/partition_table/partition-table.bin')
        self.reject(drop,True)

    def test_inconsistent_arguments_and_unknown_files(self):
        self.reject(lambda f:f.update({'metadata/flash_app_args':b'--flash-size 2MB\n0x20000 noctinirs2.bin\n'}),True)
        self.reject(lambda f:f.update({'images/compiler-ABI.bin':b'not flash'}),True)
        self.reject(lambda f:f.update({'metadata/flasher_args.json':pkg.dumps({**self.meta,'unexpected':'/etc/passwd'})}),True)

    def test_stale_source_toolchain_and_manifest(self):
        self.reject(lambda f:f.update({'metadata/toolchain.txt':f['metadata/toolchain.txt'].replace(self.sha.encode(),b'0'*40)}),True)
        def mutate(f):
            m=pkg.loads(f['manifest.json']); m['images'][0]['address']=99; f['manifest.json']=pkg.dumps(m)
        self.reject(mutate,True)

    def test_archive_escape_symlink_and_duplicate(self):
        for name in ['../escape.bin','C:/escape.bin','/escape.bin']:
            with self.subTest(name=name): self.reject(lambda f:f.update({name:b'x'}))
        p=self.root/'special.zip'
        for symlink in [True,False]:
            with zipfile.ZipFile(p,'w') as z:
                if symlink:
                    i=zipfile.ZipInfo('link'); i.create_system=3; i.external_attr=(stat.S_IFLNK|0o777)<<16; z.writestr(i,'/tmp/outside')
                else:
                    z.writestr('a',b'1'); z.writestr('A',b'2')
            with self.assertRaises(ValueError): pkg.validate(p)

    def test_generator_rejects_missing_tamper_config_duplicate_and_paths(self):
        self.archive=self.root/'negative.zip'
        (self.build/'bootloader/bootloader.bin').unlink()
        with self.assertRaises(ValueError): self.generate()
        (self.build/'bootloader/bootloader.bin').write_bytes(b'\xe9B')
        for raw in [b'{"flash_files":{},"flash_files":{}}',pkg.dumps({**self.meta,'flash_files':{'0x0':'../escape.bin'}})]:
            (self.build/'flasher_args.json').write_bytes(raw)
            with self.assertRaises(ValueError): self.generate()
        self.write_meta(); (self.evidence/'sdkconfig').write_text(self.config.replace('esp32s3','esp32'))
        with self.assertRaises(ValueError): self.generate()

    def test_cli_failure_nonzero_and_no_subprocess_in_validation(self):
        f=copy.deepcopy(self.files); f.pop('images/noctinirs2.bin')
        p=self.archive_files(f)
        r=subprocess.run([sys.executable,str(ROOT/'tools/firmware_package.py'),'validate',str(p)],capture_output=True)
        self.assertEqual(r.returncode,1)
        from unittest.mock import patch
        with patch.object(pkg.subprocess,'check_output',side_effect=AssertionError('validation executed a command')):
            pkg.validate(self.archive)


if __name__=='__main__': unittest.main()
