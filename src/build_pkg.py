#!/usr/bin/env -S python -u
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


# FIXME: add build_zip.py that build a zip with the matching brickd and daemonlib
#        source code to be used as the source download instead of the github.com
#        source download that doesn't include the matching daemonlib


import sys

if sys.hexversion < 0x3040000:
    print('Python >= 3.4 required')
    sys.exit(1)

import os
import shutil
import subprocess
import json
import time
import re
import argparse
import pprint

def specialize_template(template_filename, destination_filename, replacements, remove_template=False):
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

    if remove_template:
        os.remove(template_filename)

def git_commit_id():
    try:
        commit_id = subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode('utf-8')[:7]
    except Exception:
        commit_id = 'unknown'

    return commit_id

def build_macos_pkg():
    if (sys.hexversion & 0xFF000000) != 0x03000000:
        print('Python 3.x required')
        sys.exit(1)

    parser = argparse.ArgumentParser()

    parser.add_argument('--snapshot', action='store_true')
    parser.add_argument('--no-sign', action='store_true')

    args = parser.parse_args()

    if args.snapshot:
        version_suffix = '+snapshot~' + git_commit_id()
    else:
        version_suffix = 'no'

    print('building brickd disk image')
    root_path = os.getcwd()

    print('removing old build directory')
    dist_path = os.path.join(root_path, 'dist')

    if os.path.exists(dist_path):
        shutil.rmtree(dist_path)

    print('compiling')
    subprocess.check_call('cd brickd; make clean; make CC=gcc WITH_VERSION_SUFFIX={0}'.format(version_suffix), shell=True)

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
    version = subprocess.check_output(['./brickd/brickd', '--version']).decode('utf-8').strip()
    underscore_version = version.replace('.', '_').replace('+', '_').replace('~', '_')
    plist_path = os.path.join(contents_path, 'Info.plist')
    specialize_template(plist_path, plist_path, {'<<VERSION>>': version.split('+')[0]}) # macOS only allows for <major>.<minor>.<patch> here

    if not args.no_sign:
        print('signing brickd binary')
        subprocess.check_call('security unlock-keychain /Users/$USER/Library/Keychains/login.keychain', shell=True)
        subprocess.check_call(['codesign', '--force', '--verify', '--verbose=1', '-o', 'runtime', '--sign', 'Developer ID Application: Tinkerforge GmbH (K39N76HTZ4)', app_path])

        print('notarize app')
        zip_path = os.path.join(dist_path, 'brickd.app.zip')
        subprocess.check_call(['ditto', '-c', '-k', '--keepParent', app_path, zip_path])
        output = subprocess.check_output(['xcrun', 'notarytool', 'submit', zip_path, '--output-format', 'json', '--keychain-profile', 'notary-tinkerforge']).decode('utf-8')
        notarization_submit = json.loads(output)

        try:
            request_id = notarization_submit['id']
        except:
            print('error: notarization output does not contain expected fields')
            pprint.pprint(output)
            pprint.pprint(notarization_submit)
            sys.exit(1)

        print('notarize app request id', request_id)
        notarization_info = None
        notarization_status = None

        while True:
            try:
                output = subprocess.check_output(['xcrun', 'notarytool', 'info', request_id, '--output-format', 'json', '--keychain-profile', 'notary-tinkerforge']).decode('utf-8')
            except subprocess.CalledProcessError as e:
                print('warning: notarize query failed, retrying', e)
                time.sleep(1)
                continue

            notarization_info = json.loads(output)

            try:
                notarization_status = notarization_info['status']
            except:
                print('error: notarization output does not contain expected fields')
                pprint.pprint(output)
                pprint.pprint(notarization_info)
                sys.exit(1)

            if notarization_status != 'In Progress':
                break

            print('waiting for app notarization to complete ...')
            time.sleep(10)

        print('notarization app info', notarization_info)

        if notarization_status != 'Accepted':
            print('error: notarization app failed')
            sys.exit(1)

        print('staple notarization ticket to app')
        subprocess.check_call(['xcrun', 'stapler', 'staple', app_path])

    print('building pkg')
    scripts_path = os.path.join(root_path, 'build_data', 'macos', 'installer', 'scripts')
    component_path = os.path.join(root_path, 'build_data', 'macos', 'installer', 'component.plist')

    pkgbuild_args = ['pkgbuild']

    if not args.no_sign:
        pkgbuild_args += ['--sign', 'Developer ID Installer: Tinkerforge GmbH (K39N76HTZ4)']

    pkgbuild_args += [
        '--root',
        'dist/root',
        '--identifier',
        'com.tinkerforge.brickd',
        '--version',
        version,
        '--scripts',
        scripts_path,
        '--install-location',
        '/',
        '--component-plist',
        component_path,
        'dist/brickd.pkg',
    ]

    subprocess.check_call(pkgbuild_args)

    distribution_path = os.path.join(root_path, 'build_data', 'macos', 'installer', 'distribution.xml')
    shutil.copy(distribution_path, dist_path)
    distribution_path = os.path.join(dist_path, 'distribution.xml')
    specialize_template(distribution_path, distribution_path, {'<<VERSION>>': version})
    os.makedirs('dist/dmg')
    pkg_path = 'dist/dmg/Brickd-{0}.pkg'.format(version)
    productbuild_args = ['productbuild']

    if not args.no_sign:
        productbuild_args += ['--sign', 'Developer ID Installer: Tinkerforge GmbH (K39N76HTZ4)']

    productbuild_args += [
        '--distribution',
        distribution_path,
        '--package-path',
        dist_path,
        '--version',
        version,
        pkg_path,
    ]

    subprocess.check_call(productbuild_args)

    if not args.no_sign:
        print('notarize pkg')
        output = subprocess.check_output(['xcrun', 'notarytool', 'submit', pkg_path, '--output-format', 'json', '--keychain-profile', 'notary-tinkerforge']).decode('utf-8')
        notarization_submit = json.loads(output)

        try:
            request_id = notarization_submit['id']
        except:
            print('error: notarization output does not contain expected fields')
            pprint.pprint(output)
            pprint.pprint(notarization_submit)
            sys.exit(1)

        print('notarize pkg request id', request_id)
        notarization_info = None
        notarization_status = None

        while True:
            try:
                output = subprocess.check_output(['xcrun', 'notarytool', 'info', request_id, '--output-format', 'json', '--keychain-profile', 'notary-tinkerforge']).decode('utf-8')
            except subprocess.CalledProcessError as e:
                print('warning: notarize query failed, retrying', e)
                time.sleep(1)
                continue

            notarization_info = json.loads(output)

            try:
                notarization_status = notarization_info['status']
            except:
                print('error: notarization output does not contain expected fields')
                pprint.pprint(output)
                pprint.pprint(notarization_info)
                sys.exit(1)

            if notarization_status != 'In Progress':
                break

            print('waiting for pkg notarization to complete ...')
            time.sleep(10)

        print('notarization pkg info', notarization_info)

        if notarization_status != 'Accepted':
            print('error: notarization pkg failed')
            sys.exit(1)

        print('staple notarization ticket to pkg')
        subprocess.check_call(['xcrun', 'stapler', 'staple', pkg_path])

    print('building disk image')
    dmg_name = 'brickd_macos_{0}.dmg'.format(underscore_version)

    if os.path.exists(dmg_name):
        os.remove(dmg_name)

    subprocess.check_call(['hdiutil', 'create', '-fs', 'HFS+', '-volname', 'Brickd-{0}'.format(version), '-srcfolder', 'dist/dmg', dmg_name])

def signtool_sign(path):
    subprocess.check_call(['C:\\Program Files (x86)\\Windows Kits\\10\\bin\\x86\\signtool.exe', 'sign', '/v', '/fd', 'sha256', '/tr', 'http://rfc3161timestamp.globalsign.com/advanced', '/td', 'sha256', '/a', '/n', 'Tinkerforge GmbH', path])

def signtool_verify(path):
    subprocess.check_call(['C:\\Program Files (x86)\\Windows Kits\\10\\bin\\x86\\signtool.exe', 'verify', '/v', '/pa', path])

def build_windows_pkg():
    parser = argparse.ArgumentParser()

    parser.add_argument('--snapshot', action='store_true')
    parser.add_argument('--no-sign', action='store_true')

    args = parser.parse_args()

    if args.snapshot:
        version_suffix = '+snapshot~' + git_commit_id()
    else:
        version_suffix = 'no'

    print('building brickd NSIS installer')
    root_path = os.getcwd()

    print('removing old build directory')
    dist_path = os.path.join(root_path, 'dist')

    if os.path.exists(dist_path):
        shutil.rmtree(dist_path)
        time.sleep(1) # FIXME: without this sleep the following makedirs call fails with an access-denied error

    os.makedirs(dist_path)

    print('compiling brickd.exe')
    subprocess.check_call('cd brickd && mingw32-make clean && mingw32-make CC=gcc WITH_VERSION_SUFFIX={0}'.format(version_suffix), shell=True)

    if not args.no_sign:
        print('signing brickd.exe')
        signtool_sign('dist\\brickd.exe')

        print('verifying brickd.exe signature')
        signtool_verify('dist\\brickd.exe')

    print('compiling logviewer.exe')
    subprocess.check_call('cd build_data\\windows\\logviewer && mingw32-make clean && mingw32-make CC=gcc WITH_VERSION_SUFFIX={0}'.format(version_suffix), shell=True)

    if not args.no_sign:
        print('signing logviewer.exe')
        signtool_sign('build_data\\windows\\logviewer\\logviewer.exe')

        print('verifying logviewer.exe signature')
        signtool_verify('build_data\\windows\\logviewer\\logviewer.exe')

    print('creating NSIS script from template')
    version = subprocess.check_output(['dist\\brickd.exe', '--version']).decode('utf-8').strip()
    underscore_version = version.replace('.', '_').replace('+', '_').replace('~', '_')
    build_data_path = os.path.join(root_path, 'build_data', 'windows')
    installer_template_path = os.path.join(build_data_path, 'installer', 'brickd_installer.nsi.template')
    installer_path = os.path.join(dist_path, 'installer', 'brickd_installer.nsi')
    os.makedirs(os.path.join(dist_path, 'installer'))

    print('copying build data')
    drivers_path = os.path.join(build_data_path, 'drivers')
    dist_drivers_path = os.path.join(dist_path, 'drivers')
    shutil.copytree(drivers_path, dist_drivers_path)
    shutil.copy(os.path.join(build_data_path, 'readme.txt'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'brickd-default.ini'), dist_path)
    shutil.copy(os.path.join(build_data_path, 'logviewer', 'logviewer.exe'), dist_path)

    print('create install commands')
    install_commands = []

    for root, dirs, files in os.walk(dist_path, topdown=False):
        if os.path.normpath(os.path.relpath(root, dist_path)) == 'installer':
            continue

        install_commands.append('  SetOutPath "{0}"'.format(os.path.normpath(os.path.join('$INSTDIR', os.path.relpath(root, dist_path)))))

        for file_ in files:
            path = os.path.normpath(os.path.relpath(os.path.join(root, file_), dist_path))

            install_commands.append('  File "..\\{0}"'.format(path))
            install_commands.append('  FileWrite $0 "$INSTDIR\\{0}$\\r$\\n"'.format(path))

        for dir_ in dirs:
            path = os.path.normpath(os.path.relpath(os.path.join(root, dir_), dist_path))

            if path == 'installer':
                continue

            install_commands.append('  FileWrite $0 "$INSTDIR\\{0}$\\r$\\n"'.format(path))

    specialize_template(installer_template_path, installer_path,
                        {'<<BRICKD_VERSION>>': version,
                         '<<BRICKD_UNDERSCORE_VERSION>>': underscore_version,
                         '<<BRICKD_INSTALL_COMMANDS>>': '\n'.join(install_commands)})

    print('building NSIS installer')
    subprocess.check_call(['C:\\Program Files (x86)\\NSIS\\makensis.exe', 'dist\\installer\\brickd_installer.nsi'])
    installer = 'brickd_windows_{0}.exe'.format(underscore_version)

    if os.path.exists(installer):
        os.unlink(installer)

    shutil.move(os.path.join(dist_path, 'installer', installer), root_path)

    if not args.no_sign:
        print('signing NSIS installer')
        signtool_sign(installer)

        print('verifying NSIS installer signature')
        signtool_verify(installer)

def git_ls_files(path):
    if subprocess.call('cd {0}; git rev-parse --is-inside-work-tree >/dev/null 2>&1'.format(path), shell=True) == 0:
        return subprocess.check_output(['git', 'ls-files'], cwd=path).decode('utf-8').strip().split('\n')

    if subprocess.call('git help >/dev/null 2>&1'.format(path), shell=True) == 0:
        warning = 'warning: {0} is not in a git repository, using raw directory listing instead'.format(path)
    else:
        warning = 'warning: git is not installed, using raw directory listing instead'

    if sys.stdin.isatty():
        if input('\033[33m{0}. continue anyway?\033[0m [y/N] '.format(warning)).strip() not in ['y', 'Y']:
            print('aborted')
            sys.exit(1)
    else:
        print('\033[33m{0}. aborted!\033[0m'.format(warning))
        sys.exit(1)

    result = []

    for root, dirs, files in os.walk(path):
        for f in files:
            p = os.path.join(root, f)

            if os.path.isfile(p):
                result.append(os.path.relpath(p, path))

    return result

def parse_changelog(path):
    versions = []

    with open(path, 'r') as f:
        for i, line in enumerate(f.readlines()):
            line = line.rstrip()

            if len(line) == 0:
                continue

            if re.match(r'^(?:- ([A-Z0-9\(]|macOS)|  [A-Za-z0-9\(\"]).*$', line) != None:
                continue

            m = re.match(r'^(?:<unknown>|20[0-9]{2}-[0-9]{2}-[0-9]{2}): ([1-9][0-9]*)\.([0-9]+)\.([0-9]+) \((?:<unknown>|[a-f0-9]+)\)$', line)

            if m == None:
                raise Exception('invalid line {0} in changelog {1}: {2}'.format(i + 1, path, line))

            version = (int(m.group(1)), int(m.group(2)), int(m.group(3)))

            if version[0] not in [1, 2]:
                raise Exception('invalid major version in changelog {0}: {1}'.format(path, version))

            if len(versions) > 0:
                if versions[-1] >= version:
                    raise Exception('invalid version order in changelog {0}: {1} -> {2}'.format(path, versions[-1], version))

                if versions[-1][0] == version[0] and versions[-1][1] == version[1] and versions[-1][2] + 1 != version[2]:
                    raise Exception('invalid version jump in changelog {0}: {1} -> {2}'.format(path, versions[-1], version))

                if versions[-1][0] == version[0] and versions[-1][1] != version[1] and versions[-1][1] + 1 != version[1]:
                    raise Exception('invalid version jump in changelog {0}: {1} -> {2}'.format(path, versions[-1], version))

                if versions[-1][1] != version[1] and version[2] != 0:
                    raise Exception('invalid version jump in changelog {0}: {1} -> {2}'.format(path, versions[-1], version))

                if versions[-1][0] != version[0] and (version[1] != 0 or version[2] != 0):
                    raise Exception('invalid version jump in changelog {0}: {1} -> {2}'.format(path, versions[-1], version))

            versions.append(version)

    if len(versions) == 0:
        raise Exception('no version found in changelog: ' + path)

    return '{0}.{1}.{2}'.format(versions[-1][0], versions[-1][1], versions[-1][2])

def build_linux_pkg():
    if (sys.hexversion & 0xFF000000) != 0x03000000:
        print('Python 3.x required')
        sys.exit(1)

    parser = argparse.ArgumentParser()

    parser.add_argument('--snapshot', action='store_true')
    parser.add_argument('--changelog-date')
    parser.add_argument('--with-red-brick', action='store_true')

    args = parser.parse_args()

    if args.with_red_brick:
        version_suffix = '+redbrick'
    else:
        version_suffix = ''

    if args.snapshot:
        version_suffix += '+snapshot~' + git_commit_id()
    else:
        version_suffix += ''

    changelog_version = parse_changelog('changelog') + version_suffix
    architecture = subprocess.check_output(['dpkg', '--print-architecture']).decode('utf-8').strip()

    print('building brickd Debian package for {0}'.format(architecture))
    root_path = os.getcwd()

    print('removing old build directory')
    dist_path = os.path.join(root_path, 'dist', architecture)

    if args.with_red_brick:
        dist_path += '+redbrick'

    if os.path.exists(dist_path):
        shutil.rmtree(dist_path)

    source_path = os.path.join(dist_path, 'tinkerforge-brickd-{0}'.format(changelog_version))

    print('collecting brickd source')
    brickd_files = git_ls_files('brickd')
    brickd_path = os.path.join(source_path, 'brickd')

    os.makedirs(brickd_path)

    for brickd_file in brickd_files:
        path = os.path.join(brickd_path, brickd_file)

        os.makedirs(os.path.dirname(path), exist_ok=True)
        shutil.copy(os.path.join('brickd', brickd_file), os.path.join(brickd_path, brickd_file))

    print('collecting daemonlib source')
    daemonlib_files = git_ls_files('daemonlib')
    daemonlib_path = os.path.join(source_path, 'daemonlib')

    os.makedirs(daemonlib_path)

    for daemonlib_file in daemonlib_files:
        path = os.path.join(daemonlib_path, daemonlib_file)

        os.makedirs(os.path.dirname(path), exist_ok=True)
        shutil.copy(os.path.join('daemonlib', daemonlib_file), path)

    print('collecting installer build_data')
    installer_build_data_files = git_ls_files('build_data/linux/installer')
    installer_build_data_path = os.path.join(source_path, 'build_data/linux/installer')

    os.makedirs(installer_build_data_path)

    for installer_build_data_file in installer_build_data_files:
        path = os.path.join(installer_build_data_path, installer_build_data_file)

        os.makedirs(os.path.dirname(path), exist_ok=True)
        shutil.copy(os.path.join('build_data/linux/installer', installer_build_data_file), path)

    shutil.move(os.path.join(installer_build_data_path, 'debian'), os.path.join(source_path, 'debian'))

    print('collecting libgpiod_dlopen build_data')
    libgpiod_dlopen_build_data_files = git_ls_files('build_data/linux/libgpiod_dlopen')
    libgpiod_dlopen_build_data_path = os.path.join(source_path, 'build_data/linux/libgpiod_dlopen')

    os.makedirs(libgpiod_dlopen_build_data_path)

    for libgpiod_dlopen_build_data_file in libgpiod_dlopen_build_data_files:
        path = os.path.join(libgpiod_dlopen_build_data_path, libgpiod_dlopen_build_data_file)

        os.makedirs(os.path.dirname(path), exist_ok=True)
        shutil.copy(os.path.join('build_data/linux/libgpiod_dlopen', libgpiod_dlopen_build_data_file), path)

    print('building Debian package')

    if args.changelog_date != None:
        changelog_date = args.changelog_date
    else:
        changelog_date = subprocess.check_output(['date', '-R']).decode('utf-8').strip()

    print('changelog date:', changelog_date)

    specialize_template(os.path.join(source_path, 'debian/changelog.template'),
                        os.path.join(source_path, 'debian/changelog'),
                        {'<<VERSION>>': changelog_version,
                         '<<DATE>>': changelog_date},
                        remove_template=True)

    subprocess.check_call('cd {0}; dpkg-buildpackage -us -uc'.format(source_path), shell=True)

    binary_version = subprocess.check_output([os.path.join(source_path, 'debian/brickd/usr/bin/brickd'), '--version']).decode('utf-8').strip()

    if changelog_version != binary_version:
        print('error: version mismatch between changelog and binary: {0} != {1}'.format(changelog_version, binary_version))
        sys.exit(1)

    maximum_glibc_version = (2, 34, 0)
    glibc_version = (0, 0, 0)
    glibc_symbols = []

    for line in subprocess.check_output(['objdump', '-T', os.path.join(source_path, 'debian/brickd/usr/bin/brickd')]).decode('utf-8').split('\n'):
        m = re.search(r'(\(?GLIBC_([0-9\.]+).*)', line.strip())

        if m == None:
            continue

        symbol = m.group(1)
        version = tuple(int(x) for x in m.group(2).split('.'))

        if version > maximum_glibc_version:
            glibc_symbols.append(symbol)

        if version > glibc_version:
            glibc_version = version

    while len(glibc_version) < 3:
        glibc_version += (0,)

    if glibc_version > maximum_glibc_version:
        print('\n'.join(glibc_symbols))

        warning = 'warning: brickd binary imports glibc {0}.{1}.{2} symbols, but should import symbols from glibc <= {3}.{4}.{5} only'.format(*(glibc_version + maximum_glibc_version))

        if sys.stdin.isatty():
            if input('\033[33m{0}. continue anyway?\033[0m [y/N] '.format(warning)).strip() not in ['y', 'Y']:
                print('aborted')
                sys.exit(1)
        else:
            print('\033[33m{0}. aborted!\033[0m'.format(warning))
            sys.exit(1)

    if os.path.exists('/usr/bin/lintian'):
        print('checking Debian package')
        subprocess.check_call(['lintian', '--verbose', '--pedantic', '--tag-display-limit', '0', '{0}/brickd_{1}_{2}.deb'.format(dist_path, changelog_version, architecture)])
    else:
        print('skipping lintian check')

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
