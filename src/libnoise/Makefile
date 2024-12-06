.PHONY: all doc src include
all: doc src include lib
clean: cleandoc cleansrc cleaninclude cleanlib
install: installinclude installlib

doc src include lib:
	$(MAKE) -C $@

lib: include

cleandoc:
	$(MAKE) -C doc clean
cleansrc:
	$(MAKE) -C src clean
cleaninclude:
	$(MAKE) -C include clean
cleanlib:
	$(MAKE) -C lib clean

installinclude:
	$(MAKE) -C include include
installlib:
	$(MAKE) -C lib include
