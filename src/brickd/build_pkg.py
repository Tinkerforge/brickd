# -*- coding: utf-8 -*-  
"""
brickd (Brick Daemon)
Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
Copyright (C) 2011 Olaf Lüke <olaf@tinkerforge.com>
Copyright (C) 2011 Bastian Nordmeyer <bastian@tinkerforge.com>

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

import sys  
import os
import shutil
import subprocess
import re

def get_changelog_version(path):
    r = re.compile('^(\d+)\.(\d+)\.(\d+):')
    last = None
    for line in file(path, 'rb').readlines():
        m = r.match(line)

        if m is not None:
            last = (m.group(1), m.group(2), m.group(3))

    return last

def build_macosx_pkg():
    os.system('make clean')
    os.system('make')

    version = subprocess.check_output(['./brickd', '--version']).replace('\n', '')

    dist_dir = os.path.join(os.getcwd(), 'dist')
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)

    build_data_dir = os.path.join(os.getcwd(), '..', 'build_data', 'macos')
    shutil.copytree(build_data_dir, dist_dir)

    macos_dir = os.path.join(os.getcwd(), 'dist', 'data', 'brickd.app', 'Contents', 'MacOS')
    shutil.copy('brickd', macos_dir)

    plist_name = os.path.join(os.getcwd(), 'dist', 'data', 'brickd.app', 'Contents', 'Info.plist')
    lines = []
    for line in file(plist_name, 'rb').readlines():
        line = line.replace('<<BRICKD_VERSION>>', version)
        lines.append(line)
    file(plist_name, 'wb').writelines(lines)

    os.system('install_name_tool -change /opt/local/lib/libusb-1.0.0.dylib @executable_path/libusb-1.0.0.dylib dist/data/brickd.app/Contents/MacOS/brickd')

 
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
    if os.geteuid() != 0:
        sys.stderr.write("build_pkg for Linux has to be started as root, exiting\n")
        sys.exit(1)

    version = '.'.join(get_changelog_version('../../changelog'))
    architecture = subprocess.check_output(['dpkg', '--print-architecture']).replace('\n', '')
    
    print 'Building version ' + version + ' for ' + architecture

    os.system('make clean')
    os.system('make')

    dist_dir = os.path.join(os.getcwd(), 'dist')
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)

    build_data_dir = os.path.join(os.getcwd(), '..', 'build_data', 'linux', 'brickd')
    shutil.copytree(build_data_dir, dist_dir)

    bin_dir = os.path.join(os.getcwd(), 'dist', 'usr', 'bin')
    os.makedirs(bin_dir)
    shutil.copy('brickd', bin_dir)
    os.system('make clean')
    os.system('make clean-depend')

    lines = []
    for line in file(os.path.join(os.getcwd(), 'dist', 'DEBIAN', 'control'), 'rb').readlines():
        line = line.replace('<<BRICKD_VERSION>>', version)
        line = line.replace('<<BRICKD_ARCHITECTURE>>', architecture)
        lines.append(line)
    file(os.path.join(os.getcwd(), 'dist', 'DEBIAN', 'control'), 'wb').writelines(lines)

    os.system('chown -R root:root dist/usr')
    os.system('chown -R root:root dist/etc')

    os.system('chmod 0644 dist/DEBIAN/md5sums')
    os.system('chmod 0755 dist/DEBIAN/preinst')
    os.system('chmod 0755 dist/DEBIAN/postinst')
    os.system('chmod 0755 dist/DEBIAN/prerm')

    os.system('dpkg -b dist brickd-' + version + '_' + architecture + '.deb')


# call python build_pkg.py windows/linux/macosx to build the windows/linux/macosx package
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "error: specify platform"
    elif sys.argv[1] == "windows":
        build_windows_pkg()
    elif sys.argv[1] == "linux":
        build_linux_pkg()
    elif sys.argv[1] == "macosx":
        build_macosx_pkg()
    else:
        print "error: unknown platform: " + sys.argv[1]
