TOP=../..

-include ${TOP}/Makefile.conf


# Paths from GIT to include in our source tar
PATH_LIST          := linux common tools Makefile Makefile.ver TD-VERSION-GEN

DEBBUILD_TREE      := $(shell pwd)/debbuild
DEBBUILD_ARCH      := $(shell uname -m)
DEBBUILD_KVER      := $(shell uname -r)
MKDIR_P             = mkdir -p
OUTDIRS             = SOURCES
TAR = tar --transform "s,^,teradimm_$(TERADIMM_VERSION)/,"


all debs: src.deb
	
include ${TOP}/Makefile.ver
TERADIMM_VERSION = ${TERADIMM_VERSION_STRING}

filelist: FORCE
	@git ls-tree --name-only -r --full-tree HEAD ${PATH_LIST} > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
	
%.gz: %
	gzip -9 -v $<

%.tar: VERSION filelist
	$(TAR) -C ../.. -T filelist -cf $@.tmp
	$(TAR) --append --file $@.tmp VERSION
	mv $@.tmp $@
	@rm -f $@.tmp


VERSION: FORCE
	@echo ${TERADIMM_VERSION_STRING} > $@.tmp
	@echo ${TD_VER_REVISION} >> $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp


.PHONY: debbuild-tree
debbuild-tree: ${OUTDIRS}


${OUTDIRS}:
	${MKDIR_P} ${DEBBUILD_TREE}/$@

$(DEBBUILD_TREE)/SOURCES/%: % debbuild-tree
	cp $< $@

$(DEBBUILD_TREE)/SOURCES/%.gz: %
	gzip -9 -v -c $< > $@.tmp
	mv $@.tmp $@

src.deb: debbuild-tree $(DEBBUILD_TREE)/SOURCES/teradimm_$(TERADIMM_VERSION).tar.gz
	cp $(DEBBUILD_TREE)/SOURCES/teradimm_$(TERADIMM_VERSION).tar.gz teradimm_$(TERADIMM_VERSION).orig.tar.gz
	tar -xzf $(DEBBUILD_TREE)/SOURCES/teradimm_$(TERADIMM_VERSION).tar.gz
	mkdir -p debian/source
	dch --create -v $(TERADIMM_VERSION) --package teradimm "new version"
	echo 9 > debian/compat
	cp control.driver debian/control
	sed -e 's/Package: teradimm/Package: teradimm-$(DEBBUILD_KVER)/g' debian/control > debian/control.$$
	mv debian/control.$$ debian/control
	echo " TeraDIMM kernel module for $(DEBBUILD_KVER)" >> debian/control
	touch debian/copyright
	cp rules.driver debian/rules
	sed -e 's/make KVERSION= -C linux\/driver/make KVERSION=$(DEBBUILD_KVER) -C linux\/driver/g' debian/rules > debian/rules.$$
	mv debian/rules.$$ debian/rules
	echo "3.0 (quilt)" > debian/source/format
	echo "linux/driver/teradimm.ko /lib/modules/$(DEBBUILD_KVER)/extra/diablo" > debian/install
	echo "kmod-teradimm.conf /etc/depmod.d" >> debian/install
	echo "linux/udev/60-persistent-teradimm.rules" /etc/udev/rules.d >> debian/install
	mv debian teradimm_$(TERADIMM_VERSION)
	cd teradimm_$(TERADIMM_VERSION);debuild -uc -us

	rm -rf teradimm_$(TERADIMM_VERSION)
	tar -xzf $(DEBBUILD_TREE)/SOURCES/teradimm_$(TERADIMM_VERSION).tar.gz
	mkdir -p debian/source
	dch --create -v $(TERADIMM_VERSION) --package teradimm "new version"
	echo 9 > debian/compat
	cp control.tools debian/control
	touch debian/copyright
	cp rules.tools debian/rules
	echo "3.0 (quilt)" > debian/source/format
	echo "tools/tdcmd/tdcmd /usr/sbin/tdcmd" > debian/install
	echo "tools/tdctl/tdctl /usr/sbin/tdctl" >> debian/install
	mv debian teradimm_$(TERADIMM_VERSION)
	cd teradimm_$(TERADIMM_VERSION);debuild -uc -us

clean:
	rm -rf filelist *.tar *.tz debbuild teradimm* VERSION

.PHONY: FORCE
	
