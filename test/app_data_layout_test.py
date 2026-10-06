import pathlib,sys,unittest,struct,hashlib
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import paired_bank_images as images
import paired_candidate as candidate
class Layout(unittest.TestCase):
 def test_legacy_and_app_data_are_separate(self):
  firmware=b'x'*64
  for new in (False,True):
   store=b'y'*(images.APP_DATA_STORE_BYTES if new else images.STORE_BYTES)
   record=images.record(0,firmware,store,new)
   self.assertEqual(images.parse_record(record,new)[5],2 if new else 1)
   with self.assertRaises(ValueError):images.parse_record(record,not new)
 def test_partition_maps(self):
  legacy=candidate.EXPECTED;new=candidate.APP_DATA_EXPECTED
  for name in ('nvs','otadata','bank_state'):self.assertEqual(legacy[name],new[name])
  self.assertEqual(new['app0'],(0,0x10,0x10000,0x260000))
  self.assertEqual(new['app1'],(0,0x11,0x800000,0x260000))
  self.assertEqual(new['bootfs0'],(1,0x82,0x2f0000,0x510000))
  self.assertEqual(new['bootfs1'],(1,0x82,0xae0000,0x510000))
  regions=sorted((v[2],v[2]+v[3]) for v in new.values())
  for (_,end),(start,_) in zip(regions,regions[1:]):self.assertLessEqual(end,start)
  self.assertEqual(new['appdata'],(1,0x41,0x270000,0x80000))
  for mapping in (legacy,new):
   rows=b''.join(struct.pack('<HBBII16sI',0x50aa,*values[:2],*values[2:],name.encode(),0) for name,values in mapping.items())
   table=rows+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(rows).digest();table+=b'\xff'*(3072-len(table))
   self.assertEqual(candidate.partitions(table,mapping),mapping)
   with self.assertRaises(ValueError):candidate.partitions(table,new if mapping is legacy else legacy)
 def test_native_and_store_capacity_bounds(self):
  for new,maximum,size in ((False,0x300000,0x4f0000),(True,0x260000,0x510000)):
   store=b's'*size
   self.assertEqual(images.parse_record(images.record(1,b'f'*maximum,store,new),new)[3],maximum)
   with self.assertRaises(ValueError):images.record(1,b'f'*(maximum+1),store,new)
   for length in (size-1,size+1):
    with self.assertRaises(ValueError):images.record(1,b'f'*64,b's'*length,new)
 def test_csv_matches_exact_maps(self):
  import csv
  kinds={'app':0,'data':1};subtypes={'nvs':2,'ota_0':0x10,'ota_1':0x11,'spiffs':0x82,'ota':0}
  for filename,mapping in (('partitions-paired.csv',candidate.EXPECTED),('partitions-paired-appdata.csv',candidate.APP_DATA_EXPECTED)):
   actual={}
   for row in csv.reader(line for line in (ROOT/filename).read_text().splitlines() if line and not line.startswith('#')):
    name,kind,subtype,offset,size=[v.strip() for v in row[:5]]
    actual[name]=(kinds[kind],subtypes[subtype] if subtype in subtypes else int(subtype,0),int(offset,0),int(size,0))
   self.assertEqual(actual,mapping)
 def test_no_autoformat_or_grow(self):
  source=(ROOT/'src/ports/esp32s3/NativeAppData.cpp').read_text()
  self.assertIn('conf.format_if_mount_failed=false',source);self.assertIn('conf.grow_on_mount=false',source)
  self.assertNotIn('esp_littlefs_format(',source);self.assertNotIn('LittleFS.begin(',source)
if __name__=='__main__':unittest.main()
