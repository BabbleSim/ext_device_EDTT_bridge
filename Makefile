# Copyright 2019 Demant A/S
# SPDX-License-Identifier: Apache-2.0

BSIM_BASE_PATH?=$(abspath ../ )
include ${BSIM_BASE_PATH}/common/pre.make.inc

EXE_NAME:=bs_device_EDTT_bridge

SRCS:= src/main.c \
	src/edtt_args.c \
	src/edtt_if.c \
	src/device_if.c

INCLUDES:=-I${libUtilv1_COMP_PATH}/src/ \
          -I${libPhyComv1_COMP_PATH}/src/ \

A_LIBS:=${BSIM_LIBS_DIR}/libUtilv1.a \
        ${BSIM_LIBS_DIR}/libPhyComv1.a \

SO_LIBS:=
DEBUG:=-g
OPT:=
ARCH:=
WARNINGS:=-Wall -pedantic
COVERAGE:=
CFLAGS:=${ARCH} ${DEBUG} ${OPT} ${WARNINGS} -MMD -MP -std=c99 ${INCLUDES}
LDFLAGS:=${ARCH} ${COVERAGE}
CPPFLAGS:=-D_XOPEN_SOURCE=700

include ${BSIM_BASE_PATH}/common/make.device.inc
