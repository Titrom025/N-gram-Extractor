#include <iostream>
#include <string>
#include <vector>
#include "filesystem"
#include <codecvt>
#include <fstream>
#include <sys/mman.h>

#include "dictionary.h"
#include "filemap.h"
#include "WordContext.h"

using namespace std;
using recursive_directory_iterator = std::__fs::filesystem::recursive_directory_iterator;

int COUNT_THRESHOLD = 2;
float STABILITY = 0.1;

unordered_map <wstring, vector<WordContext*>> leftExt;
unordered_map <wstring, vector<WordContext*>> rightExt;

vector<string> getFilesFromDir(const string& dirPath) {
    vector<string> files;
    for (const auto& dirEntry : recursive_directory_iterator(dirPath))
        if (!(dirEntry.path().filename() == ".DS_Store"))
            files.push_back(dirEntry.path());
    return files;
}

WordContext* processContext(const vector<wstring>& stringContext, int windowSize,
                            unordered_map <wstring, vector<Word*>> *dictionary,
                            unordered_map <wstring, WordContext*> *NGrams
                            ) {
    auto &f = std::use_facet<std::ctype<wchar_t>>(std::locale());

    vector<Word *> contextVector;

    for (int i = 0; i < windowSize; i++) {
        wstring contextWord = stringContext.at(i);
        f.toupper(&contextWord[0], &contextWord[0] + contextWord.size());

        if (dictionary->find(contextWord) == dictionary->end()) {
            Word *newWord = new Word();
            newWord->word = contextWord;
            newWord->partOfSpeech = L"UNKW";
            dictionary->emplace(newWord->word, vector<Word *>{newWord});
        }

        vector<Word *> &words = dictionary->at(contextWord);
        contextVector.push_back(words.at(0));
    }

    wstring normalizedForm;
    wstring leftNormalizedExt;
    wstring rightNormalizedExt;

    for (int i = 0; i < contextVector.size(); i++) {
        Word* word = contextVector[i];
        if (i == 0) {
            rightNormalizedExt += word->word + L" ";
        } else if (i == contextVector.size() - 1) {
            leftNormalizedExt += word->word + L" ";
        } else {
            leftNormalizedExt += word->word + L" ";
            rightNormalizedExt += word->word + L" ";
        }
        normalizedForm += word->word + L" ";
    }

    leftNormalizedExt.pop_back();
    rightNormalizedExt.pop_back();
    normalizedForm.pop_back();

    if (windowSize == 2 ||
        NGrams->find(leftNormalizedExt) != NGrams->end() ||
        NGrams->find(rightNormalizedExt) != NGrams->end()) {

        auto *wc = new WordContext(normalizedForm,contextVector);
        if (leftExt.find(leftNormalizedExt) == leftExt.end())
            leftExt.emplace(leftNormalizedExt, vector<WordContext *>{});
        if (rightExt.find(rightNormalizedExt) == rightExt.end())
            rightExt.emplace(rightNormalizedExt, vector<WordContext *>{});
        vector<WordContext*> &wordContextsLeft = leftExt.at(leftNormalizedExt);
        vector<WordContext*> &wordContextsRight = rightExt.at(rightNormalizedExt);
        wordContextsLeft.push_back(wc);
        wordContextsRight.push_back(wc);
        return wc;
    } else {
        return nullptr;
    }
}

void addContextToSet(WordContext* context, unordered_map <wstring, WordContext*>* NGramms, const string& filePath) {
    if (NGramms->find(context->normalizedForm) == NGramms->end())
        NGramms->emplace(context->normalizedForm, context);

    auto &ngram= NGramms->at(context->normalizedForm);
    ngram->totalEntryCount++;
    ngram->textEntry.insert(filePath);
}

void handleWord(const wstring& wordStr,
                unordered_map <wstring, vector<Word*>> *dictionary,
                vector<wstring>* leftStringNGrams,
                unordered_map <wstring, WordContext*> *NGrams,
                int windowSize,
                const string& filePath) {
    for (int i = windowSize; i <= windowSize + 1; i++) {
        if (i <= leftStringNGrams->size()) {
            WordContext* phraseContext = processContext(*leftStringNGrams, i, dictionary, NGrams);
            if (phraseContext != nullptr)
                addContextToSet(phraseContext, NGrams, filePath);
        }
    }
    if (leftStringNGrams->size() == (windowSize + 1)) {
        leftStringNGrams->erase(leftStringNGrams->begin());
    }

    leftStringNGrams->push_back(wordStr);
//    wcout << endl;
}

void handleFile(const string& filePath, unordered_map <wstring, vector<Word*>> *dictionary,
                unordered_map <wstring, WordContext*> *NGrams, int windowSize) {

    size_t length;
    auto filePtr = map_file(filePath.c_str(), length);
    auto firstChar = filePtr;
    auto lastChar = filePtr + length;

    vector<wstring> leftStringNGrams;

    while (filePtr && filePtr != lastChar) {
        auto stringBegin = filePtr;
        filePtr = static_cast<char *>(memchr(filePtr, '\n', lastChar - filePtr));

        wstring line = wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(stringBegin, filePtr);
        wstring const delims {L" :;.,!?()" };

        size_t beg, pos = 0;
        while ((beg = line.find_first_not_of(delims, pos)) != string::npos) {
            pos = line.find_first_of(delims, beg + 1);
            wstring wordStr = line.substr(beg, pos - beg);

            handleWord(wordStr, dictionary,
                       &leftStringNGrams,
                       NGrams, windowSize, filePath);

            if (line[pos] == L'.' || line[pos] == L',' ||
                line[pos] == L'!' || line[pos] == L'?') {
                wstring punct;
                punct.push_back(line[pos]);
                handleWord(punct, dictionary,
                           &leftStringNGrams,
                           NGrams, windowSize, filePath);
            }
        }
        if (filePtr)
            filePtr++;
    }

    munmap(firstChar, length);
}

void findNGrams(const string& dictPath, const string& corpusPath,
                const string& outputFile) {
    auto dictionary = initDictionary(dictPath);
    vector<string> files = getFilesFromDir(corpusPath);

    unordered_map<wstring, WordContext *> NGrams;
    vector<WordContext *> stableNGrams;
    cout << "Start handling files\n";

    int windowSize = 2;
    while (true) {
        wcout << "Window size: " << windowSize << endl;
        for (int i = 0; i < 1; i++) {
            for (const string &file: files)
                handleFile(file, &dictionary, &NGrams, windowSize);

            unordered_map<wstring, WordContext *> thresholdedNGrams;

            for (auto &pair: NGrams) {
                if (pair.second->totalEntryCount >= COUNT_THRESHOLD) {
                    try {
                        if (pair.second->words.size() != windowSize)
                            continue;

                        int extensionMax = 0;

                        vector<WordContext *> leftWordContexts = leftExt.at(pair.second->normalizedForm);
                        vector<WordContext *> rightWordContexts = rightExt.at(pair.second->normalizedForm);

                        for (WordContext *wc: leftWordContexts)
                            if (wc->totalEntryCount > extensionMax)
                                extensionMax = wc->totalEntryCount;

                        for (WordContext *wc: rightWordContexts)
                            if (wc->totalEntryCount > extensionMax)
                                extensionMax = wc->totalEntryCount;

                        pair.second->stability = (extensionMax / (float) pair.second->totalEntryCount);
                        if (pair.second->stability <= STABILITY)
                            stableNGrams.push_back(pair.second);

                        thresholdedNGrams.emplace(pair);

                    } catch (const out_of_range &) {}
                }
            }
            wcout << NGrams.size() << " | " << thresholdedNGrams.size() << endl;
            NGrams = thresholdedNGrams;
        }
//        break;
        windowSize++;
    }

    struct {
        bool operator()(const WordContext* a, const WordContext* b) const { return a->stability < b->stability; }
    } comp;
    sort(stableNGrams.begin(), stableNGrams.end(), comp);

    ofstream outFile (outputFile.c_str());

    cout << "\n";
    for (const WordContext* context : stableNGrams) {
        string line = wstring_convert<codecvt_utf8<wchar_t>>().to_bytes(context->normalizedForm);
//        outFile << "<\"" << line << "\", " << context->totalEntryCount << ">\n";
//
//        wcout << "<\"" << context->normalizedForm
//        << "\", Total entries: " << context->totalEntryCount
//        << ", TF: " << context->textEntry.size()
//        << ", Stability: " << context->stability
//        << ">\n";
    }

    outFile.close();
    cout << "End handling files\n";
}

#include <chrono>
#include <thread>

int main() {
    STABILITY = 0.1;

    locale::global(locale("ru_RU.UTF-8"));
    wcout.imbue(locale("ru_RU.UTF-8"));

    string dictPath = "dict_opcorpora_clear.txt";
    string corpusPath = "/Users/titrom/Desktop/Computational Linguistics/Articles";
    string outputFile = "ngrams.txt";

    findNGrams(dictPath, corpusPath, outputFile);
    std::this_thread::sleep_for(std::chrono::milliseconds(100000));
    return 0;
}
