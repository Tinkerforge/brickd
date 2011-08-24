# package builder for brickd
#
# Windows:
#     dependencies:
#       pythonxy (2.6)
#       Twisted
#       zope.interface: python.exe ez_setup.py install zope.interface-3.6.4-py2.6-win32.egg
#       py2exe
#       NSIS
#       Microsoft Visual C++ 2008 Redistributable x86 is only necessary under Win XP to run py2exe
#
#     run: "python build_pkg.py win" to build windows installer
#          installer is placed in "dist/nsis"

import sys  
from distutils.core import setup
import os
import py2exe
import glob
import shutil
 

 
DESCRIPTION = 'WindowsBrickd'
NAME = 'WindowsBrickd'

def build_windows_pkg():
    data_files = []
    
    def visitor(arg, dirname, names):
        for n in names:
            if os.path.isfile(os.path.join(dirname, n)):
                if arg[0] == 'y': #replace first folder name
                    data_files.append((os.path.join(dirname.replace(arg[1],"")) , [os.path.join(dirname, n)]))
                else:
                    data_files.append((os.path.join(dirname) , [os.path.join(dirname, n)]))
    
    os.path.walk("_build_data\Windows\\", visitor, ('y',"_build_data\Windows\\"))
        
    #print data_files
    
    #f = open('lala.txt','w')
    #for x in data_files:
    #    f.write(str(x))
    #f.close()
    
    #return

    setup(
          name = NAME,
          description = DESCRIPTION,
          version = '1.00.00',
          service = [{
                    'modules':["brickd_windows"],
                    'cmdline':'pywin32',
                    'cmdline_style':'pywin32', 
                    'dll_excludes': [ "mswsock.dll","powrprof.dll"]
                    }],
          zipfile = None,
          data_files = data_files,
          options = {
                     "py2exe":{"packages":"encodings",
                         "includes":"win32com,win32service,win32serviceutil,win32event",
                         "optimize": '2'
                        },
          },
    )
    
    # build nsis
    run = "\"" + os.path.join("C:\Program Files\NSIS\makensis.exe") + "\""
    data = " dist\\nsis\\brickd_installer_windows.nsi"
    print "run:", run
    print "data:", data
    os.system(run + data)
    
# call python build_pkg.py win to build the windows package
    
if __name__ == "__main__":
    if sys.argv[1] == "win":
        sys.argv[1] = "py2exe" # rewrite sys.argv[1] for setup(), want to call py2exe
        build_windows_pkg()
