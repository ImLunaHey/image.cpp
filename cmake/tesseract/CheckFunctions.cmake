function(check_leptonica_tiff_support)
    # image.cpp deliberately builds Leptonica without TIFF. Tesseract's
    # configure-time probe cannot link an add_subdirectory target from its
    # isolated try-compile project, so report the known build configuration.
    set(LEPT_TIFF_RESULT 1 PARENT_SCOPE)
    set(LEPT_TIFF_COMPILE_SUCCESS TRUE PARENT_SCOPE)
endfunction()
