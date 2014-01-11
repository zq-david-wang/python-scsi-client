#include <Python.h>

#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <scsi/sg.h>

#define DEFAULT_TIMEOUT 20
#define BLOCK_SIZE 4096

/*Sending SCSI command and receive response*/
static int _send_raw_command(
        const char* device_name,
        int timeout,
        unsigned char* cdb,
        int cdb_length,
        unsigned char* sense_buffer,
        int* p_sense_buffer_len,//In and Out
        const unsigned char* dataout_buffer,
        int dataout_len,
        unsigned char* datain_buffer,
        int* p_datain_len)
{
    int sg_fd = -1; // opened device file id
    int ret = 0; // general io return value

    sg_io_hdr_t ioctl_data; //sg_io_hdr

    int sense_buffer_len = *p_sense_buffer_len; //expected sense buffer length
    int datain_len = *p_datain_len; // expected datain buffer length

    int slen = 0; // returned sense buffer length
    int ret_datain_len = 0; // returned datain buffer length


    memset(&ioctl_data, 0, sizeof(ioctl_data));
    ioctl_data.interface_id = 'S';
    ioctl_data.dxfer_direction = SG_DXFER_NONE;
    ioctl_data.cmd_len = cdb_length;
    ioctl_data.mx_sb_len = sense_buffer_len;
    /* iovec_count -> 0 */
    /* dxfer_len -> 0, initialize */
    /* dxferp -> NULL */
    ioctl_data.cmdp = cdb;
    ioctl_data.sbp = sense_buffer;
    ioctl_data.timeout = timeout; /* in millisecs */
    /* ioctl_data.flags -> 0, default */

    if (dataout_len > 0) {
    	ioctl_data.dxfer_len = dataout_len;
    	ioctl_data.dxferp = dataout_buffer;
    	ioctl_data.dxfer_direction = SG_DXFER_TO_DEV;
    	datain_len = 0;
    }
    else if (datain_len > 0){
    	ioctl_data.dxfer_len = datain_len;
    	ioctl_data.dxferp = datain_buffer;
    	ioctl_data.dxfer_direction = SG_DXFER_FROM_DEV;
    }

    sg_fd = open(device_name, O_RDWR);
    if (sg_fd < 0){
    	ret = sg_fd;
        goto done;
    }
    ret = ioctl(sg_fd, SG_IO, &ioctl_data);
    if (ret < 0){
    	goto done;
    }
    ret = ioctl_data.info & SG_INFO_OK_MASK; // 0 for OK, 1 for Check Condition

    if (datain_len > 0){
    	ret_datain_len = ioctl_data.dxfer_len - ioctl_data.resid;
    }
    slen = ioctl_data.sb_len_wr;



done:
    *p_datain_len = ret_datain_len;
    *p_sense_buffer_len = slen;
    if (sg_fd >= 0)
        close(sg_fd);
    return ret;
}


/* Allocate aligned memory (heap) starting on page boundary */
static unsigned char *
my_memalign(int length, unsigned char ** wrkBuffp)
{
    unsigned char * wrkBuff;
    size_t psz = BLOCK_SIZE;     /* no bother here, pick likely figure */


    wrkBuff = (unsigned char*)calloc(length + psz, 1);
    if (NULL == wrkBuff) {
        if (wrkBuffp)
            *wrkBuffp = NULL;
        return NULL;
    } else if (wrkBuffp)
        *wrkBuffp = wrkBuff;
    return (unsigned char *)(((unsigned long)wrkBuff + psz - 1) &
                             (~(psz - 1)));

}


static PyObject* send_raw_command(PyObject* self, PyObject* args)
{
    const char* device_name;  //something like /dev/sg0
    int timeout; // in seconds for waiting for the response from the device
    const unsigned char* cdb; // byte stream for SCSI command
    int cdb_length;
    unsigned char* dataout_buffer = NULL; //IN
    unsigned char* dataout_buffer_handler = NULL; //
    int dataout_len = 0;
    unsigned char* datain_buffer = NULL;
    unsigned char* datain_buffer_handler = NULL;
    int datain_len = 0;
    unsigned char sense_buffer[32]={0}; //hope this is enough
    int sense_buffer_len = sizeof(sense_buffer);
    PyObject* output = NULL;
    int result = 0;

    if (!PyArg_ParseTuple(
                    args,
                    "sis#|is#",
                    &device_name,
                    &timeout,
                    &cdb,
                    &cdb_length,
                    &datain_len,
                    &dataout_buffer,
                    &dataout_len))
            return NULL;

    if (dataout_len > 0){
            unsigned char* temp_dataout_buffer = my_memalign(dataout_len, &dataout_buffer_handler);
            if (temp_dataout_buffer == NULL)
            goto bail;
            memcpy(temp_dataout_buffer,dataout_buffer,dataout_len);
            dataout_buffer = temp_dataout_buffer;
    }
    else
            dataout_buffer = "";

    if (datain_len > 0){
            datain_buffer = my_memalign(datain_len, &datain_buffer_handler);
            if (datain_buffer == NULL)
            goto bail;
    }
    else
            datain_buffer = "";

    result = _send_raw_command(device_name,
                    timeout,
                    cdb,
                    cdb_length,
                    sense_buffer,
                    &sense_buffer_len,
                    dataout_buffer,
                    dataout_len,
                    datain_buffer,
                    &datain_len);
    output =  Py_BuildValue("is#s#", result, sense_buffer, sense_buffer_len,datain_buffer,datain_len);

bail:
    if (dataout_buffer_handler != NULL) free(dataout_buffer_handler);
    if (datain_buffer_handler != NULL) free(datain_buffer_handler);
    return output;
}

static PyMethodDef SGMethods[] = {
    {"raw_scsi", send_raw_command, METH_VARARGS, "Send Raw SCSI Command."},
    {NULL, NULL, 0, NULL}
};



PyMODINIT_FUNC
init_py_scsi_raw(void)
{
    (void) Py_InitModule("_py_scsi_raw", SGMethods);

}
