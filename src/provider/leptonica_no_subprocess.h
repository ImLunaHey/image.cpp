#ifndef IMAGECPP_LEPTONICA_NO_SUBPROCESS_H
#define IMAGECPP_LEPTONICA_NO_SUBPROCESS_H

#include <stdlib.h>

#ifdef _WIN32
#include <process.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

int imagecpp_leptonica_system_disabled(const char *command);

#ifdef __cplusplus
}
#endif

#define system imagecpp_leptonica_system_disabled

#endif
