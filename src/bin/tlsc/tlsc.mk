tlsc_MODULES:=	client \
		connection \
		daemon \
		event \
		log \
		main \
		server \
		service \
		threadpool \
		tlsc \
		util

tlsc_LDFLAGS:=	-pthread

ifneq ($(OPENSSLINC)$(OPENSSLLIB),)
  ifeq ($(OPENSSLINC),)
$(error OPENSSLLIB specified without OPENSSLINC)
  endif
  ifeq ($(OPENSSLLIB),)
$(error OPENSSLINC specified without OPENSSLLIB)
  endif
tlsc_INCLUDES+=	-I$(OPENSSLINC)
tlsc_LDFLAGS+=	-L$(OPENSSLLIB)
tlsc_LIBS+=	ssl
else
tlsc_PKGDEPS+=	libssl
endif

$(call binrules, tlsc)
