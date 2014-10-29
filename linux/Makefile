SUBDIRS        = driver
SUBDIR_ACTIONS = all clean distclean install install-links install-kmod
SUBDIR_TARGETS = $(foreach act,${SUBDIR_ACTIONS},$(patsubst %,%/${act},${SUBDIRS}))


include ../Makefile.conf

.PHONY: ${SUBDIR_ACTIONS} ${SUBDIR_TARGETS}

.DEFAULT: all

${SUBDIR_ACTIONS}: %: do-%

$(SUBDIR_ACTIONS:%=do-%): do-%:
	${MAKE} $(patsubst %,%/$*,${SUBDIRS})

${SUBDIR_TARGETS}:
	${MAKE} -C $(dir $@) $(filter-out all,$(notdir $@))

all:
	@echo local $@

clean distclean:
	${MAKE} -C rpm $@

.PHONY: rpm rpms

rpm rpms:
	${MAKE} -C rpm clean rpms

.PHONY: help

help:
	@echo "make all            - build everything"
	@echo "make clean          - cleanup"
	@echo ""
	@echo "make rpms           - build RPM packages"
	@echo ""
	@echo "make install        - install binaries in DEST_BIN (default ${DEST_BIN})"
	@echo "make install-links  - install sym-links in DEST_BIN (default ${DEST_BIN})"
	@echo "make install-kmod   - install kernel modules in KDIR/kernel/misc"

# --- ctags and cscope

.PHONY: tags ctags cscope

tags ctags cscope:
	$(MAKE) -C .. $@
