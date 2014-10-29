OUTPUT = kabi.h

.PHONY: all ${OUTPUT} clean distclean
all: ${OUTPUT}

${OUTPUT}:
	./kabi -g \
		$(if ${KDIR},-k ${KDIR}) \
		$(if ${KARCH},-a ${KARCH}) \
		$(if ${SYMBOLS},-s "${SYMBOLS}") \
		> $@

clean:
	-rm -f ${OUTPUT} *.h *~

distclean: clean
	-rm -rf tmp
