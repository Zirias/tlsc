tlsc_MODULES:=	config \
		main \
		tlsc

tlsc_PKGDEPS:=	posercore

$(call binrules, tlsc)
