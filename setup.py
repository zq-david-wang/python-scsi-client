from distutils.core import setup, Extension
 
module1 = Extension('_py_sg_raw', sources = ['_py_sg_raw.c'], libraries=['sgutils2'],
include_dirs = ['/usr/local/include/scsi'])
 
setup (name = 'py_sg_raw',
        version = '0.01',
        description = 'This is a simple scsi command package',
        ext_modules = [module1])