#include <errno.h>
#include <sys/types.h>

pid_t imagecpp_ggml_fork_disabled(void);
int imagecpp_ggml_execlp_disabled(const char *file, const char *argument, ...);

pid_t imagecpp_ggml_fork_disabled(void) {
    errno = ENOSYS;
    return (pid_t)-1;
}

int imagecpp_ggml_execlp_disabled(const char *file, const char *argument, ...) {
    (void)file;
    (void)argument;
    errno = ENOSYS;
    return -1;
}
