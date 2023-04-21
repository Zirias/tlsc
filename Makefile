SINGLECONFVARS=	OPENSSLINC OPENSSLLIB
include zimk/zimk.mk

$(call zinc, src/bin/tlsc/tlsc.mk)
