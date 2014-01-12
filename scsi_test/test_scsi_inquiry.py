import unittest
from ioctl_scsi.base import *

class TestSCSIInquiryCommand(unittest.TestCase):
    def setUp(self):
        pass
    def tearDown(self):
        pass
    
    def test_standard_inquiry(self):
        c = InquirySCSICommand(evpd = 0,
                               pagecode = 0, 
                               allocation_length = 10)
        c.execute()
        datain = c.decode_data_in()
        print datain