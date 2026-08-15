#include <errno.h>
#include <stdio.h>

FILE *imagecpp_sam3_disabled_popen(const char *command, const char *mode) {
    (void) command;
    (void) mode;
    errno = ENOSYS;
    return NULL;
}

int imagecpp_sam3_disabled_pclose(FILE *stream) {
    (void) stream;
    errno = ENOSYS;
    return -1;
}
