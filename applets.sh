#!/bin/sh
sed -n '/^DECLARE_APPLET/s:.*(\(.*\))$:\1:p' "${0%/*}"/applets.h | sort
