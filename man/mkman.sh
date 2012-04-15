#!/bin/bash

export NOCOLOR=1

[[ $# -eq 0 ]] && set -- $(../q | awk '$0 ~ / : / && $1 != "Usage:" { print $1 }')

for applet in "$@" ; do
	man="${applet}.1"
	echo "creating ${man}"

	help2man -N -S "Gentoo Foundation" -m ${applet} -s 1 \
		$(printf ' -I %s' include/${applet}-*.include) \
		-n "$(../q $applet | sed -n '/^Usage/{s|^.* : ||p;q;}')" \
		-o ${man} "../q ${applet}"
	[[ $? == 0 ]] || continue

	sed \
		-e "s/PORTAGE-UTILS-CVS:/${applet}/g" \
		-e "s/portage-utils-cvs:/${applet}/g" \
		-e '/\.SH SYNOPSIS/,/\.SH/s/ : .*\\fR$/\\fR/' \
		-e '/\.SH DESCRIPTION/,/\.SH/s|^\(Options:.*\)\\fR\(.*\)|\1\2\\fR|' \
		${man} | \
		awk '{
			if ($1 == "<arg>") {
				line = line " " $1
				$1 = ""
			}
			if (line)
				print line
			line = $0
		}
		END { print line }
		' | \
		sed -e 's:^ *[*] ::' > ${man}~
	mv ${man}~ ${man}

	head -n $(($(wc -l < ${man})-1)) ${man} \
		| tr '@' '\n' > ${man}~ && mv ${man}~ ${man}

	sed -e s/'compiled on'/'@@@@@@@@@@@@@@'/ ${man} |
		cut -d @ -f 1 | \
		grep -v 'DO NOT MODIFY THIS FILE' \
		> ${man}~
	mv ${man}~ ${man}
done
