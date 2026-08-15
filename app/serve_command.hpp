#ifndef IMAGECPP_APP_SERVE_COMMAND_HPP
#define IMAGECPP_APP_SERVE_COMMAND_HPP

#include <iosfwd>

int serve_command(int argc, char **argv);
void print_serve_command_usage(std::ostream &output);

#endif
