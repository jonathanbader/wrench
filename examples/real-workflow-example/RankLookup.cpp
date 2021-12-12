//
// Created by anon on 12.12.21.
//

#include "RankLookup.h"

RankLookup::RankLookup(rapidcsv::Document &pDoc) {
    this->mDoc = pDoc;
}

template<typename T>
RankLookup RankLookup::filter(const std::string &pColumnName, const T &pValue) {
    RankLookup cp = *this;
    for (auto i = 0; i < cp.GetRowCount(); i++) {
        if (cp.GetCell<T>(pColumnName, i) != *pValue){
            cp.RemoveRow(i);
        }
    }
    return cp;
}
