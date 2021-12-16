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

    /**
     * @warning see this->_toMap
     */
    template<typename K, typename V>
    std::map<K, V> toMap(std::string col1, std::string col2) {
        return this->_toMap<K, V>(this->mDoc.GetColumn<K>(col1), this->mDoc.GetColumn<V>(col2));
    }

private:
    rapidcsv::Document mDoc;

    /**
     * @warning if there are duplicates in the keys in vector a, the resulting map will have the last matching value from vector b under the key from a.
     */
    template<typename K, typename V>
    static std::map<K, V> _toMap(std::vector<K> a, std::vector<V> b) {
        assert(a.size() == b.size());
        std::map<K, V> m;
        for (size_t i = 0; i < a.size(); ++i) {
            m[a[i]] = b[i];
        }
        return m;
    }
};


#endif //WRENCH_RANKLOOKUP_H
