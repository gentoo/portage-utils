#!/usr/bin/python

"""Generate man pages for the q applets"""

from __future__ import print_function

import datetime
import functools
import glob
import locale
import multiprocessing
import os
import re
import subprocess
import sys


MKMAN_DIR = os.path.realpath(os.path.join(__file__, '..'))
FRAGS_DIR = os.path.join(MKMAN_DIR, 'include')

TOPDIR = os.path.join(MKMAN_DIR, '..')
Q = os.path.join(TOPDIR, 'q')


def FindApplets():
    """Return a list of all supported applets"""
    applets = os.path.join(TOPDIR, 'applets.sh')
    return subprocess.check_output([applets]).splitlines()


COMMON_AUTHORS = [
    'Ned Ludd <solar@gentoo.org>',
    'Mike Frysinger <vapier@gentoo.org>',
]
TEMPLATE = r""".TH %(applet)s "1" "%(date)s" "Gentoo Foundation" "%(applet)s"
.SH NAME
%(applet)s \- %(short_desc)s
.SH SYNOPSIS
.B %(applet)s
\fI%(usage)s\fR
.SH DESCRIPTION
%(description)s
.SH OPTIONS
%(options)s
%(extra_sections)s
.SH "REPORTING BUGS"
Please report bugs via http://bugs.gentoo.org/
.br
Product: Portage Development; Component: Tools
.SH AUTHORS
.nf
%(authors)s
.fi
.SH "SEE ALSO"
%(see_also)s
"""

def MkMan(applets, applet, output):
    """Generate a man page for |applet| and write it to |output|"""
    print('%-10s: generating %s' % (applet, output))

    # Extract the main use string and description:
    # Usage: q <applet> <args>  : invoke a portage utility applet
    ahelp = subprocess.check_output([Q, applet, '--help'])
    lines = ahelp.splitlines()
    m = re.search(r'^Usage: %s (.*) : (.*)' % applet, ahelp)
    usage = m.group(1)
    short_desc = m.group(2)

    authors = COMMON_AUTHORS
    see_also = sorted(['.BR %s (1)' % x for x in applets if x != applet])

    description = ''
    desc_file = os.path.join(FRAGS_DIR, '%s.desc' % applet)
    if os.path.exists(desc_file):
        description = open(desc_file).read().rstrip()

    # Extract all the options
    options = []
    for line, i in zip(lines, xrange(len(lines))):
        if not line.startswith('Options: '):
            continue

        for option in [x.strip().split() for x in lines[i + 1:]]:
            flags = [option[0].rstrip(',')]
            option.pop(0)
            if option[0][0] == '-':
                flags += [option[0].rstrip(',')]
                option.pop(0)

            if option[0] == '<arg>':
                flags = [r'\fB%s\fR \fI<arg>\fR' % x for x in flags]
                option.pop(0)
            else:
                flags = [r'\fB%s\fR' % x for x in flags]

            assert option[0] == '*', 'help line for %s is broken: %r' % (applet, option)
            option.pop(0)

            options += [
                '.TP',
                ', '.join(flags).replace('-', r'\-'),
                ' '.join(option),
            ]
        break

    # Handle applets that have applets
    extra_sections = []
    if 'Currently defined applets:' in ahelp:
        alines = lines[lines.index('Currently defined applets:') + 1:]
        alines = alines[:alines.index('')]
        extra_sections += (
            ['.SH APPLETS', '.nf',
             '.B This applet also has sub applets:'] +
            alines +
            ['.fi']
        )

    # Handle any fragments this applet has available
    for frag in sorted(glob.glob(os.path.join(FRAGS_DIR, '%s-*.include' % applet))):
        with open(frag) as f:
            extra_sections += [x.rstrip() for x in f.readlines()]

    data = {
        'applet': applet,
        'date': datetime.datetime.now().strftime('%b %Y'),
        'short_desc': short_desc,
        'usage': usage,
        'description': description,
        'options': '\n'.join(options),
        'extra_sections': '\n'.join(extra_sections),
        'authors': '\n'.join(authors),
        'see_also': ',\n'.join(see_also),
        'rcsid': '',
    }
    with open(output, 'w') as f:
        f.write(TEMPLATE % data)


def _MkMan(applets, applet):
    """Trampoline to MkMan for multiprocessing pickle"""
    output = os.path.join(MKMAN_DIR, '%s.1' % applet)
    MkMan(applets, applet, output)


def main(argv):
    os.environ['NOCOLOR'] = '1'

    if not argv:
        argv = FindApplets()
    # Support file completion like "qfile.1" or "./qdepends.1"
    applets = [os.path.basename(x).split('.', 1)[0] for x in argv]

    p = multiprocessing.Pool()
    functor = functools.partial(_MkMan, applets)
    p.map(functor, applets)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
