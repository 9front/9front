from _testcapi import test_structmembersType, \
    CHAR_MAX, CHAR_MIN, UCHAR_MAX, \
    SHRT_MAX, SHRT_MIN, USHRT_MAX, \
    INT_MAX, INT_MIN, UINT_MAX, \
    LONG_MAX, LONG_MIN, ULONG_MAX

import warnings, exceptions, unittest, test.test_warnings
from test import test_support

ts=test_structmembersType(1,2,3,4,5,6,7,8,9.99999,10.1010101010)

class ReadWriteTests(unittest.TestCase):
    def test_types(self):
        ts.T_BYTE=CHAR_MAX
        self.assertEquals(ts.T_BYTE, CHAR_MAX)
        ts.T_BYTE=CHAR_MIN
        self.assertEquals(ts.T_BYTE, CHAR_MIN)
        ts.T_UBYTE=UCHAR_MAX
        self.assertEquals(ts.T_UBYTE, UCHAR_MAX)

        ts.T_SHORT=SHRT_MAX
        self.assertEquals(ts.T_SHORT, SHRT_MAX)
        ts.T_SHORT=SHRT_MIN
        self.assertEquals(ts.T_SHORT, SHRT_MIN)
        ts.T_USHORT=USHRT_MAX
        self.assertEquals(ts.T_USHORT, USHRT_MAX)

        ts.T_INT=INT_MAX
        self.assertEquals(ts.T_INT, INT_MAX)
        ts.T_INT=INT_MIN
        self.assertEquals(ts.T_INT, INT_MIN)
        ts.T_UINT=UINT_MAX
        self.assertEquals(ts.T_UINT, UINT_MAX)

        ts.T_LONG=LONG_MAX
        self.assertEquals(ts.T_LONG, LONG_MAX)
        ts.T_LONG=LONG_MIN
        self.assertEquals(ts.T_LONG, LONG_MIN)
        ts.T_ULONG=ULONG_MAX
        self.assertEquals(ts.T_ULONG, ULONG_MAX)

def test_main(verbose=None):
    test_support.run_unittest(
        ReadWriteTests
        )

if __name__ == "__main__":
    test_main(verbose=True)
