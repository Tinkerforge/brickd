#!/usr/bin/python3
# -*- coding: utf-8 -*

import sys

if 'indent' in sys.argv[1:]:
    lines = []
    indent = -1

    with open('Makefile', 'r') as f:
        for line in f.readlines():
            if line.startswith('if'):
                indent += 1
                lines.append('\t' * max(indent, 0) + line)
            elif line.startswith('endif'):
                lines.append('\t' * max(indent, 0) + line)
                indent -= 1
            elif len(line.strip()) > 0:
                lines.append('\t' * max(indent, 0) + line)
            else:
                lines.append(line)

    with open('Makefile.indent', 'w') as f:
        f.writelines(lines)
elif 'flatten' in sys.argv[1:]:
    lines = []

    with open('Makefile.indent', 'r') as f:
        for line in f.readlines():
            stripped = line.lstrip('\t')

            if stripped.startswith('if') or \
               stripped.startswith('else') or \
               stripped.startswith('endif') or \
               stripped.startswith('$(error'):
                lines.append(stripped)
            elif line.startswith('\t'):
                lines.append('\t' + stripped)
            else:
                lines.append(line)

    with open('Makefile', 'w') as f:
        f.writelines(lines)
else:
    print('usage: {} indent|flatten'.format(sys.argv[0]))
