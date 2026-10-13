import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import paired_bank_images as images
from paired_candidate import APP_DATA_EXPECTED,APP_DATA_EXPANDED_EXPECTED
class ExpandedLayout(unittest.TestCase):
 def test_unchanged_data_and_boundaries(self):
  old,new=APP_DATA_EXPECTED,APP_DATA_EXPANDED_EXPECTED
  for n in ('appdata','nvs','app0','otadata','bank_state'):self.assertEqual(old[n],new[n])
  self.assertEqual(new['appdata'][3],524288)
  rows=sorted(new.values(),key=lambda x:x[2])
  for a,b in zip(rows,rows[1:]):self.assertLessEqual(a[2]+a[3],b[2])
  self.assertEqual(new['bootfs0'][2]+new['bootfs0'][3],new['app1'][2])
  self.assertEqual(new['app1'][2]+new['app1'][3],new['bootfs1'][2])
  self.assertEqual(new['bootfs1'][2]+new['bootfs1'][3],new['otadata'][2])
 def test_incompatible_journals(self):
  for data,expanded,size in ((False,False,0x4f0000),(True,False,0x510000),(True,True,0x550000)):
   record=images.record(0,b'x'*256,b'y'*size,data,expanded)
   for other_data,other_expanded in ((False,False),(True,False),(True,True)):
    if (data,expanded)==(other_data,other_expanded):images.parse_record(record,other_data,other_expanded)
    else:
     with self.assertRaises(ValueError):images.parse_record(record,other_data,other_expanded)
if __name__=='__main__':unittest.main()
