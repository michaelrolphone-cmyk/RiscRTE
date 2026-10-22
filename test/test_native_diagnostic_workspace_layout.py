import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("layout", Path(__file__).with_name("native_diagnostic_workspace_target_test.py"))
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)

class LayoutControls(unittest.TestCase):
    def test_measured_target_object(self):
        # Hosted Xtensa object from run39830408161:7352 bytes. Production
        # SerialStatSnapshot has its own bounded96-byte static assertion.
        layout.verify_workspace_layout((7352,8,2592,992,96,3672),7352,1,1,64)
    def test_capacity_128_keeps_bounded_delta(self):
        layout.verify_workspace_layout((9400,8,2592,992,96,5720),9400,1,1,128)
    def test_missing_stat_rejected(self):
        with self.assertRaises(AssertionError):
            layout.verify_workspace_layout((7352,8,2592,992,0,3672),7352,1,1,64)
    def test_storage_size_mismatch_rejected(self):
        with self.assertRaises(AssertionError):
            layout.verify_workspace_layout((7352,8,2592,992,96,3672),7256,1,1,64)
    def test_unbounded_extra_payload_rejected(self):
        with self.assertRaises(AssertionError):
            layout.verify_workspace_layout((7360,8,2600,992,96,3672),7360,1,1,64)
    def test_wrong_stat_size_rejected(self):
        with self.assertRaises(AssertionError):
            layout.verify_workspace_layout((7352,8,2584,992,104,3672),7352,1,1,64)

if __name__ == '__main__': unittest.main()
