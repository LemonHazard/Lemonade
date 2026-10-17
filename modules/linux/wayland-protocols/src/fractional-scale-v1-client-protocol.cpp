#ifdef OS_LINUX

// Compiles the protocol code that wayland-scanner generated as C (fractional-scale-v1-protocol.c)
// into the unity build: extern "C" gives its wl_interface symbols the C linkage the
// generated headers declare them with.
//
// Generated with wayland-scanner 1.23.1 from wayland-protocols 1.44 (staging/fractional-scale/fractional-scale-v1.xml):
//   wayland-scanner client-header fractional-scale-v1.xml fractional-scale-v1-client-protocol.h
//   wayland-scanner private-code  fractional-scale-v1.xml fractional-scale-v1-protocol.c
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes" // WL_PRIVATE's visibility is moot in an executable

extern "C"
{
#include "fractional-scale-v1-protocol.c"
}

#pragma GCC diagnostic pop

#endif // OS_LINUX
