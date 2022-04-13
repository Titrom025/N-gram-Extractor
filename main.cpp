#include <iostream>
#include <string>
#include <vector>
#include "filesystem"
#include <codecvt>
#include <fstream>

#include "dictionary.h"
#include "filemap.h"
#include "WordContext.h"

using namespace std;
using recursive_directory_iterator = std::__fs::filesystem::recursive_directory_iterator;

int MIN_NGRAM_LENGTH = 2;
int MAX_NGRAM_LENGTH = 4;
int CONTEXT_FREQUENCY_THRESHOLD = 5;
int hGramsHandled = 0;

vector<string> getFilesFromDir(const string& dirPath) {
    vector<string> files;
    for (const auto& dirEntry : recursive_directory_iterator(dirPath))
        if (!(dirEntry.path().filename() == ".DS_Store"))
            files.push_back(dirEntry.path());
    return files;
}

WordContext processContext(const vector<wstring>& stringContext, int count, unordered_map <wstring, vector<Word*>> *dictionary) {
    auto& f = std::use_facet<std::ctype<wchar_t>>(std::locale());

    vector<Word*> contextVector;

    for (int i = 0; i < count; i++) {
        wstring contextWord = stringContext.at(i);
        f.toupper(&contextWord[0], &contextWord[0] + contextWord.size());

        if (dictionary->find(contextWord) == dictionary->end()) {
            Word *newWord = new Word();
            newWord->word = contextWord;
            newWord->partOfSpeech = L"UNKW";
            dictionary->emplace(newWord->word, vector<Word*>{newWord});
        }

        vector<Word*> &words = dictionary->at(contextWord);
        contextVector.push_back(words.at(0));
    }

    wstring normalizedForm;
    for (Word* word : contextVector)
        normalizedForm += word->word + L" ";
    normalizedForm.pop_back();

    return {normalizedForm,contextVector};
}

void addContextToSet(WordContext& context, unordered_map <wstring, WordContext>* NGramms, const string& filePath) {
    if (NGramms->find(context.normalizedForm) == NGramms->end())
        NGramms->emplace(context.normalizedForm, context);

    auto &ngram= NGramms->at(context.normalizedForm);
    ngram.totalEntryCount++;
    ngram.textEntry.insert(filePath);
}

void handleWord(const wstring& wordStr, int windowSize,
                unordered_map <wstring, vector<Word*>> *dictionary,
                vector<wstring>* leftStringNGrams,
                unordered_map <wstring, WordContext> *NGrams,
                const string& filePath) {
    for (int i = MIN_NGRAM_LENGTH; i <= MAX_NGRAM_LENGTH; i++) {
        if (i <= leftStringNGrams->size()) {
            WordContext phraseContext = processContext(*leftStringNGrams, i, dictionary);
            addContextToSet(phraseContext, NGrams, filePath);
        }
    }
    if (leftStringNGrams->size() == MAX_NGRAM_LENGTH) {
        hGramsHandled++;
        leftStringNGrams->erase(leftStringNGrams->begin());
    }

    leftStringNGrams->push_back(wordStr);
}

void handleFile(const string& filePath, unordered_map <wstring, vector<Word*>> *dictionary,
                int windowSize, unordered_map <wstring, WordContext> *NGrams) {

    size_t length;
    auto filePtr = map_file(filePath.c_str(), length);
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

            handleWord(wordStr, windowSize,
                       dictionary,
                       &leftStringNGrams,
                       NGrams, filePath);

            if (line[pos] == L'.' || line[pos] == L',' ||
                line[pos] == L'!' || line[pos] == L'?') {
                wstring punct;
                punct.push_back(line[pos]);
                handleWord(punct, windowSize,
                           dictionary,
                           &leftStringNGrams,
                           NGrams, filePath);
            }
        }
        if (filePtr)
            filePtr++;
    }
}

void findNGrams(const string& dictPath, const string& corpusPath,
                int windowSize, const string& outputFile) {
    auto dictionary = initDictionary(dictPath);
    vector<string> files = getFilesFromDir(corpusPath);

    unordered_map <wstring, WordContext> NGrams;

    cout << "Start handling files\n";
    for (const string& file : files)
        handleFile(file, &dictionary,
                   windowSize,&NGrams);

    vector<WordContext> leftContexts;
    leftContexts.reserve(NGrams.size());

    for(auto& pair : NGrams)
        if (pair.second.totalEntryCount >= CONTEXT_FREQUENCY_THRESHOLD) {
            pair.second.tfidf = ((float) pair.second.totalEntryCount / hGramsHandled) *
                                log10((float) files.size() / pair.second.textEntry.size() + 1);
            leftContexts.push_back(pair.second);
        }

    struct {
        bool operator()(const WordContext& a, const WordContext& b) const { return a.tfidf > b.tfidf; }
    } comp;

    sort(leftContexts.begin(), leftContexts.end(), comp);

    ofstream outFile (outputFile.c_str());

    int printCount = 0;
    cout << "\n";
    for (const WordContext& context : leftContexts) {
        if (context.totalEntryCount >= CONTEXT_FREQUENCY_THRESHOLD) {
            wcout << "<\"" << context.normalizedForm
            << "\", Total entries: " << context.totalEntryCount
            << ", TF: " << context.textEntry.size()
            << ", TF-IDF: " << context.tfidf
            << ">\n";
            printCount++;
        }

        string line = wstring_convert<codecvt_utf8<wchar_t>>().to_bytes(context.normalizedForm);
        outFile << "<\"" << line << "\", " << context.totalEntryCount << ">\n";
    }

    outFile.close();
}

int main() {
    CONTEXT_FREQUENCY_THRESHOLD = 10;
    MIN_NGRAM_LENGTH = 2;
    MAX_NGRAM_LENGTH = 4;

    locale::global(locale("ru_RU.UTF-8"));
    wcout.imbue(locale("ru_RU.UTF-8"));

    string dictPath = "dict_opcorpora_clear.txt";
    string corpusPath = "/Users/titrom/Desktop/Computational Linguistics/Articles";
    const int windowSize = 3;

    string outputFile = "concordances.txt";

    findNGrams(dictPath, corpusPath, windowSize, outputFile);
    return 0;
}
