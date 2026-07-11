# CMakeLists.txt for bishlink

cmake_minimum_required(VERSION 3.12)
project(bishlink C)

set(CMAKE_C_STANDARD 99)
add_definitions(-D_GNU_SOURCE)

# Common sources and include directories
set(COMMON_INCLUDES
	src/common
	deps/nanors
	deps/nanors/deps/obl
	deps/quicly/include
	deps/quicly/deps/picotls/include
	deps/quicly/deps/klib
)

set(COMMON_SOURCES
	src/common/transport_quicly.c
	src/common/fec.c
	deps/nanors/rs.c
	deps/nanors/deps/obl/oblas_common.c
	deps/nanors/deps/obl/oblas_lite.c
	deps/quicly/lib/quicly.c
	deps/quicly/lib/defaults.c
	deps/quicly/lib/frame.c
	deps/quicly/lib/local_cid.c
	deps/quicly/lib/loss.c
	deps/quicly/lib/ranges.c
	deps/quicly/lib/rate.c
	deps/quicly/lib/recvstate.c
	deps/quicly/lib/remote_cid.c
	deps/quicly/lib/sendstate.c
	deps/quicly/lib/sentmap.c
	deps/quicly/lib/streambuf.c
	deps/quicly/lib/cc-reno.c
	deps/quicly/lib/cc-cubic.c
	deps/quicly/lib/cc-pico.c
	deps/quicly/deps/picotls/lib/picotls.c
	deps/quicly/deps/picotls/lib/openssl.c
	deps/quicly/deps/picotls/lib/pembase64.c
	deps/quicly/deps/picotls/lib/hpke.c
	deps/quicly/deps/picotls/lib/asn1.c
)

# Host build target
set(HOST_SOURCES
	src/host/main.c
	src/host/linux/capture_x11.c
	src/host/linux/audio_pulse.c
	src/host/linux/inject_uinput.c
	${COMMON_SOURCES}
)

add_executable(bishlink-host ${HOST_SOURCES})
target_include_directories(bishlink-host PRIVATE src/host ${COMMON_INCLUDES})

# Client build target
set(CLIENT_SOURCES
	src/client/main.c
	src/client/linux/decode_vaapi.c
	${COMMON_SOURCES}
)

add_executable(bishlink-client ${CLIENT_SOURCES})
target_include_directories(bishlink-client PRIVATE src/client ${COMMON_INCLUDES})

# Output build message status
message(STATUS "Configured targets: bishlink-host, bishlink-client")
