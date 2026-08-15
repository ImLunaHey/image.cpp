#ifndef IMAGECPP_APP_OCR_COMMAND_HPP
#define IMAGECPP_APP_OCR_COMMAND_HPP

#include <iosfwd>

void print_ocr_command_usage(std::ostream &output);
int ocr_image_command(int argc, char **argv);

#endif
