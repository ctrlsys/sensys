/*
 * Copyright (c) 2016 Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef SENSOR_INTERFACE_H
#define SENSOR_INTERFACE_H

class sensorInterface
{
public:
    virtual int init(void);
    virtual int finalize(void);
};

#endif
