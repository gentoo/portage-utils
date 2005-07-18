#Usage: help2man [OPTION]... EXECUTABLE
#
# -n, --name=STRING       description for the NAME paragraph
# -s, --section=SECTION   section number for manual page (1, 6, 8)
# -m, --manual=TEXT       name of manual (User Commands, ...)
# -S, --source=TEXT       source of program (FSF, Debian, ...)
# -i, --include=FILE      include material from `FILE'
# -I, --opt-include=FILE  include material from `FILE' if it exists
# -o, --output=FILE       send output to `FILE'
# -p, --info-page=TEXT    name of Texinfo manual
# -N, --no-info           suppress pointer to Texinfo manual
#     --help              print this help, then exit
#     --version           print version number, then exit

export NOCOLOR=1

APPLETS=$(../q | grep -e ' : ' | awk '{print $1}' | grep ^q)

for applet in $APPLETS; do
	help2man -N -S "Gentoo Foundation" -m ${applet} -s 1 -o ${applet}.1 "../q $applet"
	sed  -i -e s/'PORTAGE-UTILS-CVS:'/${applet}/g \
		-e s/'portage-utils-cvs:'/${applet}/g \
		-e s/'> \*'/'>@\.BR@ \*'/g ${applet}.1
	head -n $(($(cat ${applet}.1 | wc -l)-1)) ${applet}.1 \
		| tr '@' '\n' > ${applet}.1~ && mv ${applet}.1~ ${applet}.1
done
