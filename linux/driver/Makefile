##
## This Makefile does the global config, and checks
## which part of the build you are doing.
## "Local" make rules are confined to Makefile.local
## "Kbuild" make rules basically just populate

BUILD_OS	?= linux
BUILD_TYPE	?= debug

TD_CONFIG_H	?= config-$(BUILD_OS)-$(BUILD_TYPE).h

CONFIG_BLK_DEV_TERADIMM=m

define CONFIG_IF
$(shell grep -q '#define $(strip $(1))' ${TD_CONFIG_H} && echo $(2) )
endef


# --- select object files

LINUX_OBJS = td_drv.o \
	     td_control.o \
	     td_token_bio.o \
	     td_mapper_linux.o \
	     td_bio_linux.o \
	     td_worker.o \
	     td_devgroup.o \
	     td_block.o \
	     td_scan_bios.o \
	     td_mon.o \
	     td_osdev.o \
	     td_device.o

PROTOCOL_OBJS = td_protocol.o \
	        td_crypto.o \
		td_compat.o

VMWARE_OBJS = td_vmware.o \
	      td_vmkscsi_inquiry.o \
	      td_token_vmk.o \
	      td_bio_vmk.o \
	      td_mapper_vmk.o \
	      td_vmkscsi.o

COMMON_OBJS = td_engine.o \
	      td_mapper.o \
	      td_raid.o \
	      td_command.o \
	      td_eng_teradimm.o \
	      td_eng_mcefree.o \
	      td_eng_conf.o \
	      td_eng_hal.o \
	      td_eng_completion.o \
	      td_monitor.o \
	      td_stash.o \
	      td_biogrp.o \
	      td_token.o \
	      td_ucmd.o \
	      td_ioctl.o \
	      td_util.o \
	      tr_stripe.o \
	      tr_mirror.o\

LINUX_OBJS += $(call CONFIG_IF, CONFIG_TERADIMM_SIMULATOR, td_simulator.o td_sim_td.o td_eng_sim_td.o)

LINUX_OBJS += $(call CONFIG_IF, CONFIG_TERADIMM_SYSFS, td_eng_conf_sysfs.o)
LINUX_OBJS += $(call CONFIG_IF, CONFIG_TERADIMM_ACPI_ASL, td_scan_asl.o)
LINUX_OBJS += $(call CONFIG_IF, CONFIG_TERADIMM_SGIO, td_dev_ata.o)
LINUX_OBJS += $(call CONFIG_IF, CONFIG_TERADIMM_STM, td_eng_stm_td.o)


COMMON_OBJS += $(call CONFIG_IF,CONFIG_TERADIMM_MEGADIMM, td_eng_sim_md.o td_eng_megadimm.o md_token.o md_command.o md_stats.o)
COMMON_OBJS += $(call CONFIG_IF,CONFIG_TERADIMM_MCEFREE_FWSTATUS, td_eng_mcefree.o)

COMMON_OBJS += $(call CONFIG_IF,CONFIG_TERADIMM_INTERNAL_TRAIN, td_uefi_training_sample_code.o)

COMMON_OBJS += $(call CONFIG_IF,CONFIG_TERADIMM_TRACE,td_trace.o)

COMMON_OBJS += $(call CONFIG_IF,CONFIG_TD_HISTOGRAM,td_histogram.o)


STM_DEPS += $(call CONFIG_IF,CONFIG_TERADIMM_STM, Module.symvers)

LINUX_SRCS = ${sort ${LINUX_OBJS:.o=.c}}
COMMON_SRCS = ${sort ${COMMON_OBJS:.o=.c}}
PROTOCOL_SRCS = ${PROTOCOL_OBJS:.o=.c}

ifeq ($(KERNELRELEASE)$(VMWAREDDK),)
# --- local make targets
include Makefile.local

else
ifneq ($(KERNELRELEASE),)
# we are in kbuild!
# M is passed by kbuild and points to linux driver source
TOP     := ${M}/../..
include ${M}/Makefile.kbuild

else
ifneq ($(VMWAREDDK),)

# We are in vmware DDK build harness
TOP     := ${M}/../..
include ${M}/Makefile.vmware

# make 3.80 from SLES10.2 can't seem to handle else ifneq on the same line.
endif
endif
endif		## LOCAL / KERNEL RELEASE / VMWARE DDKnma

## This should be enabled occasionally to verify stuff windows can't handle
## Unfortunately, we can't compile teradimm.mod.c with it
#WARNING_CFLAGS	= -DSUPPRESS_KERNEL_WARNINGS -Wpointer-arith -Werror

EXTRA_CFLAGS	:= ${WARNING_CFLAGS} -Wall \
		-DTERADIMM_VERSION='"${TERADIMM_VERSION}"' \
		-DTERADIMM_REVISION='"${TERADIMM_REVISION}"' \
		-I${TOP}/linux/driver \
		-I${TOP}/common/driver -I${TOP}/common/util -I${TOP}/common/util \
		$(if ${TERADIMM_KABI_H},-include ${TERADIMM_KABI_H},$(error you need to define TERADIMM_KABI_H)) \
		$(if ${TD_CONFIG_H},-include ${TD_CONFIG_H},$(error you need to define TD_CONFIG_H))

