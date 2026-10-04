import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
spec=importlib.util.spec_from_file_location('deploy_syncer',Path(__file__).resolve().parents[1]/'tools/deploy_syncer.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class FakeAPI:
    def __init__(self, owned=True):self.calls=[];self.owned=owned
    def call(self,path,data=None,method=None,**kwargs):
        self.calls.append((path,method))
        if path=='/containers/json?all=1':
            return [{'Id':'old','Names':['/pearlpod-syncer'],'Labels':{'org.pearlpod.role':'syncer'} if self.owned else {}}]
        if path.startswith('/containers/create?name=pearlpod-syncer-init'):return {'Id':'init'}
        if path=='/containers/init/json':return {'State':{'Running':False,'ExitCode':0}}
        if path=='/containers/create?name=pearlpod-syncer':return {'Id':'new'}
        if path=='/containers/new/start':raise RuntimeError('New image cannot start')


class DeploymentTests(unittest.TestCase):
    def args(self):
        return SimpleNamespace(image_archive=None,source_dir=None,image='image',deployment_dir='/own',music='/music',
            plex_token_file=None,plex_url='http://plex',source_root='/source',host_ip='192.0.2.2',poll_interval=60)
    def test_failed_start_restores_old_container(self):
        api=FakeAPI()
        with self.assertRaises(RuntimeError):module.deploy(api,self.args())
        paths=[p for p,m in api.calls]
        self.assertIn('/containers/new?force=true',paths)
        self.assertIn('/containers/old/rename?name=pearlpod-syncer',paths)
        self.assertEqual(paths[-1],'/containers/old/start')
    def test_unrelated_container_is_never_replaced(self):
        api=FakeAPI(False)
        with self.assertRaises(ValueError):module.deploy(api,self.args())
        self.assertEqual(len(api.calls),1)


if __name__=='__main__':unittest.main()
