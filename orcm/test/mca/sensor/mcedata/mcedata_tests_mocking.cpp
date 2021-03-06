/*
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "mcedata_tests_mocking.h"

#include <iostream>
using namespace std;

mcedata_tests_mocking mcedata_mocking;


extern "C" { // Mocking must use correct "C" linkages
    DIR* __wrap_opendir(const char* dirname)
    {
        if(NULL == mcedata_mocking.opendir_callback) {
            return __real_opendir(dirname);
        } else {
            return mcedata_mocking.opendir_callback(dirname);
        }
    }

    int __wrap_closedir(DIR* dir_fd)
    {
        if(NULL == mcedata_mocking.closedir_callback) {
            return __real_closedir(dir_fd);
        } else {
            return mcedata_mocking.closedir_callback(dir_fd);
        }
    }

    struct dirent* __wrap_readdir(DIR* dir_fd)
    {
        if(NULL == mcedata_mocking.readdir_callback) {
            return __real_readdir(dir_fd);
        } else {
            return mcedata_mocking.readdir_callback(dir_fd);
        }
    }

    int __wrap_fseek(FILE* fd, long offset, int based_from)
    {
        if(NULL == mcedata_mocking.fseek_callback) {
            return __real_fseek(fd, offset, based_from);
        } else {
            return mcedata_mocking.fseek_callback(fd, offset, based_from);
        }
    }
} // extern "C"

mcedata_tests_mocking::mcedata_tests_mocking() :
    opendir_callback(NULL), closedir_callback(NULL), readdir_callback(NULL),
    fseek_callback(NULL)
{
}
