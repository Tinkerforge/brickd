# -*- coding: utf-8 -*-
"""
brickd (Brick Daemon)
Copyright (C) 2012-2016 Matthias Bolte <matthias@tinkerforge.com>
Copyright (C) 2011 Olaf Lüke <olaf@tinkerforge.com>
Copyright (C) 2011 Bastian Nordmeyer <bastian@tinkerforge.com>

build_pkg.py: Package builder for Brick Daemon

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
import plistlib
import time
import re

def system(command):
    if os.system(command) != 0:
        sys.exit(1)

def check_output(*args, **kwargs):
    if 'stdout' in kwargs:
        raise ValueError('stdout argument not allowed, it will be overridden')

    process = subprocess.Popen(stdout=subprocess.PIPE, *args, **kwargs)
    output, error = process.communicate()
    exit_code = process.poll()

    if exit_code != 0:
        command = kwargs.get('args')

        if command == None:
            command = args[0]

        raise subprocess.CalledProcessError(exit_code, command, output=output)

    return output.decode('utf-8')

def specialize_template(template_filename, destination_filename, replacements):
    lines = []
    replaced = set()

    # intentionally use mode=rb and decode/encode to be able to enforce UTF-8
    # in an ASCII environment even with Python2 where the open function doesn't
    # have an encoding parameter
    with open(template_filename, 'rb') as f:
        for line in f.readlines():
            line = line.decode('utf-8')

            for key in replacements:
                replaced_line = line.replace(key, replacements[key])

                if replaced_line != line:
                    replaced.add(key)

                line = replaced_line

            lines.append(line.encode('utf-8'))

    if replaced != set(replacements.keys()):
        raise Exception('Not all replacements for {0} have been applied'.format(template_filename))

    with open(destination_filename, 'wb') as f:
        f.writelines(lines)

def build_macos_pkg():
    if (sys.hexversion & 0xFF000000) != 0x03000000:
        print('Python 3.x required')
        sys.exit(1)

    print('building brickd disk image')
    root_path = os.getcwd()

    print('removing old build directory')
    dist_path = os.path.join(root_path, 'dist')

    if os.path.exists(dist_path):
        shutil.rmtree(dist_path)

    print('compiling')
    system('cd brickd; make clean')
    system('cd brickd; env CC=gcc make')

    print('copying installer root')
    installer_root_path = os.path.join(root_path, 'build_data', 'macos', 'installer', 'root')
    dist_root_path = os.path.join(dist_path, 'root')
    shutil.copytree(installer_root_path, dist_root_path)

    print('copying brickd binary')
    app_path = os.path.join(dist_root_path, 'usr', 'local', 'libexec', 'brickd.app')
    contents_path = os.path.join(app_path, 'Contents')
    macos_path = os.path.join(contents_path, 'MacOS')
    os.makedirs(macos_path)
    shutil.copy('brickd/brickd', macos_path)

    print('creating Info.plist from template')
    version = check_output(['./brickd/brickd', '--version']).replace('\n', '')
    plist_path = os.path.join(contents_path, 'Info.plist')
    specialize_template(plist_path, plist_path, {'<<VERSION>>': version})

    print('copying and patching libusb')
    libusb_path = os.path.join(root_path, 'build_data', 'macos', 'libusb', 'libusb-1.0-brickd.dylib')
    shutil.copy(libusb_path, macos_path)
    system('install_name_tool -id @executable_path/libusb-1.0-brickd.dylib {0}'.format(os.path.join(macos_path, 'libusb-1.0-brickd.dylib')))
    system('install_name_tool -change @executable_path/../build_data/macos/libusb/libusb-1.0-brickd.dylib @executable_path/libusb-1.0-brickd.dylib {0}'.format(os.path.join(macos_path, 'brickd')))

    print('signing libusb and brickd binaries')
    system('security unlock-keychain /Users/$USER/Library/Keychains/login.keychain')
    # NOTE: codesign_application_identity contains "Developer ID Application: ..."
    codesign_command = 'codesign --force --verify --verbose -o runtime --sign "`cat codesign_application_identity`" {0}'
    system(codesign_command.format(os.path.join(macos_path, 'libusb-1.0-brickd.dylib')))
    system(codesign_command.format(app_path))

    print('notarize app')
    zip_path = os.path.join(dist_path, 'brickd.app.zip')
    system('ditto -c -k --keepParent "{0}" "{1}"'.format(app_path, zip_path))
    output = subprocess.check_output(['xcrun', 'altool', '--notarize-app', '--primary-bundle-id', 'com.tinkerforge.brickd', '--username', 'olaf@tinkerforge.com', '--password', '@keychain:Notarization', '--output-format', 'xml', '--file', zip_path])
    request_uuid = plistlib.loads(output)['notarization-upload']['RequestUUID']

    print('notarize app request uuid', request_uuid)
    notarization_info = None

    while True:
        output = subprocess.check_output(['xcrun', 'altool', '--notarization-info', request_uuid, '--username', 'olaf@tinkerforge.com', '--password', '@keychain:Notarization', '--output-format', 'xml'])
        notarization_info = plistlib.loads(output)['notarization-info']

        if notarization_info['Status'] != 'in progress':
            break

        print('waiting for app notarization to complete ...')
        time.sleep(10)

    print('notarization app info', notarization_info)

    if notarization_info['Status'] != 'success':
        print('error: notarization app failed')
        sys.exit(1)

    print('staple notarization ticket to app')
    system('xcrun stapler staple "{0}"'.format(app_path))

    print('building pkg')
    scripts_path = os.path.join(root_path, 'build_data', 'macos', 'installer', 'scripts')
    component_path = os.path.join(root_path, 'build_data', 'macos', 'installer', 'component.plist')
    # NOTE: codesign_installer_identity contains "Developer ID Installer: ..."
    system('pkgbuild --sign "`cat codesign_installer_identity`" --root dist/root --identifier com.tinkerforge.brickd --version {0} --scripts {1} --install-location / --component-plist {2} dist/brickd.pkg'.format(version, scripts_path, component_path))
    distribution_path = os.path.join(root_path, 'build_data', 'macos', 'installer', 'distribution.xml')
    shutil.copy(distribution_path, dist_path)
    distribution_path = os.path.join(dist_path, 'distribution.xml')
    specialize_template(distribution_path, distribution_path, {'<<VERSION>>': version})
    os.makedirs('dist/dmg')
    pkg_path = 'dist/dmg/Brickd-{0}.pkg'.format(version)
    system('productbuild --sign "`cat codesign_installer_identity`" --distribution {0} --package-path {1} --version {2} {3}'.format(distribution_path, dist_path, version, pkg_path))

    print('notarize pkg')
    output = subprocess.check_output(['xcrun', 'altool', '--notarize-app', '--primary-bundle-id', 'com.tinkerforge.brickd.pkg', '--username', 'olaf@tinkerforge.com', '--password', '@keychain:Notarization', '--output-format', 'xml', '--file', pkg_path])
    request_uuid = plistlib.loads(output)['notarization-upload']['RequestUUID']

    print('notarize pkg request uuid', request_uuid)
    notarization_info = None

    while True:
        output = subprocess.check_output(['xcrun', 'altool', '--notarization-info', request_uuid, '--username', 'olaf@tinkerforge.com', '--password', '@keychain:Notarization', '--output-format', 'xml'])
        notarization_info = plistlib.loads(output)['notarization-info']

        if notarization_info['Status'] != 'in progress':
            break

        print('waiting for pkg notarization to complete ...')
        time.sleep(10)

    print('notarization pkg info', notarization_info)

    if notarization_info['Status'] != 'success':
        print('error: notarization pkg failed')
        sys.exit(1)

    print('staple notarization ticket to pkg')
    system('xcrun stapler staple "{0}"'.format(pkg_path))

    print('building disk image')
    dmg_name = 'brickd_macos_{0}.dmg'.format(version.replace('.', '_'))

    if os.path.exists(dmg_name):
        os.remove(dmg_name)

    system('hdiutil create -fs HFS+ -volname "Brickd-{0}" -srcfolder dist/dmg {1}'.format(version, dmg_name))

def build_windows_pkg():
    print('building brickd NSIS installer')
    root_path = os.getcwd()

    print('removing old build directory')
    dist_path = os.path.join(root_path, 'dist')

    if os.path.exists(dist_path):
        shutil.rmtree(dist_path)
        time.sleep(1) # FIXME: without this sleep the following makedirs call fails with an access-denied error

    os.makedirs(dist_path)

    print('compiling brickd.exe')
    system('cd brickd && compile.bat')

    if '--no-sign' not in sys.argv:
        print('signing brickd.exe')
        system('signtool.exe sign /v /tr http://rfc3161timestamp.globalsign.com/advanced /td sha256 /n "Tinkerforge GmbH" dist\\brickd.exe')

        print('verifying brickd.exe signature')
        system('signtool.exe verify /v /pa dist\\brickd.exe')

    print('compiling logviewer.exe')
    system('cd build_data\\windows\\logviewer && compile.bat')

    if '--no-sign' not in sys.argv:
        print('signing logviewer.exe')
        system('signtool.exe sign /v /tr http://rfc3161timestamp.globalsign.com/advanced /td sha256 /n "Tinkerforge GmbH" build_data\\windows\\logviewer\\logviewer.exe')

        print('verifying logviewer.exe signature')
        system('signtool.exe verify /v /pa build_data\\windows\\logviewer\\logviewer.exe')

    print('creating NSIS script from template')
    version = check_output(['dist\\brickd.exe', '--version']).replace('\r\n', '')
    build_data_path = os.path.join(root_path, 'build_data', 'windows')
    installer_template_path = os.path.join(build_data_path, 'installer', 'brickd_installer.nsi.template')
    installer_path = os.path.join(dist_path, 'installer', 'brickd_installer.nsi')
    os.makedirs(os.path.join(dist_path, 'installer'))
    specialize_template(installer_template_path, installer_path,
                        {'<<BRICKD_VERSION>>': version,
                         '<<BRICKD_UNDERSCORE_VERSION>>': version.replace('.', '_')})

    print('copying build data')
    drivers_path = os.path.join(build_data_path, 'drivers')
    dist_drivers_path = os.path.join(dist_path, 'drivers')
    shutil.copytree(drivers_path, dist_drivers_path)
    shutil.copy(os.path.join(build_data_path, 'readme.txt'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'brickd-default.ini'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'logviewer', 'logviewer.exe'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'logviewer', 'logviewer.pdb'), dist_path)

    print('building NSIS installer')
    system('"C:\\Program Files (x86)\\NSIS\\makensis.exe" dist\\installer\\brickd_installer.nsi')
    installer = 'brickd_windows_{0}.exe'.format(version.replace('.', '_'))

    if os.path.exists(installer):
        os.unlink(installer)

    shutil.move(os.path.join(dist_path, 'installer', installer), root_path)

    if '--no-sign' not in sys.argv:
        print('signing NSIS installer')
        system('signtool.exe sign /v /tr http://rfc3161timestamp.globalsign.com/advanced /td sha256 /n "Tinkerforge GmbH" ' + installer)

        print('verifying NSIS installer signature')
        system('signtool.exe verify /v /pa ' + installer)

def build_linux_pkg():
    if (sys.hexversion & 0xFF000000) != 0x03000000:
        print('Python 3.x required')
        sys.exit(1)

    print('building brickd Debian package')
    root_path = os.getcwd()

    print('removing old build directory')
    dist_path = os.path.join(root_path, 'dist')

    if os.path.exists(dist_path):
        shutil.rmtree(dist_path)

    architecture = check_output(['dpkg', '--print-architecture']).replace('\n', '')

    print('compiling for ' + architecture)
    system('cd brickd; make clean')

    config = [
        'CC=gcc',
        'WITH_LIBUDEV=yes',
        'WITH_LIBUDEV_DLOPEN=yes',
        'WITH_PM_UTILS=yes',
        'WITH_UNKNOWN_LIBUSB_API_VERSION=yes'
    ]

    if architecture == 'i386':
        config.append('CFLAGS=-march=i386')

    system('cd brickd; env {0} make'.format(' '.join(config)))

    glibc_version = (0, 0, 0)

    for line in subprocess.check_output(['objdump', '-T', 'brickd/brickd']).decode('utf-8').split('\n'):
        m = re.search(r'GLIBC_([0-9\.]+)', line)

        if m == None:
            continue

        version = tuple(int(x) for x in m.group(1).split('.'))

        if version > glibc_version:
            glibc_version = version

    glibc_version = glibc_version + (0, 0)

    if glibc_version > (2, 9, 0):
        if input('\033[33mwarning: brickd depends on glibc {0}.{1}.{2} > 2.9.0. continue anyway?\033[0m [y/N] '.format(*glibc_version)).strip() not in ['y', 'Y']:
            print('aborted')
            sys.exit(1)

    print('copying build data')
    installer_path = os.path.join(root_path, 'build_data', 'linux', 'installer')
    shutil.copytree(installer_path, dist_path)

    print('copying brickd binary')
    bin_path = os.path.join(dist_path, 'usr', 'bin')
    os.makedirs(bin_path)
    shutil.copy('brickd/brickd', bin_path)

    print('creating DEBIAN/control from template')
    version = check_output(['./brickd/brickd', '--version']).replace('\n', '').replace(' ', '-')
    installed_size = int(check_output(['du', '-s', '--exclude', 'dist/DEBIAN', 'dist']).split('\t')[0])
    control_path = os.path.join(dist_path, 'DEBIAN', 'control')
    specialize_template(control_path, control_path,
                        {'<<VERSION>>': version,
                         '<<ARCHITECTURE>>': architecture,
                         '<<INSTALLED_SIZE>>': str(installed_size)})

    print('preparing files')
    system('objcopy --strip-debug --strip-unneeded dist/usr/bin/brickd')
    system('cp changelog dist/usr/share/doc/brickd/')

    if version.endswith('+redbrick'):
        os.rename('dist/etc/brickd-red-brick.conf', 'dist/etc/brickd.conf')
        os.remove('dist/etc/brickd-default.conf')
    else:
        os.rename('dist/etc/brickd-default.conf', 'dist/etc/brickd.conf')
        os.remove('dist/etc/brickd-red-brick.conf')

    system('gzip -n -9 dist/usr/share/doc/brickd/changelog')
    system('gzip -n -9 dist/usr/share/man/man8/brickd.8')
    system('gzip -n -9 dist/usr/share/man/man5/brickd.conf.5')

    system('cd dist; find usr -type f -exec md5sum {} \\; >> DEBIAN/md5sums; find lib -type f -exec md5sum {} \\; >> DEBIAN/md5sums')

    system('find dist -type d -exec chmod 0755 {} \\;')
    system('find dist -type f -perm 664 -exec chmod 0644 {} \\;')
    system('find dist -type f -perm 775 -exec chmod 0755 {} \\;')

    print('changing owner to root')
    system('sudo chown -R root:root dist')

    print('building Debian package')
    system('dpkg -b dist brickd-{0}_{1}.deb'.format(version, architecture))

    print('changing owner back to original user')
    system('sudo chown -R ${USER}:${USER} dist')

    if os.path.exists('/usr/bin/lintian'):
        print('checking Debian package')
        system('lintian --pedantic --no-tag-display-limit brickd-{0}_{1}.deb || true'.format(version, architecture))
    else:
        print('skipping lintian check')

    print('cleaning up')
    system('cd brickd; make clean')

# run 'python build_pkg.py' to build the windows/linux/macos package
if __name__ == '__main__':
    if sys.platform != 'win32' and os.geteuid() == 0:
        print('error: must not be started as root, exiting')
        sys.exit(1)

    if sys.platform.startswith('linux'):
        build_linux_pkg()
    elif sys.platform == 'win32':
        build_windows_pkg()
    elif sys.platform == 'darwin':
        build_macos_pkg()
    else:
        print('error: unsupported platform: ' + sys.platform)
        sys.exit(1)

    print('done')
