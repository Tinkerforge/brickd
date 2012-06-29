# -*- coding: utf-8 -*-  
"""
brickd (Brick Daemon)
Copyright (C) 2011 Olaf Lüke <olaf@tinkerforge.com>
              2011 Bastian Nordmeyer <bastian@tinkerforge.com>

brickd_pkg.py: Package builder for Brick Daemon

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License 
as published by the Free Software Foundation; either version 2 
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public
License along with this program; if not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.
"""

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

import config
import sys  
import distutils
from distutils.core import setup
import os
import glob
import shutil
import subprocess
 

def build_macos_pkg():
    from setuptools import setup, find_packages

    PWD = os.path.dirname(os.path.realpath(__file__))
    RES_PATH = os.path.join(PWD, 'dist', '%s.app' % 'brickd', 'Contents', 'Resources')
    data_files = [
        ("../build_data/macos/", glob.glob(os.path.join(PWD, "../build_data/macos/", "*.nib"))),
    ]
    packages = find_packages()

    plist = dict(
        CFBundleName = 'brickd',
        CFBundleShortVersionString = config.BRICKD_VERSION,
        CFBundleGetInfoString = ' '.join(['brickd', config.BRICKD_VERSION]),
        CFBundleExecutable = 'brickd_macosx',
        CFBundleIdentifier = 'com.tinkerforge.brickd',
        # hide dock icon
    #    LSUIElement = True,
    )

    additional_data_files = []
    def visitor(arg, dirname, names):
        for n in names:

            if os.path.isfile(os.path.join(dirname, n)):
                if arg[0] == 'y': #replace first folder name
                    additional_data_files.append((os.path.join(dirname.replace(arg[1],"")) , [os.path.join(dirname, n)]))
                else: # keep full path
                    additional_data_files.append((os.path.join(dirname) , [os.path.join(dirname, n)]))
    
    os.path.walk(os.path.normcase("../build_data/macos/"), visitor, ('y',os.path.normcase("../build_data/macos/")))
    



    def delete_old():
        BUILD_PATH = os.path.join(PWD, "build")
        DIST_PATH = os.path.join(PWD, "dist")
        if os.path.exists(BUILD_PATH):
            shutil.rmtree(BUILD_PATH)
        if os.path.exists(DIST_PATH):
            shutil.rmtree(DIST_PATH)

    def create_app():
        apps = [
            {
                "script" : 'brickd_macosx.py',
                "plist" : plist,
            }
        ]

        OPTIONS = {'argv_emulation': True, 'site_packages': True, "includes":[],}

        data = data_files + additional_data_files

        setup(
            name = 'brickd_macosx',
            version = config.BRICKD_VERSION,
            description = 'Brick Daemon Software',
            author = 'Tinkerforge',
            author_email = 'info@tinkerforge.com',
            platforms = ["Mac OSX"],
            license = "GPL V2",
            url = "http://www.tinkerforge.com",
            scripts = ['brickd_macosx.py'],

            app = apps,
            options = {'py2app': OPTIONS},
            # setup_requires = ['py2app'],
            data_files = data,
            packages = packages,
        )

        print data

    _RUN_IN_TERM_PATCH = """import os
import sys

os.environ['RESOURCEPATH'] = os.path.dirname(os.path.realpath(__file__))

"""

    def run_in_term_patch():
        BOOT_FILE_PATH = os.path.join(RES_PATH, "__boot__.py")
        with open(BOOT_FILE_PATH) as f:
            old = f.read()

        new = _RUN_IN_TERM_PATCH + old

        with open(BOOT_FILE_PATH, 'w') as f:
            f.write(new)

    def data_files_patch():
        for item in data_files:
            if isinstance(item, tuple):
                folder_name = item[0]
            else:
                folder_name = item

            src = os.path.join(PWD, folder_name)
            dst = os.path.join(RES_PATH, folder_name)
            if not os.path.exists(dst):
                shutil.copytree(src, dst)

    ACTION_CREATE = len(sys.argv) == 3 and sys.argv[-1] == "build"

    def create_dmg():
        if os.path.exists("macos_build"):
            shutil.rmtree("macos_build")
        os.mkdir("macos_build")
        distutils.dir_util.copy_tree("../build_data/macos/", "macos_build")
        distutils.dir_util.copy_tree("dist", "macos_build/data")
        distutils.dir_util.copy_tree("../build_data/macos/data/libusb/", "macos_build/data/brickd.app/Contents/Resources/")

#        subprocess.call('./_build_dmg.sh', shell=True)

    if ACTION_CREATE:
        delete_old()
        create_app()
        run_in_term_patch()
        data_files_patch()
        create_dmg()
    else:
        create_app()
        print "Usage: python setup.py py2app build"

 
DESCRIPTION = 'Brickd for Windows'
NAME = 'Brickd'

def build_windows_pkg():
    PWD = os.path.dirname(os.path.realpath(__file__))
    BUILD_PATH = os.path.join(PWD, "build")
    DIST_PATH = os.path.join(PWD, "dist")
    if os.path.exists(BUILD_PATH):
        shutil.rmtree(BUILD_PATH)
    if os.path.exists(DIST_PATH):
        shutil.rmtree(DIST_PATH)

    import py2exe
    data_files = []

    lines = []
    for line in file('../build_data/Windows/nsis/brickd_installer_windows.nsi.template', 'rb').readlines():
        line = line.replace('<<BRICKD_DOT_VERSION>>', config.BRICKD_VERSION)
        line = line.replace('<<BRICKD_UNDERSCORE_VERSION>>', config.BRICKD_VERSION.replace('.', '_'))
        lines.append(line)
    file('../build_data/Windows/nsis/brickd_installer_windows.nsi', 'wb').writelines(lines)

    def visitor(arg, dirname, names):
        for n in names:
            if os.path.isfile(os.path.join(dirname, n)):
                if arg[0] == 'y': # replace first folder name
                    data_files.append((os.path.join(dirname.replace(arg[1],"")) , [os.path.join(dirname, n)]))
                else:
                    data_files.append((os.path.join(dirname) , [os.path.join(dirname, n)]))
    
    os.path.walk("..\\build_data\Windows\\", visitor, ('y',"..\\build_data\Windows\\"))

    setup(name = NAME,
          description = DESCRIPTION,
          version = config.BRICKD_VERSION,
          service = [{
                    'modules': ["brickd_windows"],
                    'cmdline_style': 'custom',
                    'dll_excludes': ["mswsock.dll", "powrprof.dll"]
                    }],
          zipfile = None,
          data_files = data_files,
          options = {
                     "py2exe" : {
                     "packages" : "encodings",
                     "includes" : ["win32com",
                                   "win32service",
                                   "win32serviceutil",
                                   "win32event"],
                     "excludes" : ["distutils",
                                   "email",
                                   "doctest",
                                   "difflib",
                                   "pdb",
                                   "unittest",
                                   "ctypes.macholib"],
                     "optimize" : '2'},
          },
    )
    
    # build nsis
    run = "\"" + os.path.join("C:\Program Files\NSIS\makensis.exe") + "\""
    data = " dist\\nsis\\brickd_installer_windows.nsi"
    print "run:", run
    print "data:", data
    os.system(run + data)
    
def build_linux_pkg():
    src_path = os.getcwd()
    build_dir = 'build_data/linux/brickd/usr/share/brickd'
    dest_path = os.path.join(os.path.split(src_path)[0], build_dir)
    if os.path.isdir(dest_path):
        shutil.rmtree(dest_path)

    shutil.copytree(src_path, dest_path)
    
    build_data_path = os.path.join(os.path.split(src_path)[0], 'build_data/linux')
    os.chdir(build_data_path)

    STEXT = 'Version:'
    RTEXT = 'Version: {0}\n'.format(config.BRICKD_VERSION)

    f = open('brickd/DEBIAN/control', 'r')
    lines = f.readlines()
    f.close()

    f = open('brickd/DEBIAN/control', 'w')
    for line in lines:
        if not line.find(STEXT) == -1:
            line = RTEXT
        f.write(line)
    f.close()

    os.system('chown -R root brickd/usr')
    os.system('chgrp -R root brickd/usr')
    os.system('chown -R root brickd/etc')
    os.system('chgrp -R root brickd/etc')

    os.system('chmod 0644 brickd/DEBIAN/md5sums')
    os.system('chmod 0755 brickd/DEBIAN/postinst')
    os.system('chmod 0755 brickd/DEBIAN/postrm')

    files = ['control', 'md5sums', 'postinst', 'postrm']

    for f in files:
        os.chown('brickd/DEBIAN/{0}'.format(f), 0, 0)

    os.system('dpkg -b brickd/ brickd-' + config.BRICKD_VERSION + '_all.deb')

    for f in files:
        os.chown('brickd/DEBIAN/{0}'.format(f), 1000, 1000)


# call python build_pkg.py windows/linux to build the windows/linux package
    
if __name__ == "__main__":
    if sys.argv[1] == "windows":
        sys.argv[1] = "py2exe" # rewrite sys.argv[1] for setup(), want to call py2exe
        build_windows_pkg()
    elif sys.argv[1] == "linux":
        build_linux_pkg()
    elif sys.argv[1] == "macos":
        sys.argv[1] = "py2app" # rewrite sys.argv[1] for setup(), want to call py2app
        sys.argv.append("build")
        build_macos_pkg()
