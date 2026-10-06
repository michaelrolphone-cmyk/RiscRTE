import configparser,pathlib,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class MetadataTargets(unittest.TestCase):
 def test_explicit_psram_filesystem_and_unchanged_embedded_targets(self):
  c=configparser.ConfigParser(interpolation=None);c.read(ROOT/'platformio.ini')
  base=c['env:esp32s3'];self.assertEqual(base['board_build.arduino.memory_type'],'qio_opi')
  self.assertIn('-DBOARD_HAS_PSRAM',base['build_flags']);self.assertNotIn('-DRISC_RUNTIME_METADATA_PSRAM=1',base['build_flags'])
  for target in ('cam-ci','x4-ci'):
   e=c['env:'+target];self.assertNotIn('-DRISC_RUNTIME_METADATA_PSRAM=1',e['build_flags']);self.assertIn('-DRISC_EMBEDDED_BOOTSTORE=1',e['build_flags'])
  self.assertIn('-DRISC_RUNTIME_METADATA_PSRAM=1',c['env:esp32s3-16mb-usb']['build_flags'])
  self.assertIn('-DRISC_PAIRED_BANKS=1',c['env:esp32s3-16mb-paired']['build_flags'])
