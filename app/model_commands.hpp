#ifndef IMAGECPP_APP_MODEL_COMMANDS_HPP
#define IMAGECPP_APP_MODEL_COMMANDS_HPP

#include <iosfwd>

void print_model_command_usage(std::ostream &output);
int generate_image_command(int argc, char **argv);
int edit_image_command(int argc, char **argv);
int upscale_image_command(int argc, char **argv);

#endif
