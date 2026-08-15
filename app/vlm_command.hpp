#ifndef IMAGECPP_APP_VLM_COMMAND_HPP
#define IMAGECPP_APP_VLM_COMMAND_HPP

#include <iosfwd>

int caption_image_command(int argc, char **argv);
int ask_image_command(int argc, char **argv);
void print_vlm_command_usage(std::ostream &output);

#endif
