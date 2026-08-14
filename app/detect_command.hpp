#ifndef IMAGECPP_APP_DETECT_COMMAND_HPP
#define IMAGECPP_APP_DETECT_COMMAND_HPP

#include <iosfwd>

void print_detect_command_usage(std::ostream &output);
int detect_image_command(int argc, char **argv);
int ground_image_command(int argc, char **argv);

#endif
