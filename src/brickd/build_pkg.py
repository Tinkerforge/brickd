# -*- coding: utf-8 -*-
"""
brickd (Brick Daemon)
Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
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

def check_output(*popenargs, **kwargs):
    if 'stdout' in kwargs:
        raise ValueError('stdout argument not allowed, it will be overridden.')
    process = subprocess.Popen(stdout=subprocess.PIPE, *popenargs, **kwargs)
    output, unused_err = process.communicate()
    retcode = process.poll()
    if retcode:
        cmd = kwargs.get("args")
        if cmd is None:
            cmd = popenargs[0]
        raise subprocess.CalledProcessError(retcode, cmd, output=output)
    return output

def build_macosx_pkg():
    os.system('make clean')
    os.system('CC=gcc make')

    version = check_output(['./brickd', '--version']).replace('\n', '')

    dist_dir = os.path.join(os.getcwd(), 'dist')
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)

    installer_dir = os.path.join(os.getcwd(), '..', 'build_data', 'macosx', 'installer')
    shutil.copytree(installer_dir, dist_dir)

    install_app_dir = os.path.join(os.getcwd(), 'dist', 'INSTALL.app')
    brickd_app_dir = os.path.join(os.getcwd(), 'dist', 'data', 'brickd.app')

    contents_dir = os.path.join(brickd_app_dir, 'Contents')
    macos_dir = os.path.join(contents_dir, 'MacOS')
    os.makedirs(macos_dir)
    shutil.copy('brickd', macos_dir)

    plist_name = os.path.join(contents_dir, 'Info.plist')
    lines = []
    for line in file(plist_name, 'rb').readlines():
        line = line.replace('<<BRICKD_VERSION>>', version)
        lines.append(line)
    file(plist_name, 'wb').writelines(lines)

    libusb_path = os.path.join(os.getcwd(), '..', 'build_data', 'macosx', 'libusb', 'libusb-1.0.dylib')

    shutil.copy(libusb_path, macos_dir)

    os.system('install_name_tool -id @executable_path/libusb-1.0.dylib {0}'.format(os.path.join(macos_dir, 'libusb-1.0.dylib')))
    os.system('install_name_tool -change @executable_path/../build_data/macosx/libusb/libusb-1.0.dylib @executable_path/libusb-1.0.dylib {0}'.format(os.path.join(macos_dir, 'brickd')))

    print 'signing...'
    codesign_command = 'codesign --force --verify --verbose --sign "${{CODESIGN_IDENTITY}}" {0}'

    os.system(codesign_command.format(install_app_dir))
    os.system(codesign_command.format(os.path.join(macos_dir, 'libusb-1.0.dylib')))
    os.system(codesign_command.format(brickd_app_dir))

    rc = os.system('./build_dmg.sh')

    if rc != 0:
        print "============================================="
        print "   Run ./build_dmg.sh to create .dmg file"
        print "============================================="

def build_windows_pkg():
    dist_dir = os.path.join(os.getcwd(), 'dist')
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)
    os.makedirs(dist_dir)

    os.system('compile.bat')

    version = check_output(['dist\\brickd.exe', '--version']).replace('\r\n', '')

    nsis_dir = os.path.join(os.getcwd(), 'dist', 'nsis')
    os.makedirs(nsis_dir)
    lines = []
    for line in file('../build_data/Windows/nsis/brickd_installer.nsi', 'rb').readlines():
        line = line.replace('<<BRICKD_DOT_VERSION>>', version)
        line = line.replace('<<BRICKD_UNDERSCORE_VERSION>>', version.replace('.', '_'))
        lines.append(line)
    file('dist/nsis/brickd_installer.nsi', 'wb').writelines(lines)

    build_data_dir = os.path.join(os.getcwd(), '..', 'build_data', 'windows')
    drivers_dir = os.path.join(build_data_dir, 'drivers')
    eventlog_dir = os.path.join(build_data_dir, 'eventlog')
    dist_dir = os.path.join(os.getcwd(), 'dist')
    dist_drivers_dir = os.path.join(dist_dir, 'drivers')

    shutil.copytree(drivers_dir, dist_drivers_dir)
    shutil.copy(os.path.join(build_data_dir, 'readme.txt'), dist_dir)
    shutil.copy(os.path.join(build_data_dir, 'brickd.ini'), dist_dir)
    shutil.copy(os.path.join(eventlog_dir, 'eventlog.exe'), dist_dir)

    os.system('"C:\\Program Files\\NSIS\\makensis.exe" dist\\nsis\\brickd_installer.nsi')

    dist_nsis_dir = os.path.join(dist_dir, 'nsis')
    shutil.move(os.path.join(dist_nsis_dir, 'brickd_windows_{0}.exe'.format(version.replace('.', '_'))), os.getcwd())


def build_linux_pkg():
    if os.geteuid() != 0:
        sys.stderr.write("build_pkg for Linux has to be started as root, exiting\n")
        sys.exit(1)

    architecture = check_output(['dpkg', '--print-architecture']).replace('\n', '')

    print 'Building version for ' + architecture

    os.system('make clean')

    if architecture == 'i386':
        os.system('CC=gcc WITH_LIBUDEV=yes WITH_LIBUDEV_DLOPEN=yes CFLAGS=-march=i386 make')
    else:
        os.system('CC=gcc WITH_LIBUDEV=yes WITH_LIBUDEV_DLOPEN=yes make')

    version = check_output(['./brickd', '--version']).replace('\n', '').replace(' ', '-')

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

    os.system('objcopy --strip-debug --strip-unneeded dist/usr/bin/brickd')

    os.system('cp ../../changelog dist/usr/share/doc/brickd/')

    os.system('gzip -9 dist/usr/share/doc/brickd/changelog')
    os.system('gzip -9 dist/usr/share/man/man8/brickd.8')
    os.system('gzip -9 dist/usr/share/man/man5/brickd.conf.5')

    os.system('cd dist; find usr -type f -exec md5sum {} \; >> DEBIAN/md5sums')

    os.system('chown -R root:root dist/usr')
    os.system('chown -R root:root dist/etc')

    os.system('find dist -type d -exec chmod 0755 {} \;')

    os.chmod('dist/DEBIAN/conffiles', 0644)
    os.chmod('dist/DEBIAN/md5sums', 0644)

    os.chmod('dist/DEBIAN/preinst', 0755)
    os.chmod('dist/DEBIAN/postinst', 0755)
    os.chmod('dist/DEBIAN/prerm', 0755)
    os.chmod('dist/DEBIAN/postrm', 0755)

    os.chmod('dist/etc/brickd.conf', 0644)
    os.chmod('dist/usr/share/man/man8/brickd.8.gz', 0644)
    os.chmod('dist/usr/share/man/man5/brickd.conf.5.gz', 0644)

    print 'Packaging...'
    os.system('dpkg -b dist brickd-' + version + '_' + architecture + '.deb')

    print 'Checking...'
    os.system('lintian --allow-root --pedantic brickd-' + version + '_' + architecture + '.deb')

    os.system('make clean')

    print 'Done'


# call python build_pkg.py to build the windows/linux/macosx package
if __name__ == "__main__":
    if sys.platform.startswith('linux'):
        build_linux_pkg()
    elif sys.platform == 'win32':
        build_windows_pkg()
    elif sys.platform == 'darwin':
        build_macosx_pkg()
    else:
        print "error: unsupported platform: " + sys.platform
