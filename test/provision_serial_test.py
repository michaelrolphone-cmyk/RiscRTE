import importlib.util,json,pathlib,subprocess,sys,unittest,os,shutil,tempfile
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('client',pathlib.Path(__file__).resolve().parents[1]/'scripts/provision_install.py')
client=importlib.util.module_from_spec(spec);spec.loader.exec_module(client)
server=str(pathlib.Path(sys.argv.pop(1)).resolve())
validator=str(pathlib.Path(sys.argv.pop(1)).resolve())
class Pipe:
    def __init__(self):self.process=subprocess.Popen([server,'--server'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    def write(self,data,deadline):self.process.stdin.write(data);self.process.stdin.flush()
    def line(self,deadline):return self.process.stdout.readline().rstrip(b'\n')
    def close(self):
        self.process.stdin.close();self.process.wait(timeout=5)
        assert self.process.returncode==0,self.process.stderr.read()
        self.process.stdout.close();self.process.stderr.close()
class InstallTest(unittest.TestCase):
    def test_transaction(self):
        profile=json.dumps({'schema':'riscrte.provisioning','schema_version':1,'wifi':{'ssid':'dummy','password':'dummy-password'},'files':[{'path':x,'url':'https://example.invalid/'+x,'bytes':1,'sha256':'0'*64} for x in ('boot.json','board.json','default.elf')]}).encode()
        time=json.dumps({'schema':'riscrte.sntp','schema_version':1,'servers':['time.example.invalid']}).encode()
        pipe=Pipe()
        try:
            self.assertEqual(client.transfer(pipe,'a'*40,profile,time),'RTE_INSTALL INSTALLED')
            self.assertEqual(client.transfer(pipe,'a'*40,profile,time),'RTE_INSTALL UNCHANGED')
            self.assertEqual(client.transfer(pipe,'a'*40,profile.replace(b'dummy-password',b'other-password'),b''),'RTE_INSTALL INSTALLED')
        finally:pipe.close()
    def test_relative_validator(self):
        profile={'schema':'riscrte.provisioning','schema_version':1,'wifi':{'ssid':'dummy','password':'dummy-password'},'files':[{'path':x,'url':'https://example.invalid/'+x,'bytes':1,'sha256':'0'*64} for x in ('boot.json','board.json','default.elf')]}
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);shutil.copy2(validator,root/'provision-input');(root/'profile.json').write_text(json.dumps(profile))
            previous=os.getcwd()
            try:
                os.chdir(root)
                with patch.dict(os.environ,{'PATH':'/usr/bin:/bin'}),patch.object(client,'SerialTransport',lambda _:Pipe()),patch.object(sys,'argv',['client','--validator','./provision-input','--profile','profile.json','--port','SIMULATED','--expected-source','a'*40]):client.main()
            finally:os.chdir(previous)
    def test_wrong_source(self):
        pipe=Pipe()
        try:
            with self.assertRaisesRegex(ValueError,'source mismatch'):client.transfer(pipe,'b'*40,b'{}',b'')
        finally:pipe.close()
    def test_no_retry_after_send(self):
        class Lost:
            def __init__(self):self.writes=[];self.reads=0
            def write(self,data,deadline):self.writes.append(data)
            def line(self,deadline):
                self.reads+=1
                if self.reads==1:return b'RTE_MAINTENANCE_V1 '+b'a'*40+b' 12345678'
                raise TimeoutError()
        transport=Lost()
        with self.assertRaisesRegex(RuntimeError,'unknown'):client.transfer(transport,'a'*40,b'{}',b'')
        self.assertEqual(len(transport.writes),3)
if __name__=='__main__':unittest.main()
