/*
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef ORCM_MCA_ANALYTICS_INTERFACE_ANALYTICS_INTERFACE_H_
#define ORCM_MCA_ANALYTICS_INTERFACE_ANALYTICS_INTERFACE_H_

#include "orcm/common/dataContainer.hpp"
#include <list>

struct Event {
    const std::string severity;
    const std::string action;
    const std::string msg;
    const double value;
    const std::string unit;
};

struct DataSet {
    DataSet(dataContainer& _results, std::list<Event>& _events) :
            results(_results), events(_events) {
    }
    dataContainer attributes;
    dataContainer key;
    dataContainer non_compute;
    dataContainer compute;
    dataContainer &results;
    std::list<Event> &events;
};

class Analytics {
    public:
        virtual ~Analytics() {};
        virtual int analyze(DataSet& data_set) = 0;
};

#endif /* ORCM_MCA_ANALYTICS_INTERFACE_ANALYTICS_INTERFACE_H_ */
