"""Synthetic updater integrity, transaction and personal-data regressions."""
from pathlib import Path
import hashlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('updater',ROOT/'tools/update_release.py')
u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)

def import_prepare():
    spec=importlib.util.spec_from_file_location('prepare_inputs',ROOT/'tools/prepare_inputs.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

class Updates(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='smgp update a\u00e7\u00e3o ')
        self.work_base=u.WORK_BASE;u.WORK_BASE=Path(self.temp.name)/'work'
        self.root=Path(self.temp.name)/'installed game';self.root.mkdir()
        self.put('.local/installation.json',json.dumps({'product':'SuperMonacoDuoRecomp','version':'0.20','games':['smgp1','smgp2']}))
        for n,data in {'.local/build-inputs/smgp1/game.bin':'SYNTHETIC ROM',
                       '.local/build-inputs/smgp2/game.bin':'SYNTHETIC ROM 2','build/native20/bin/smgp2/smgp2.exe':'old game 2',
                       '.local/launcher.ini':'custom launcher','build/native20/bin/smgp2/settings.ini':'custom input 2',
                       'build/native20/bin/smgp1/smgp1.exe':'old game','SuperMonacoDuoRecomp.exe':'old launcher',
                       'build/native20/bin/smgp1/settings.ini':'custom input',
                       '.local/controls.ini':'custom controls','.local/achievements/smgp1.dat':'progress',
                       '.local/saves/smgp1-campaign.state':'checkpoint','.local/music/custom.m4a':'custom music'}.items():self.put(n,data)
        self.personal={n:(self.root/n).read_bytes() for n in ['.local/controls.ini','.local/achievements/smgp1.dat',
            '.local/saves/smgp1-campaign.state','.local/music/custom.m4a','build/native20/bin/smgp1/settings.ini','.local/build-inputs/smgp1/game.bin','.local/launcher.ini','.local/build-inputs/smgp2/game.bin','build/native20/bin/smgp2/settings.ini']}
    def tearDown(self):u.WORK_BASE=self.work_base;self.temp.cleanup()
    def put(self,name,data):
        p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(data);return p
    def asset(self,label='0.21'):
        name=f'SuperMonacoDuoRecomp-{label}-Windows-x64.zip'
        return dict(version=label,name=name,size=100,digest='sha256:'+'a'*64,state='uploaded',
                    browser_download_url='https://github.com/'+u.REPO+'/releases/download/v'+label+'/'+name)
    def job(self):return u.new_job(self.root)
    def stage(self,job):
        stage=u.workspace(job)/'s'
        for n,data in {'SuperMonacoDuoRecomp.exe':'new launcher','build/native20/bin/smgp1/smgp1.exe':'new game','build/native20/bin/smgp2/smgp2.exe':'new game 2','.local/launcher.ini':'WRONG DEFAULT','tools/__pycache__/x.pyc':'junk','.tools/python/python.exe':'new python',
                       '.local/installation.json':json.dumps({'product':'SuperMonacoDuoRecomp','version':'0.21','games':['smgp1','smgp2']}),
                       'assets/new.txt':'new asset','.local/controls.ini':'WRONG DEFAULT',
                       'build/native20/bin/smgp1/settings.ini':'WRONG DEFAULT','.local/saves/smgp1-campaign.state':'WRONG DEFAULT'}.items():
            p=stage/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(data)
        return stage
    def assertPersonal(self):
        for n,data in self.personal.items():self.assertEqual((self.root/n).read_bytes(),data,n)
    def archive(self,job,extra=None):
        a=self.asset();prefix=a['name'][:-4]+'/'
        data={'SuperMonacoDuoRecomp.exe':b'synthetic executable','scripts/install.ps1':b'synthetic installer',
              'tools/update_release.py':b'synthetic worker','CMakeLists.txt':b'synthetic project'}
        manifest={'version':'0.21','roms_included':False,'kind':'installer','files':[
            dict(path=n,bytes=len(b),sha256=hashlib.sha256(b).hexdigest()) for n,b in data.items()]}
        data['PACKAGE-MANIFEST.json']=json.dumps(manifest).encode();path=job/'download.zip'
        with zipfile.ZipFile(path,'w') as z:
            for n,b in data.items():z.writestr(prefix+n,b)
            if extra:z.writestr(extra,b'invalid path')
        a['size']=path.stat().st_size;a['digest']='sha256:'+u.digest(path);return path,a

    def test_newest_published_test_release_is_selected_and_drafts_ignored(self):
        a=self.asset();feed=[dict(tag_name='v0.20',draft=False,assets=[]),dict(tag_name='v9.0',draft=True,assets=[]),
                             dict(tag_name='v0.21',draft=False,prerelease=True,assets=[a])]
        self.assertEqual(u.select_release(feed,'0.20')['version'],'0.21')
        self.assertIsNone(u.select_release(feed,'0.21'))
        self.assertIsNone(u.select_release(feed,'0.21.0'))
    def test_missing_duplicate_and_wrong_repository_assets_are_rejected(self):
        a=self.asset()
        for assets in [[],[a,a],[dict(a,browser_download_url='https://example.com/update.zip')]]:
            with self.subTest(assets=assets),self.assertRaises(ValueError):u.select_release([dict(tag_name='v0.21',assets=assets)],'0.20')
        for changes in [dict(size=True),dict(size=-1),dict(digest='a'*64),dict(state='uploading')]:
            with self.subTest(changes=changes),self.assertRaises(ValueError):u.validate_asset(dict(a,**changes))
    def test_invalid_versions_are_rejected(self):
        for v in ['0.21-rc1','v0.21/../x',None,'latest','0']:
            with self.subTest(v=v),self.assertRaises(ValueError):u.version(v)
    def test_parent_ads_reserved_and_aliased_paths_are_rejected(self):
        for name in ['../outside','/absolute','x/../y','x//y','C:/x','x\\y','x/CON.txt','x/file:stream','x/name.','x/name ']:
            with self.subTest(name=name),self.assertRaises(ValueError):u.child(self.root,name)
    def test_development_and_incomplete_installations_are_rejected(self):
        self.put('.git','gitdir: fixture')
        with self.assertRaisesRegex(ValueError,'development'):u.installation(self.root)
        (self.root/'.git').unlink();self.put('.local/installation.json',json.dumps({'product':'SuperMonacoDuoRecomp','status':'installing'}))
        with self.assertRaises(ValueError):u.installation(self.root)
    def test_verified_package_extracts_only_manifest_members(self):
        job=self.job();path,a=self.archive(job);p=u.extract(path,a,job)
        self.assertTrue((p/'tools/update_release.py').is_file());self.assertPersonal()
    def test_bad_download_hash_size_and_unlisted_files_are_rejected(self):
        for change in ['hash','size','extra']:
            with self.subTest(change=change):
                job=self.job();a=self.asset();extra=a['name'][:-4]+'/unlisted.txt' if change=='extra' else None
                p,a=self.archive(job,extra)
                if change=='hash':a['digest']='sha256:'+'0'*64
                if change=='size':a['size']+=1
                with self.assertRaises(ValueError):u.extract(p,a,job)
                self.assertPersonal()
    def test_zip_slip_wrong_prefix_and_case_duplicates_are_rejected(self):
        name=self.asset()['name'][:-4]
        for extra in [name+'/../escape.txt','other/file',name+'/CON',name+'/superMonacoDuoRecomp.EXE']:
            with self.subTest(extra=extra):
                job=self.job();p,a=self.archive(job,extra)
                with self.assertRaises(ValueError):u.extract(p,a,job)
    def test_successful_transaction_preserves_personal_files_and_backs_up_state(self):
        job=self.job();stage=self.stage(job);u.deploy(self.root,job,stage,log=lambda s:None)
        self.assertEqual((self.root/'build/native20/bin/smgp1/smgp1.exe').read_text(),'new game');self.assertPersonal()
        saved=json.loads((job/'previous-saves.json').read_text());entry=next(e for e in saved if e['path']=='.local/saves/smgp1-campaign.state');self.assertEqual((job/entry['backup']).read_text(),'checkpoint')
        self.assertIsNone(u.recovery_job(self.root));self.assertEqual(json.loads((job/'transaction.json').read_text())['status'],'complete')
    def test_replacement_failure_restores_prior_runtime_and_new_files(self):
        job=self.job();stage=self.stage(job)
        def fail(i):raise OSError('synthetic replacement failure')
        with self.assertRaises(OSError):u.deploy(self.root,job,stage,log=lambda s:None,after_replace=fail)
        self.assertEqual((self.root/'SuperMonacoDuoRecomp.exe').read_text(),'old launcher')
        self.assertEqual((self.root/'build/native20/bin/smgp1/smgp1.exe').read_text(),'old game')
        self.assertFalse((self.root/'assets/new.txt').exists());self.assertPersonal();self.assertIsNone(u.recovery_job(self.root))
    def test_abrupt_process_exit_is_recoverable_from_the_durable_journal(self):
        job=self.job();stage=self.stage(job);script=Path(self.temp.name)/'crash.py'
        script.write_text('import importlib.util,os\nfrom pathlib import Path\n'+
            f's=importlib.util.spec_from_file_location("u",{str(ROOT/"tools/update_release.py")!r})\nu=importlib.util.module_from_spec(s);s.loader.exec_module(u)\n'+
            f'u.WORK_BASE=Path({str(u.WORK_BASE)!r})\n'+
            f'u.deploy(Path({str(self.root)!r}),Path({str(job)!r}),Path({str(stage)!r}),after_replace=lambda i:os._exit(77))\n',encoding='utf-8')
        r=subprocess.run([sys.executable,str(script)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(r.returncode,77,r.stdout);self.assertEqual(u.recovery_job(self.root),job)
        u.recover(self.root,job,log=lambda s:None);self.assertEqual((self.root/'SuperMonacoDuoRecomp.exe').read_text(),'old launcher');self.assertPersonal()
    def test_unmanaged_paths_are_never_deployed_and_settings_are_backed_up(self):
        for rel in ['.tools/python/python.exe','tools/__pycache__/x.pyc','.local/launcher.ini','.local/controls.ini','build/native20/bin/smgp1/settings.ini','build/native20/bin/smgp1/save.dat']:
            self.assertFalse(u.managed(rel),rel)
        for rel in ['build/native20/bin/smgp1/smgp1.exe','build/native20/bin/SDL2.dll','src/game/x.c','SuperMonacoDuoRecomp.exe']:self.assertTrue(u.managed(rel),rel)
        job=self.job();stage=self.stage(job);u.deploy(self.root,job,stage,log=lambda s:None)
        self.assertFalse((self.root/'.tools').exists());self.assertFalse((self.root/'tools/__pycache__').exists())
        saved={e['path'] for e in json.loads((job/'previous-saves.json').read_text())}
        self.assertTrue({'.local/controls.ini','.local/launcher.ini','build/native20/bin/smgp1/settings.ini','build/native20/bin/smgp2/settings.ini','.local/saves/smgp1-campaign.state'}<=saved)
    def test_build_folder_and_game_names_match_the_installer_and_launcher_layout(self):
        text=(ROOT/'scripts/install.ps1').read_text(encoding='utf-8')
        self.assertIn(f"$BuildFolder='{u.BUILD}'",text);self.assertEqual(u.GAMES,('smgp1','smgp2'));self.assertEqual(u.PRODUCT,'SuperMonacoDuoRecomp')
        self.assertRegex(text,r"\$version='"+u.CURRENT+"'")
    def test_only_github_https_redirects_are_followed(self):
        h=u.GitHubRedirect();req=u.urllib.request.Request('https://github.com/x')
        for url in ['http://github.com/x','https://example.com/x','https://githubusercontent.com.evil.test/x']:
            with self.subTest(url=url),self.assertRaises(ValueError):h.redirect_request(req,None,302,'Found',{},url)
        h.redirect_request(req,None,302,'Found',{},'https://objects.githubusercontent.com/x')
    def test_download_is_size_and_hash_checked_without_network(self):
        job=self.job();path,a=self.archive(job);data=path.read_bytes()
        import contextlib
        class Response(io.BytesIO):
            def __enter__(self):return self
            def __exit__(self,*x):self.close()
        with unittest.mock.patch.object(u,'games_closed'),unittest.mock.patch.object(u.shutil,'disk_usage',return_value=type('D',(),{'free':10*1024**3})()):
            j,p=u.prepare(self.root,a,opener=lambda url:Response(data),progress=lambda s:None)
            self.assertTrue((p/'scripts/install.ps1').is_file())
            with self.assertRaises(ValueError):u.prepare(self.root,dict(a,digest='sha256:'+'1'*64),opener=lambda url:Response(data),progress=lambda s:None)
            with self.assertRaises(ValueError):u.prepare(self.root,a,opener=lambda url:Response(data+b'x'),progress=lambda s:None)
        self.assertPersonal()
    def test_wrong_job_ownership_and_corrupt_backups_cannot_be_recovered(self):
        job=self.job();u.write_json(job/'owner.json',{'owner':'other','root':str(self.root)})
        with self.assertRaises(ValueError):u.validate_job(self.root,job)
        self.assertPersonal()
    def test_second_update_cannot_acquire_the_installation_lock(self):
        with u.update_lock(self.root):
            with self.assertRaises((ValueError,BlockingIOError)):
                with u.update_lock(self.root):pass
    def test_owned_stage_cleanup_does_not_remove_previous_version_backup(self):
        job=self.job();stage=self.stage(job);u.deploy(self.root,job,stage,log=lambda s:None);u.cleanup_stage(self.root,job)
        entries=json.loads((job/'transaction.json').read_text())['entries'];entry=next(e for e in entries if e['path']=='SuperMonacoDuoRecomp.exe')
        self.assertFalse(stage.exists());self.assertTrue((job/entry['backup']).exists());self.assertPersonal()

if __name__=='__main__':unittest.main()
