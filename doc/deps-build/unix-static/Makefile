
#
# This makefile downloads, configures and builds Slic3r PE dependencies for Unix.
# (That is, all dependencies except perl + wxWidgets.)
# The libraries are installed in DESTDIR, which you can customize like so:
#
#     DESTDIR=foo/bar make
#
# The default DESTDIR is ~/slic3r-destdir
# If the DESTDIR doesn't exits, the makefile tries to create it
#
# To pass the DESTDIR path along to cmake, set the use CMAKE_PREFIX_PATH variable
# and set it to $DESTDIR/usr/local
#
# You can also customize the NPROC variable in the same way to configure the number
# of cores the build process uses. By default this is set to what the `nproc` command says.
#


DESTDIR ?= $(HOME)/slic3r-destdir
NPROC ?= $(shell nproc)


BOOST = boost_1_66_0
TBB_SHA = a0dc9bf76d0120f917b641ed095360448cabc85b
TBB = tbb-$(TBB_SHA)
OPENSSL = openssl-OpenSSL_1_1_0g
CURL = curl-7.58.0

.PHONY: all destdir boost libcurl libopenssl libtbb

all: destdir boost libtbb libcurl
	@echo
	@echo "All done!"
	@echo

destdir:
	mkdir -p $(DESTDIR)



boost: $(BOOST).tar.gz
	tar -zxvf $(BOOST).tar.gz
	cd $(BOOST) && ./bootstrap.sh --with-libraries=system,filesystem,thread,log,locale,regex --prefix=$(DESTDIR)/usr/local
	cd $(BOOST) && ./b2 \
		-j $(NPROC) \
		link=static \
		variant=release \
		threading=multi \
		boost.locale.icu=off \
		cxxflags=-fPIC cflags=-fPIC \
		install

$(BOOST).tar.gz:
	curl -L -o $@ https://dl.bintray.com/boostorg/release/1.66.0/source/$@



libtbb: $(TBB).tar.gz
	tar -zxvf $(TBB).tar.gz
	mkdir -p $(TBB)/mybuild
	cd $(TBB)/mybuild && cmake .. -DTBB_BUILD_SHARED=OFF -DTBB_BUILD_TESTS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
	$(MAKE) -C $(TBB)/mybuild -j$(NPROC)
	$(MAKE) -C $(TBB)/mybuild install DESTDIR=$(DESTDIR)

$(TBB).tar.gz:
	curl -L -o $@ https://github.com/wjakob/tbb/archive/$(TBB_SHA).tar.gz


# Note: libcurl build system seems to be a bit wonky about finding openssl (cf. #2378).
# It seems that currently the only working option is to set a prefix in the openssl build
# and use the `--with-ssl=...` option in libcurl.
# Additionally, pkg-config needs to be installed and openssl libs need to NOT be installed on the build system.

libopenssl: $(OPENSSL).tar.gz
	tar -zxvf $(OPENSSL).tar.gz
	cd $(OPENSSL) && ./config --prefix=$(DESTDIR)/usr/local no-shared no-ssl3-method no-dynamic-engine '-Wa,--noexecstack'
	$(MAKE) -C $(OPENSSL) depend
	$(MAKE) -C $(OPENSSL) -j$(NPROC)
	$(MAKE) -C $(OPENSSL) install_sw

$(OPENSSL).tar.gz:
	curl -L -o $@ 'https://github.com/openssl/openssl/archive/OpenSSL_1_1_0g.tar.gz'



libcurl: libopenssl $(CURL).tar.gz
	tar -zxvf $(CURL).tar.gz
	cd $(CURL) && ./configure \
		--enable-static \
		--disable-shared \
		--with-ssl=$(DESTDIR)/usr/local \
		--with-pic \
		--enable-ipv6 \
		--enable-versioned-symbols \
		--enable-threaded-resolver \
		--with-random=/dev/urandom \
		--with-ca-bundle=/etc/ssl/certs/ca-certificates.crt \
		--disable-ldap \
		--disable-ldaps \
		--disable-manual \
		--disable-rtsp \
		--disable-dict \
		--disable-telnet \
		--disable-pop3 \
		--disable-imap \
		--disable-smb \
		--disable-smtp \
		--disable-gopher \
		--disable-crypto-auth \
		--without-gssapi \
		--without-libpsl \
		--without-libidn2 \
		--without-gnutls \
		--without-polarssl \
		--without-mbedtls \
		--without-cyassl \
		--without-nss \
		--without-axtls \
		--without-brotli \
		--without-libmetalink \
		--without-libssh \
		--without-libssh2 \
		--without-librtmp \
		--without-nghttp2 \
		--without-zsh-functions-dir
	$(MAKE) -C $(CURL) -j$(NPROC)
	$(MAKE) -C $(CURL) install DESTDIR=$(DESTDIR)

$(CURL).tar.gz:
	curl -L -o $@ https://curl.haxx.se/download/$@


clean:
	rm -rf $(BOOST) $(BOOST).tar.gz $(TBB) $(TBB).tar.gz $(OPENSSL) $(OPENSSL).tar.gz $(CURL) $(CURL).tar.gz
