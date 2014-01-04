/*
 * A python extension utility program originally written for the Linux OS SCSI subsystem.
 *
 * Based on Sg3 utility, http://sg.danny.cz/sg/sg3_utils.html
 *
 * Author: David Wang (00107082@163.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program can be used to send raw SCSI commands in python
 * through a Generic SCSI interface.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "sg_lib.h"
#include "sg_pt.h"

#include <Python.h>

#define DEFAULT_TIMEOUT 20
#define MIN_SCSI_CDBSZ 6
#define MAX_SCSI_CDBSZ 256
#define MAX_SCSI_DXLEN (64 * 1024)

/*Sending SCSI command and receive response*/
static int _send_raw_command(
        const char* device_name,
        int timeout,
        const unsigned char* cdb,
        int cdb_length,
        unsigned char* sense_buffer,
        int* p_sense_buffer_len,//In and Out
        const unsigned char* dataout_buffer,
        int dataout_len,
        unsigned char* datain_buffer,
        int* p_datain_len)
{
    int sg_fd = -1; // opened device file id
    int readonly = 0; //For a generic api, readonly is not enough.
    int do_verbose = 2;
    int ret = 0; // return value
    struct sg_pt_base *ptvp = NULL; // sg3 utility structure pointer
    int slen = 0; // returned sense buffer length
    int res_cat, status; // SCSI response code
    char b[128]; // buffer used for logging assistant
    int sense_buffer_len = *p_sense_buffer_len; //expected sense buffer length
    int datain_len = *p_datain_len; // expected datain buffer length
    int ret_datain_len = 0; // returned datain buffer length


    sg_fd = scsi_pt_open_device(device_name, readonly, do_verbose);
    if (sg_fd < 0) {
        fprintf(stderr, "%s: %s\n", device_name, safe_strerror(-sg_fd));
        ret = SG_LIB_FILE_ERROR;
        goto done;
    }

    ptvp = construct_scsi_pt_obj();
    if (ptvp == NULL) {
        fprintf(stderr, "out of memory\n");
        ret = SG_LIB_CAT_OTHER;
        goto done;
    }

    set_scsi_pt_cdb(ptvp, cdb, cdb_length);
    set_scsi_pt_sense(ptvp, sense_buffer, sense_buffer_len);
    if (dataout_len > 0){
        set_scsi_pt_data_out(ptvp, dataout_buffer, dataout_len);
    }
    if (datain_len > 0){
        set_scsi_pt_data_in(ptvp, datain_buffer, datain_len);
    }

    ret = do_scsi_pt(ptvp, sg_fd, timeout, do_verbose);

    if (ret > 0) {
        if (SCSI_PT_DO_BAD_PARAMS == ret) {
            fprintf(stderr, "do_scsi_pt: bad pass through setup\n");
            ret = SG_LIB_CAT_OTHER;
        } else if (SCSI_PT_DO_TIMEOUT == ret) {
            fprintf(stderr, "do_scsi_pt: timeout\n");
            ret = SG_LIB_CAT_TIMEOUT;
        } else
            ret = SG_LIB_CAT_OTHER;
        goto done;
    } else if (ret < 0) {
        fprintf(stderr, "do_scsi_pt: %s\n", safe_strerror(-ret));
        ret = SG_LIB_CAT_OTHER;
        goto done;
    }


    res_cat = get_scsi_pt_result_category(ptvp);
    switch (res_cat) {
    case SCSI_PT_RESULT_GOOD:
        ret = 0;
        break;
    case SCSI_PT_RESULT_SENSE:
        slen = get_scsi_pt_sense_len(ptvp);
        ret = sg_err_category_sense(sense_buffer, slen);
        break;
    case SCSI_PT_RESULT_TRANSPORT_ERR:
        get_scsi_pt_transport_err_str(ptvp, sizeof(b), b);
        fprintf(sg_warnings_strm, ">>> transport error: %s\n", b);
        ret = SG_LIB_CAT_OTHER;
        break;
    case SCSI_PT_RESULT_OS_ERR:
        get_scsi_pt_os_err_str(ptvp, sizeof(b), b);
        fprintf(sg_warnings_strm, ">>> os error: %s\n", b);
        ret = SG_LIB_CAT_OTHER;
        break;
    default:
        fprintf(sg_warnings_strm, ">>> unknown pass through result "
                "category (%d)\n", res_cat);
        ret = SG_LIB_CAT_OTHER;
        break;
    }

    status = get_scsi_pt_status_response(ptvp);
    fprintf(stderr, "SCSI Status: ");
    sg_print_scsi_status(status);
    fprintf(stderr, "\n\n");
    if ((SAM_STAT_CHECK_CONDITION == status)) {
        if (SCSI_PT_RESULT_SENSE != res_cat)
            slen = get_scsi_pt_sense_len(ptvp);
        if (0 == slen)
            fprintf(stderr, ">>> Strange: status is CHECK CONDITION but no "
                    "Sense Information\n");
        else {
            fprintf(stderr, "Sense Information:\n");
            sg_print_sense(NULL, sense_buffer, slen, (do_verbose > 0));
            fprintf(stderr, "\n");
        }
    }

    ret_datain_len = datain_len - get_scsi_pt_resid(ptvp);

done:
    *p_datain_len = ret_datain_len;
    *p_sense_buffer_len = slen;
    if (ptvp)
        destruct_scsi_pt_obj(ptvp);
    if (sg_fd >= 0)
        scsi_pt_close_device(sg_fd);
    return ret;
}


/* Allocate aligned memory (heap) starting on page boundary */
static unsigned char *
my_memalign(int length, unsigned char ** wrkBuffp)
{
    unsigned char * wrkBuff;
    size_t psz = 4096;     /* no bother here, pick likely figure */


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
init_py_sg_raw(void)
{
    (void) Py_InitModule("_py_sg_raw", SGMethods);

}
