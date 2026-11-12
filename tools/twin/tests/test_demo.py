from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from demo import Probe


class YamcsValueTests(unittest.TestCase):
    def test_actual_xtce_boolean_enum_representation(self):
        # Observed from the real generated MDB: F Prime true uses enum 255.
        actual = {'engValue': {'type': 'ENUMERATED', 'stringValue': 'True', 'sint64Value': '255'}}
        self.assertIs(Probe.value(actual), True)
        self.assertIs(Probe.value({'engValue': {'stringValue': 'False'}}), False)
        self.assertEqual(Probe.value({'engValue': {'stringValue': 'CACHE_READY'}}), 'CACHE_READY')
