#!/bin/sh
export NOCOLOR=1

APPLETS=$(../q | grep -e ' : ' | awk '{print $1}' | grep ^q$1)

for applet in $APPLETS; do
	help2man -N -S "Gentoo Foundation" -m ${applet} -s 1 -o ${applet}.1 "../q $applet"
	[[ $? == 0 ]] || continue;
	sed  -i -e s/'PORTAGE-UTILS-CVS:'/${applet}/g \
		-e s/'portage-utils-cvs:'/${applet}/g \
		-e s/'> \*'/'>@\.BR@ \*'/g ${applet}.1
	head -n $(($(cat ${applet}.1 | wc -l)-1)) ${applet}.1 \
		| tr '@' '\n' > ${applet}.1~ && mv ${applet}.1~ ${applet}.1

	sed -i -e s/'compiled on'/'@@@@@@@@@@@@@@'/ ${applet}.1
	cat ${applet}.1 | cut -d @ -f 1 > ${applet}.1~ && mv ${applet}.1~ ${applet}.1
done
