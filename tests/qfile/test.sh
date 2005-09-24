
QFILE='../../q file'
if [[ $(${QFILE} -C /bin/bash)  != "app-shells/bash (/bin/bash)" ]]; then
	echo "/bin/bash does not seem to be provided by app-shells/bash. Found $(${QFILE} -Cq /bin/bash)" > /dev/stderr
	echo FAILED
	exit 1
fi
echo PASSED
exit 0
