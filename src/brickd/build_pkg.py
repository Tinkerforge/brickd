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
import glob

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
    dist_dir = os.path.join(os.getcwd(), 'dist')
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)
    os.makedirs(dist_dir)

    os.system('build_exe.bat')

    version = subprocess.check_output(['dist\\brickd.exe', '--version']).replace('\r\n', '')

    nsis_dir = os.path.join(os.getcwd(), 'dist', 'nsis')
    os.makedirs(nsis_dir)
    lines = []
    for line in file('../build_data/Windows/nsis/brickd_installer.nsi', 'rb').readlines():
        line = line.replace('<<BRICKD_DOT_VERSION>>', version)
        line = line.replace('<<BRICKD_UNDERSCORE_VERSION>>', version.replace('.', '_'))
        lines.append(line)
    file('dist/nsis/brickd_installer.nsi', 'wb').writelines(lines)

    drivers_dir = os.path.join(os.getcwd(), '..', 'build_data', 'Windows', 'drivers')
    dist_drivers_dir = os.path.join(os.getcwd(), 'dist', 'drivers')
    shutil.copytree(drivers_dir, dist_drivers_dir)

    os.system('"C:\\Program Files\\NSIS\\makensis.exe" dist\\nsis\\brickd_installer.nsi')


def build_linux_pkg():
    if os.geteuid() != 0:
        sys.stderr.write("build_pkg for Linux has to be started as root, exiting\n")
        sys.exit(1)

    architecture = subprocess.check_output(['dpkg', '--print-architecture']).replace('\n', '')

    print 'Building version for ' + architecture

    os.system('make clean')
    os.system('make')

    version = subprocess.check_output(['./brickd', '--version']).replace('\n', '').replace(' ', '-')

    dist_dir = os.path.join(os.getcwd(), 'dist')
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)

    build_data_dir = os.path.join(os.getcwd(), '..', 'build_data', 'linux')
    shutil.copytree(build_data_dir, dist_dir)

    bin_dir = os.path.join(os.getcwd(), 'dist', 'usr', 'bin')
    os.makedirs(bin_dir)
    shutil.copy('brickd', bin_dir)

    control_name = os.path.join(os.getcwd(), 'dist', 'DEBIAN', 'control')
    lines = []
    for line in file(control_name, 'rb').readlines():
        line = line.replace('<<BRICKD_VERSION>>', version)
        line = line.replace('<<BRICKD_ARCHITECTURE>>', architecture)
        lines.append(line)
    file(control_name, 'wb').writelines(lines)

    os.system('chown -R root:root dist/usr')
    os.system('chown -R root:root dist/etc')

    os.system('chmod 0644 dist/DEBIAN/md5sums')
    os.system('chmod 0755 dist/DEBIAN/preinst')
    os.system('chmod 0755 dist/DEBIAN/postinst')
    os.system('chmod 0755 dist/DEBIAN/prerm')

    os.system('dpkg -b dist brickd-' + version + '_' + architecture + '.deb')

    os.system('make clean')
    os.system('make clean-depend')


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
