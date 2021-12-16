//
// Created by anon on 12.12.21.
//

#ifndef WRENCH_RANKLOOKUP_H
#define WRENCH_RANKLOOKUP_H


#include "rapidcsv.h"

class RankLookup {
public:
    RankLookup(rapidcsv::Document &pDoc) {
        this->mDoc = pDoc;
    };

    RankLookup filter(const std::string &pColumnName, int pValue) {
        rapidcsv::Document cp = *(&this->mDoc);
        for (int i = cp.GetRowCount() - 1; i >= 0; i--) {
            int a = round(cp.GetCell<double>(pColumnName, i));
            if (a != pValue) {
                cp.RemoveRow(i);
            }
        }
        return RankLookup(cp);
    };

    RankLookup filter(const std::string &pColumnName, const std::string &pValue) {
        rapidcsv::Document cp = *(&this->mDoc);
        for (int i = cp.GetRowCount() - 1; i >= 0; i--) {
            std::string a = cp.GetCell<std::string>(pColumnName, i);
            if (a != pValue) {
                cp.RemoveRow(i);
            }
        }
        return RankLookup(cp);
    };

   /* template<typename T>
    RankLookup filter(const std::string &pColumnName, T pValue) {
        rapidcsv::Document cp = *(&this->mDoc);
        for (int i = cp.GetRowCount() - 1; i >= 0; i--) {
            T a = cp.GetCell<T>(pColumnName, i);
            if (a != pValue) {
                cp.RemoveRow(i);
            }
        }
        return RankLookup(cp);
    };*/

private:
    rapidcsv::Document mDoc;
};


#endif //WRENCH_RANKLOOKUP_H
