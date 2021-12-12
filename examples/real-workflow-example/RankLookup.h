//
// Created by anon on 12.12.21.
//

#ifndef WRENCH_RANKLOOKUP_H
#define WRENCH_RANKLOOKUP_H


#include "rapidcsv.h"

class RankLookup : rapidcsv::Document {
public:
    RankLookup(rapidcsv::Document &pDoc);

    template<typename T>
    RankLookup filter(const std::string &pColumnName, const T &pValue);

private:
    rapidcsv::Document mDoc;
};


#endif //WRENCH_RANKLOOKUP_H
