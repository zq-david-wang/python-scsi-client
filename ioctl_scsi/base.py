import logging

from ioctl_scsi import exceptions
from _py_scsi_raw import raw_scsi

LOG = logging.getLogger(__name__)

class BaseSCSICommand(object):
    DataInClass = None
    
    def __init__(self, cdb, data_out = "", data_in_len = 0):
        self.cdb = cdb
        self.data_out = data_out
        self.data_in_len = data_in_len
        self.sense_data = None
        self.date_in = None
        
            
    def __str__(self):
        return "SCSICommand: "+self.cdb
    
    def execute(self, device='/dev/sg0', timeout = 10):
        r = raw_scsi(device, timeout, self.cdb, self.data_in_len, self.data_out, )
        if r:
            self.sense_data = r[1]
            self.date_in = r[2]
            return r[0]
    
    def decode_sense_data(self):
        pass
    
    def decode_data_in(self):
        if self.date_in != None:
            return self.DataInClass(self.date_in, self.data_in_len).decode()

class SCSIDataInObject(object):
    def __init__(self, dateinbuffer, buffermaxlen):
        self.datainbuffer = dateinbuffer
        self.buffermaxlen = buffermaxlen
    
    def decode(self):
        pass
    
class StandInquiryDataInObject(SCSIDataInObject):    
    def decode(self):
        self.version = 0
        if self.buffermaxlen > 4:
            self.version = ord(self.datainbuffer[2])
            self.add_length = ord(self.datainbuffer[4])
        return self
    
    def __str__(self):
        return "Version:%d\n Additional length:%d"%(self.version,self.add_length)

class InquirySCSICommand(BaseSCSICommand):
    DataInClass = StandInquiryDataInObject
    
    def __init__(self, evpd,  pagecode, allocation_length, reserved = 0, obsolete = 0, control = 0):
        cdb = bytearray(6)
        cdb[0] = 0x12
        cdb[1] = ((reserved & 0b111111) << 2) | ((obsolete & 0b1) << 1) | (evpd & 0b01)
        cdb[2] = pagecode
        cdb[3] = (allocation_length & 0xff00) >> 8
        cdb[4] = allocation_length & 0xff
        cdb[5] = control
        data_in_len = allocation_length
        super(InquirySCSICommand, self).__init__(str(cdb), "", data_in_len)
        
        