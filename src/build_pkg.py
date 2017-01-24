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

    return output


def specialize_template(template_filename, destination_filename, replacements):
    template_file = open(template_filename, 'rb')
    lines = []
    replaced = set()

    for line in template_file.readlines():
        for key in replacements:
            replaced_line = line.replace(key, replacements[key])

            if replaced_line != line:
                replaced.add(key)

            line = replaced_line

        lines.append(line)

    template_file.close()

    if replaced != set(replacements.keys()):
        raise Exception('Not all replacements for {0} have been applied'.format(template_filename))

    destination_file = open(destination_filename, 'wb')
    destination_file.writelines(lines)
    destination_file.close()


def build_macosx_pkg():
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
    installer_root_path = os.path.join(root_path, 'build_data', 'macosx', 'installer', 'root')
    dist_root_path = os.path.join(dist_path, 'root')
    shutil.copytree(installer_root_path, dist_root_path)

    print('copying brickd binary')
    brickd_app_path = os.path.join(dist_root_path, 'usr', 'local', 'libexec', 'brickd.app')
    contents_path = os.path.join(brickd_app_path, 'Contents')
    macos_path = os.path.join(contents_path, 'MacOS')
    os.makedirs(macos_path)
    shutil.copy('brickd/brickd', macos_path)

    print('creating Info.plist from template')
    version = check_output(['./brickd/brickd', '--version']).replace('\n', '')
    plist_path = os.path.join(contents_path, 'Info.plist')
    specialize_template(plist_path, plist_path, {'<<VERSION>>': version})

    print('copying and patching libusb')
    libusb_path = os.path.join(root_path, 'build_data', 'macosx', 'libusb', 'libusb-1.0.dylib')
    shutil.copy(libusb_path, macos_path)
    system('install_name_tool -id @executable_path/libusb-1.0.dylib {0}'.format(os.path.join(macos_path, 'libusb-1.0.dylib')))
    system('install_name_tool -change @executable_path/../build_data/macosx/libusb/libusb-1.0.dylib @executable_path/libusb-1.0.dylib {0}'.format(os.path.join(macos_path, 'brickd')))

    print('signing libusb and brickd binaries')
    system('security unlock-keychain /Users/$USER/Library/Keychains/login.keychain')
    # NOTE: codesign_application_identity contains "Developer ID Application: ..."
    codesign_command = 'codesign --force --verify --verbose --sign "`cat codesign_application_identity`" {0}'
    system(codesign_command.format(os.path.join(macos_path, 'libusb-1.0.dylib')))
    system(codesign_command.format(brickd_app_path))

    print('building pkg')
    scripts_path = os.path.join(root_path, 'build_data', 'macosx', 'installer', 'scripts')
    component_path = os.path.join(root_path, 'build_data', 'macosx', 'installer', 'component.plist')
    # NOTE: codesign_installer_identity contains "Developer ID Installer: ..."
    system('pkgbuild --sign "`cat codesign_installer_identity`" --root dist/root --identifier com.tinkerforge.brickd --version {0} --scripts {1} --install-location / --component-plist {2} dist/brickd.pkg'.format(version, scripts_path, component_path))
    distribution_path = os.path.join(root_path, 'build_data', 'macosx', 'installer', 'distribution.xml')
    shutil.copy(distribution_path, dist_path)
    distribution_path = os.path.join(dist_path, 'distribution.xml')
    specialize_template(distribution_path, distribution_path, {'<<VERSION>>': version})
    os.makedirs('dist/dmg')
    system('productbuild --sign "`cat codesign_installer_identity`" --distribution {0} --package-path {1} --version {2} dist/dmg/Brickd-{2}.pkg'.format(distribution_path, dist_path, version))

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

    os.makedirs(dist_path)

    print('compiling')
    system('cd brickd && compile.bat')

    if os.path.exists('X:\\sign.bat'):
        system('X:\\sign.bat dist\\brickd.exe')

    print('creating NSIS script from template')
    version = check_output(['dist\\brickd.exe', '--version']).replace('\r\n', '')
    build_data_path = os.path.join(root_path, 'build_data', 'windows')
    installer_template_path = os.path.join(build_data_path, 'installer', 'brickd_installer.nsi.template')
    installer_path = os.path.join(dist_path, 'installer', 'brickd_installer.nsi')
    os.makedirs(os.path.join(dist_path, 'installer'))
    specialize_template(installer_template_path, installer_path,
                        {'<<BRICKD_DOT_VERSION>>': version,
                         '<<BRICKD_UNDERSCORE_VERSION>>': version.replace('.', '_')})

    print('copying build data')
    drivers_path = os.path.join(build_data_path, 'drivers')
    dist_drivers_path = os.path.join(dist_path, 'drivers')
    shutil.copytree(drivers_path, dist_drivers_path)
    shutil.copy(os.path.join(build_data_path, 'readme.txt'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'brickd.ini'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'logviewer', 'logviewer.exe'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'logviewer', 'logviewer.pdb'), dist_path)

    print('building NSIS installer')
    system('"C:\\Program Files\\NSIS\\makensis.exe" dist\\installer\\brickd_installer.nsi')
    installer = 'brickd_windows_{0}.exe'.format(version.replace('.', '_'))

    if os.path.exists(installer):
        os.unlink(installer)

    shutil.move(os.path.join(dist_path, 'installer', installer), root_path)

    if os.path.exists('X:\\sign.bat'):
        system('X:\\sign.bat ' + installer)


def build_linux_pkg():
    print('building brickd Debian package')
    root_path = os.getcwd()

    print('removing old build directory')
    dist_path = os.path.join(root_path, 'dist')

    if os.path.exists(dist_path):
        shutil.rmtree(dist_path)

    architecture = check_output(['dpkg', '--print-architecture']).replace('\n', '')

    print('compiling for ' + architecture)
    system('cd brickd; make clean')

    if architecture == 'i386':
        system('cd brickd; env CC=gcc WITH_LIBUDEV=yes WITH_LIBUDEV_DLOPEN=yes WITH_PM_UTILS=yes CFLAGS=-march=i386 make')
    else:
        system('cd brickd; env CC=gcc WITH_LIBUDEV=yes WITH_LIBUDEV_DLOPEN=yes WITH_PM_UTILS=yes make')

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

    system('gzip -9 dist/usr/share/doc/brickd/changelog')
    system('gzip -9 dist/usr/share/man/man8/brickd.8')
    system('gzip -9 dist/usr/share/man/man5/brickd.conf.5')

    system('cd dist; find usr -type f -exec md5sum {} \; >> DEBIAN/md5sums')

    system('find dist -type d -exec chmod 0755 {} \;')

    os.chmod('dist/DEBIAN/conffiles', 0644)
    os.chmod('dist/DEBIAN/md5sums', 0644)
    os.chmod('dist/DEBIAN/preinst', 0755)
    os.chmod('dist/DEBIAN/postinst', 0755)
    os.chmod('dist/DEBIAN/prerm', 0755)
    os.chmod('dist/DEBIAN/postrm', 0755)

    os.chmod('dist/usr/bin/brickd', 0755)
    os.chmod('dist/etc/brickd.conf', 0644)
    os.chmod('dist/etc/init.d/brickd', 0755)
    os.chmod('dist/etc/logrotate.d/brickd', 0644)
    os.chmod('dist/usr/share/doc/brickd/changelog.gz', 0644)
    os.chmod('dist/usr/share/doc/brickd/copyright', 0644)
    os.chmod('dist/usr/share/man/man8/brickd.8.gz', 0644)
    os.chmod('dist/usr/share/man/man5/brickd.conf.5.gz', 0644)
    os.chmod('dist/usr/lib/pm-utils/sleep.d/42brickd', 0755)

    print('changing owner to root')
    system('sudo chown -R root:root dist')

    print('building Debian package')
    system('dpkg -b dist brickd-{0}_{1}.deb'.format(version, architecture))

    print('changing owner back to original user')
    system('sudo chown -R `logname`:`logname` dist')

    print('checking Debian package')
    system('lintian --pedantic brickd-{0}_{1}.deb'.format(version, architecture))

    print('cleaning up')
    system('cd brickd; make clean')


# run 'python build_pkg.py' to build the windows/linux/macosx package
if __name__ == '__main__':
    if sys.platform != 'win32' and os.geteuid() == 0:
        print('error: must not be started as root, exiting')
        sys.exit(1)

    if sys.platform.startswith('linux'):
        build_linux_pkg()
    elif sys.platform == 'win32':
        build_windows_pkg()
    elif sys.platform == 'darwin':
        build_macosx_pkg()
    else:
        print('error: unsupported platform: ' + sys.platform)
        sys.exit(1)

    print('done')
