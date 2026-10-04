import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace as NS
import unittest
spec = importlib.util.spec_from_file_location('recovery',Path(__file__).resolve().parents[1]/'tools/recover.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
class RecoveryTests(unittest.TestCase):
 def test_refuses_ambiguous_devices_and_accepts_explicit_port(self):
  good=NS(vid=0x303a,pid=0x1001,serial_number='test-one',device='/dev/cu.usbmodemnew')
  wrong=NS(vid=0x303a,pid=0x1001,serial_number='test-two',device='/dev/cu.usbmodemother')
  self.assertEqual(module.candidates([wrong,good]),[]);self.assertEqual(module.candidates([wrong,good],good.device),[good.device]);self.assertEqual(module.candidates([good],'/dev/wrong'),[])
 def test_stops_at_success_and_only_writes_app(self):
  good=NS(vid=0x303a,pid=0x1001,serial_number="test",device='/dev/player');calls=[]
  def run(command,**kwargs):
   calls.append(command);return NS(returncode=0 if len(calls)==2 else 1)
  self.assertTrue(module.run_attempts(20,None,Path('/image'),io.StringIO(),enumerate_ports=lambda:[good],run=run))
  self.assertEqual(len(calls),2);self.assertEqual(calls[0][-2:],['0x10000','/image']);self.assertIn('usb_reset',calls[0]);self.assertNotIn('erase_flash',calls[0])
 def test_absent_device_never_flashes_and_is_bounded(self):
  t=[0];calls=[]
  def sleep(n):t[0]+=n
  self.assertFalse(module.run_attempts(2,None,Path('/image'),io.StringIO(),enumerate_ports=lambda:[],run=lambda *a,**k:calls.append(a),now=lambda:t[0],sleep=sleep));self.assertEqual(calls,[]);self.assertLess(t[0],6.2)
if __name__=='__main__':unittest.main()
