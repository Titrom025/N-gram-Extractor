//
// Created by Roman Titkov on 23.03.2022.
//

#ifndef LABS_WORDCONTEXT_H
#define LABS_WORDCONTEXT_H

#include <utility>
#include <vector>
#include "dictionary.h"
class WordContext {
public:
    wstring normalizedForm;
    vector<Word*> words;
    int totalEntryCount = 0;
    double tfidf = 0;
    set <string> textEntry;

    WordContext(const wstring& normalizedForm, vector<Word*> words) {
        this->normalizedForm = normalizedForm;
        this->words = std::move(words);
    }
};
#endif //LABS_WORDCONTEXT_H
