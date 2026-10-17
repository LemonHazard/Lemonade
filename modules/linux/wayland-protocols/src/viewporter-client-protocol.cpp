#ifdef OS_LINUX

// Compiles the protocol code that wayland-scanner generated as C (viewporter-protocol.c)
// into the unity build: extern "C" gives its wl_interface symbols the C linkage the
// generated headers declare them with.
//
// Generated with wayland-scanner 1.23.1 from wayland-protocols 1.44 (stable/viewporter/viewporter.xml):
//   wayland-scanner client-header viewporter.xml viewporter-client-protocol.h
//   wayland-scanner private-code  viewporter.xml viewporter-protocol.c
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes" // WL_PRIVATE's visibility is moot in an executable

extern "C"
{
#include "viewporter-protocol.c"
}

#pragma GCC diagnostic pop

#endif // OS_LINUX
