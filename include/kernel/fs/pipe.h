#pragma once

#include <stdint.h>
#include "fs/vfs.h"

int pipe_create(vfs_file_t** read_file_out, vfs_file_t** write_file_out, int flags);