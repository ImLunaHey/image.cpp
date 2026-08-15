#ifdef system
#undef system
#endif

int imagecpp_leptonica_system_disabled(const char *command) {
    (void)command;
    return -1;
}
