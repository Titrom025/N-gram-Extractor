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
#include <chrono>

using namespace std;
using recursive_directory_iterator = std::__fs::filesystem::recursive_directory_iterator;

int OUTPUT_COUNT = 50;
int COUNT_THRESHOLD = 2;
float STABILITY = 0.1;

unordered_map <wstring, vector<WordContext*>> leftExt;
unordered_map <wstring, vector<WordContext*>> rightExt;
unordered_map <string, vector<int>> newNGramsPositions;
unordered_map <string, vector<int>> oldNGramsPositions;

vector<string> getFilesFromDir(const string& dirPath) {
    vector<string> files;
    for (const auto& dirEntry : recursive_directory_iterator(dirPath))
        if (!(dirEntry.path().filename() == ".DS_Store"))
            files.push_back(dirEntry.path());
    return files;
}

WordContext* processContext(const vector<Word*>& wordVector, int windowSize,
                            unordered_map <wstring, WordContext*> *NGrams,
                            bool isNotExt) {
    wstring normalizedForm;
    normalizedForm.reserve(windowSize * 8);
    wstring leftNormalizedExt;
    leftNormalizedExt.reserve(windowSize * 8);
    wstring rightNormalizedExt;
    rightNormalizedExt.reserve(windowSize * 8);

    vector<Word*> contextVector;
    for (int i = 0; i < windowSize; i++) {
        Word* word = wordVector[i];
        contextVector.push_back(word);
        if (i == 0) {
            rightNormalizedExt += word->word + L" ";
        } else if (i == wordVector.size() - 1) {
            leftNormalizedExt += word->word + L" ";
        } else {
            leftNormalizedExt += word->word + L" ";
            rightNormalizedExt += word->word + L" ";
        }
        normalizedForm += word->word + L' ';
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

void handleWord(Word* word,
                vector<Word*>* leftWordContext,
                unordered_map <wstring, WordContext*> *NGrams,
                int windowSize, const string& filePath,
                const int wordPosition) {
    for (int i = windowSize; i <= windowSize + 1; i++) {
        if (i <= leftWordContext->size()) {
            bool isCloseToNGramm = false;
            if (windowSize > 2) {
                if (oldNGramsPositions.find(filePath) == newNGramsPositions.end())
                    continue;
                vector<int> positions = oldNGramsPositions.at(filePath);
                for (int position : positions)
                    if (abs(wordPosition - position) <= 0) {
                        isCloseToNGramm = true;
                        break;
                    }
            } else
                isCloseToNGramm = true;

            if (!isCloseToNGramm)
                continue;

            WordContext* phraseContext = processContext(*leftWordContext, i, NGrams, i == windowSize);
            if (phraseContext != nullptr) {
                addContextToSet(phraseContext, NGrams, filePath);
                if (newNGramsPositions.find(filePath) == newNGramsPositions.end())
                    newNGramsPositions.emplace(filePath, vector<int>{});
                newNGramsPositions.at(filePath).push_back(wordPosition);
            }
        }
    }
    if (leftWordContext->size() == (windowSize + 1)) {
        leftWordContext->erase(leftWordContext->begin());
    }

    leftWordContext->push_back(word);
}

void handleFile(const string& filepath, const vector<Word*>& fileContent, unordered_map <wstring, vector<Word*>> *dictionary,
                unordered_map <wstring, WordContext*> *NGrams, int windowSize) {
    vector<Word*> leftWordContext;

    int wordPosition = 0;
    for (Word* word : fileContent) {
        handleWord(word,&leftWordContext,
                   NGrams, windowSize, filepath, wordPosition);
        wordPosition++;
    }
}

unordered_map <string, vector<Word*>> readAllTexts(const vector<string>& files,
                                                   unordered_map <wstring, vector<Word*>> *dictionary) {
    auto &f = std::use_facet<std::ctype<wchar_t>>(std::locale());
    unordered_map <string, vector<Word*>> fileContents;
    vector<wstring> leftStringNGrams;

    for (const auto& filepath : files) {
        fileContents.emplace(filepath, vector<Word *>{});
        vector<Word*> fileContent = fileContents.at(filepath);

        size_t length;
        auto filePtr = map_file(filepath.c_str(), length);
        auto firstChar = filePtr;
        auto lastChar = filePtr + length;

        while (filePtr && filePtr != lastChar) {
            auto stringBegin = filePtr;
            filePtr = static_cast<char *>(memchr(filePtr, '\n', lastChar - filePtr));

            wstring line = wstring_convert<codecvt_utf8<wchar_t>>().from_bytes(stringBegin, filePtr);
            wstring const delims{L" :;.,!?() \r\n"};

            size_t beg, pos = 0;
            while ((beg = line.find_first_not_of(delims, pos)) != string::npos) {
                pos = line.find_first_of(delims, beg + 1);
                wstring wordStr = line.substr(beg, pos - beg);

                f.toupper(&wordStr[0], &wordStr[0] + wordStr.size());

                if (dictionary->find(wordStr) == dictionary->end()) {
                    Word *newWord = new Word();
                    newWord->word = wordStr;
                    newWord->partOfSpeech = L"UNKW";
                    dictionary->emplace(newWord->word, vector<Word *>{newWord});
                }

                vector<Word *> &words = dictionary->at(wordStr);
                fileContent.push_back(words.at(0));

                if (line[pos] == L'.' || line[pos] == L',' ||
                    line[pos] == L'!' || line[pos] == L'?') {
                    wstring punct;
                    punct.push_back(line[pos]);
                    if (dictionary->find(punct) == dictionary->end()) {
                        Word *newWord = new Word();
                        newWord->word = punct;
                        newWord->partOfSpeech = L"UNKW";
                        dictionary->emplace(newWord->word, vector<Word *>{newWord});
                    }

                    vector<Word *> &wordsPunct = dictionary->at(punct);
                    fileContent.push_back(wordsPunct.at(0));
                }
            }
            if (filePtr)
                filePtr++;
        }
        munmap(firstChar, length);
        fileContents.at(filepath) = fileContent;
    }
    return fileContents;
}

void findNGrams(const string& dictPath, const string& corpusPath,
                const string& outputFile) {
    auto dictionary = initDictionary(dictPath);
    vector<string> files = getFilesFromDir(corpusPath);

    unordered_map<wstring, WordContext *> NGrams;
    vector<WordContext *> stableNGrams;
    cout << "Start handling files\n";
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    unordered_map <string, vector<Word*>> filesContent = readAllTexts(files, &dictionary);

    int windowSize = 2;
    while (true) {
        for (const string &filepath: files) {
            handleFile(filepath, filesContent.at(filepath), &dictionary, &NGrams, windowSize);
        }

        unordered_map<wstring, WordContext *> thresholdedNGrams;

        for (auto &pair: NGrams) {
            if (pair.second->totalEntryCount >= COUNT_THRESHOLD) {
                try {
                    if (pair.second->words.size() != windowSize)
                        continue;

                    int extensionMax = 0;

                    vector<WordContext *> leftWordContexts = leftExt.at(pair.second->normalizedForm);
                    vector<WordContext *> rightWordContexts = rightExt.at(pair.second->normalizedForm);

                    for (WordContext *wc: leftWordContexts) {
                        if ((wc->words.size() == pair.second->words.size() + 1) && wc->totalEntryCount > extensionMax)
                            extensionMax = wc->totalEntryCount;
                    }

                    for (WordContext *wc: rightWordContexts) {
                        if ((wc->words.size() == pair.second->words.size() + 1) && wc->totalEntryCount > extensionMax)
                            extensionMax = wc->totalEntryCount;
                    }

                    pair.second->stability = ((float) extensionMax / (float) pair.second->totalEntryCount);
                    if (pair.second->stability <= STABILITY)
                        stableNGrams.push_back(pair.second);

                    thresholdedNGrams.emplace(pair);
                } catch (const out_of_range &) {}
            }
        }
        wcout << "Window size: " << windowSize << ", repeated ngrams: " << thresholdedNGrams.size() << endl;
        NGrams = thresholdedNGrams;

        oldNGramsPositions = newNGramsPositions;
        newNGramsPositions.clear();
        windowSize++;
        if (NGrams.empty())
            break;
    }

    struct {
        bool operator()(const WordContext* a, const WordContext* b) const { return a->stability < b->stability; }
    } comp;
    sort(stableNGrams.begin(), stableNGrams.end(), comp);

    ofstream outFile (outputFile.c_str());

    cout << "\n";
    int printCount = 0;
    for (const WordContext* context : stableNGrams) {
        string line = wstring_convert<codecvt_utf8<wchar_t>>().to_bytes(context->normalizedForm);
        outFile << "<\"" << line << "\", " << context->totalEntryCount << ">\n";

        wcout << "<\"" << context->normalizedForm
        << "\", Total entries: " << context->totalEntryCount
        << ", TF: " << context->textEntry.size()
        << ", Stability: " << context->stability
        << ">\n";

        printCount++;
        if (printCount >= OUTPUT_COUNT)
            break;
    }

    outFile.close();
    cout << "End handling files\n";
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::seconds>(end - begin).count() << "[s]" << std::endl;
}

int main() {
    OUTPUT_COUNT = 50;
    STABILITY = 0.5;

    locale::global(locale("ru_RU.UTF-8"));
    wcout.imbue(locale("ru_RU.UTF-8"));

    string dictPath = "dict_opcorpora_clear.txt";
    string corpusPath = "/Users/titrom/Desktop/Computational Linguistics/Articles";
    string outputFile = "ngrams.txt";

    findNGrams(dictPath, corpusPath, outputFile);
    return 0;
}
