import importlib.util,tempfile,unittest
from pathlib import Path
from PIL import Image
spec=importlib.util.spec_from_file_location('prepare',Path(__file__).parents[1]/'tools/prepare_card.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class PrepareTests(unittest.TestCase):
 def test_rgb_layout(self):
  b=module.rgb565(Image.new('RGB',(5,3),'red'));self.assertEqual(len(b),115200);self.assertEqual(b[:2],b'\xf8\x00')
 def test_empty_card(self):
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):module.prepare(Path(d))
if __name__=='__main__':unittest.main()
