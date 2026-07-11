# Makefile for bishlink

CC = gcc
ARCH = $(shell uname -m)

# Default CFLAGS for our code
CFLAGS_COMMON = -Wvla -Wall -Wextra -std=c11 -g -D_GNU_SOURCE -D_DEFAULT_SOURCE -DPATHFLOW_ARENA_SIZE=65536

# Include paths: use -isystem for quicly and picotls to suppress warning headers
INCLUDES = -Isrc/common -Ideps/nanors -Ideps/nanors/deps/obl -Ideps/nanorq/include -Ideps/nanorq/deps -Ideps/pathflow -Ideps/pathflow/solvers -isystem deps/quicly/include -isystem deps/quicly/deps/picotls/include -Ideps/quicly/deps/klib

LDFLAGS = -lpthread -lrt -ldl -lm -lcrypto -lssl

QUICLY_SRCS = deps/quicly/lib/quicly.c \
              deps/quicly/lib/defaults.c \
              deps/quicly/lib/frame.c \
              deps/quicly/lib/local_cid.c \
              deps/quicly/lib/loss.c \
              deps/quicly/lib/ranges.c \
              deps/quicly/lib/rate.c \
              deps/quicly/lib/recvstate.c \
              deps/quicly/lib/remote_cid.c \
              deps/quicly/lib/sendstate.c \
              deps/quicly/lib/sentmap.c \
              deps/quicly/lib/streambuf.c \
              deps/quicly/lib/cc-reno.c \
              deps/quicly/lib/cc-cubic.c \
              deps/quicly/lib/cc-pico.c \
              deps/quicly/deps/picotls/lib/picotls.c \
              deps/quicly/deps/picotls/lib/openssl.c \
              deps/quicly/deps/picotls/lib/pembase64.c \
              deps/quicly/deps/picotls/lib/hpke.c \
              deps/quicly/deps/picotls/lib/asn1.c

NANORQ_SRCS = deps/nanorq/lib/chooser.c \
              deps/nanorq/lib/nanorq_core.c \
              deps/nanorq/lib/ops.c \
              deps/nanorq/lib/params.c \
              deps/nanorq/lib/precode.c \
              deps/nanorq/lib/rand.c \
              deps/nanorq/lib/tuple.c \
              deps/nanorq/lib/uvec.c

PATHFLOW_SRCS = deps/pathflow/pathflow.c

IFMON_SRCS = src/common/ifmon.c

QUICLY_OBJS = $(QUICLY_SRCS:.c=.o)
NANORQ_OBJS = $(NANORQ_SRCS:.c=.o)
PATHFLOW_OBJS = $(PATHFLOW_SRCS:.c=.o)
IFMON_OBJS = $(IFMON_SRCS:.c=.o)

COMMON_OBJS = src/common/data_uds.o \
              src/common/transport_quicly.o \
              src/common/fec.o \
              deps/nanors/rs.o \
              deps/nanors/deps/obl/oblas_common.o \
              deps/nanors/deps/obl/oblas_lite.o \
              $(QUICLY_OBJS) \
              $(NANORQ_OBJS) \
              $(PATHFLOW_OBJS) \
              $(IFMON_OBJS)

HOST_OBJS = src/host/main.o \
            src/host/linux/capture_x11.o \
            src/host/linux/audio_pulse.o \
            src/host/linux/inject_uinput.o \
            $(COMMON_OBJS)

CLIENT_OBJS = src/client/main.o \
              $(COMMON_OBJS)

FEC_OBJS = src/common/fec.o \
           deps/nanors/rs.o \
           deps/nanors/deps/obl/oblas_common.o \
           deps/nanors/deps/obl/oblas_lite.o \
           $(NANORQ_OBJS) \
           $(PATHFLOW_OBJS)

# Target-specific includes for host-related code
t/00util/test_uds.o: INCLUDES += -Isrc/host
src/host/linux/capture_x11.o: INCLUDES += -Isrc/host
src/host/linux/inject_uinput.o: INCLUDES += -Isrc/host
src/host/linux/audio_pulse.o: INCLUDES += -Isrc/host
src/host/main.o: INCLUDES += -Isrc/host

# Rules to compile our source files with full warnings
src/%.o: src/%.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) $(CFLAGS) -c $< -o $@

examples/%.o: examples/%.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) $(CFLAGS) -c $< -o $@

t/%.o: t/%.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) $(CFLAGS) -c $< -o $@

# Rule to compile third-party deps and suppress all warnings with -w
deps/%.o: deps/%.c
	$(CC) $(INCLUDES) $(CFLAGS) -w -c $< -o $@

all: patch-quicly bishlink-host bishlink-client bishlink-inputd bishlink-videod bishlink-audiod bishlink-tund

patch-quicly:

unpatch-quicly:

bishlink-host: patch-quicly $(HOST_OBJS)
	$(CC) -o $@ $(HOST_OBJS) $(LDFLAGS)

bishlink-client: patch-quicly $(CLIENT_OBJS)
	$(CC) -o $@ $(CLIENT_OBJS) $(LDFLAGS)

bishlink-inputd: src/host/linux/inputd.c src/common/input_event.h
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -o $@ src/host/linux/inputd.c

bishlink-videod: src/host/linux/videod.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -o $@ src/host/linux/videod.c -lX11 -lXext

bishlink-audiod: src/host/linux/audiod.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -o $@ src/host/linux/audiod.c -lpulse-simple -lpulse -lm

bishlink-tund: src/host/linux/tund.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -o $@ src/host/linux/tund.c

t/00util/test_fec: t/00util/test_fec.o $(FEC_OBJS)
	$(CC) -o $@ t/00util/test_fec.o $(FEC_OBJS) $(LDFLAGS)

t/00util/test_transport: patch-quicly t/00util/test_transport.o $(COMMON_OBJS)
	$(CC) -o $@ t/00util/test_transport.o $(COMMON_OBJS) $(LDFLAGS)

examples/video_latency_benchmark: patch-quicly examples/video_latency_benchmark.o $(COMMON_OBJS)
	$(CC) -o $@ examples/video_latency_benchmark.o $(COMMON_OBJS) $(LDFLAGS)

t/00util/test_uds: t/00util/test_uds.o src/host/linux/capture_x11.o src/host/linux/inject_uinput.o
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -Isrc/host -o $@ t/00util/test_uds.o src/host/linux/capture_x11.o src/host/linux/inject_uinput.o $(LDFLAGS)

t/00util/test_audio: patch-quicly t/00util/test_audio.o $(COMMON_OBJS)
	$(CC) -o $@ t/00util/test_audio.o $(COMMON_OBJS) $(LDFLAGS)

t/00util/test_inputd: t/00util/test_inputd.c src/common/input_event.h
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -o $@ t/00util/test_inputd.c

t/00util/test_videod: t/00util/test_videod.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -o $@ t/00util/test_videod.c

t/00util/test_audiod: t/00util/test_audiod.c
	$(CC) $(CFLAGS_COMMON) $(INCLUDES) -o $@ t/00util/test_audiod.c

t/00util/test_tund: patch-quicly t/00util/test_tund.o $(COMMON_OBJS)
	$(CC) -o $@ t/00util/test_tund.o $(COMMON_OBJS) $(LDFLAGS)

t/00util/test_benchmark: patch-quicly t/00util/test_benchmark.o $(COMMON_OBJS)
	$(CC) -o $@ t/00util/test_benchmark.o $(COMMON_OBJS) $(LDFLAGS)

t/00util/test_tc_benchmark: patch-quicly t/00util/test_tc_benchmark.o $(COMMON_OBJS)
	$(CC) -o $@ t/00util/test_tc_benchmark.o $(COMMON_OBJS) $(LDFLAGS)

t/00util/test_multipath: patch-quicly t/00util/test_multipath.o $(COMMON_OBJS)
	$(CC) -o $@ t/00util/test_multipath.o $(COMMON_OBJS) $(LDFLAGS)

benchmark: t/00util/test_benchmark
	./t/00util/test_benchmark

clean: unpatch-quicly
	rm -f bishlink-host bishlink-client bishlink-inputd bishlink-videod bishlink-audiod bishlink-tund t/00util/test_fec t/00util/test_transport t/00util/test_uds t/00util/test_audio t/00util/test_inputd t/00util/test_videod t/00util/test_audiod t/00util/test_tund t/00util/test_multipath t/00util/test_benchmark t/00util/test_tc_benchmark examples/video_latency_benchmark
	find src deps t examples -name "*.o" -delete

check: patch-quicly bishlink-inputd bishlink-videod bishlink-audiod bishlink-tund t/00util/test_fec t/00util/test_transport t/00util/test_uds t/00util/test_audio t/00util/test_inputd t/00util/test_videod t/00util/test_audiod t/00util/test_tund t/00util/test_multipath
	prove -I. -v t/*.t

indent:
	clang-format -style=LLVM -i src/common/*.c src/common/*.h src/host/*.c src/host/linux/*.c src/client/*.c examples/*.c t/00util/*.c

.PHONY: all clean check patch-quicly unpatch-quicly benchmark indent
