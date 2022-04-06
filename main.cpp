#include <iostream>
#include <string>
#include <vector>
#include "filesystem"
#include <codecvt>
#include <sstream>
#include <fstream>

#include "dictionary.h"
#include "filemap.h"
#include "WordContext.h"

using namespace std;
using recursive_directory_iterator = std::__fs::filesystem::recursive_directory_iterator;

int CONTEXT_FREQUENCY_THRESHOLD = 5;

vector<string> getFilesFromDir(const string& dirPath) {
    vector<string> files;
    for (const auto& dirEntry : recursive_directory_iterator(dirPath))
        if (!(dirEntry.path().filename() == ".DS_Store"))
            files.push_back(dirEntry.path());
    return files;
}

vector<WordContext> makeAllContexts(vector<vector<Word*>> wordVectors, wstring &stringContext) {
    vector<vector<Word*>> allWordContexts;

    for (int i = 0; i < wordVectors.size(); i++) {
        auto& currentVector = wordVectors.at(i);
        if (i == 0) {
            set<wstring> handledForms;
            for (Word *word: currentVector)
                if (handledForms.find(word->word) == handledForms.end()) {
                    handledForms.insert(word->word);
                    allWordContexts.push_back(vector<Word *>{word});
                }
        }
        else if (currentVector.size() == 1) {
            for (vector<Word*>& context : allWordContexts)
                context.push_back(currentVector.at(0));
        } else {
            vector<vector<Word*>> newAllContexts;
            set<wstring> handledForms;
            for (Word *word: currentVector)
                for (const vector<Word*>& context : allWordContexts) {
                    if (handledForms.find(word->word) == handledForms.end()) {
                        handledForms.insert(word->word);
                        vector<Word*> newContext;
                        newContext = context;
                        newContext.push_back(word);
                        newAllContexts.push_back(newContext);
                    }
                }
            allWordContexts = newAllContexts;
        }
    }

    vector<WordContext> allContexts;

    for (const auto& contextVector : allWordContexts) {
        wstring normalizedForm;
        for (Word* word : contextVector)
            normalizedForm += word->word + L" ";

        normalizedForm.pop_back();
        allContexts.emplace_back(normalizedForm, normalizedForm,contextVector);
    }
    return allContexts;
}

vector<WordContext> processContext(const vector<wstring>& stringContext, unordered_map <wstring, vector<Word*>> *dictionary) {
    auto& f = std::use_facet<std::ctype<wchar_t>>(std::locale());

    wstring rawContext;
    vector<vector<Word*>> contextVector;

    for (wstring contextWord: stringContext) {
        rawContext += contextWord + L" ";
        f.toupper(&contextWord[0], &contextWord[0] + contextWord.size());

        if (dictionary->find(contextWord) == dictionary->end()) {
            Word *newWord = new Word();
            newWord->word = contextWord;
            newWord->partOfSpeech = L"UNKW";
            dictionary->emplace(newWord->word, vector<Word*>{newWord});
        }

        vector<Word*> words = dictionary->at(contextWord);
        contextVector.push_back(words);
    }

    rawContext.pop_back();

    return makeAllContexts(contextVector, rawContext);
}

bool haveCommonForm(wstring firstWord, wstring secondWord, unordered_map <wstring, vector<Word*>> *dictionary) {
    auto& f = std::use_facet<std::ctype<wchar_t>>(std::locale());
    f.toupper(&firstWord[0], &firstWord[0] + firstWord.size());
    f.toupper(&secondWord[0], &secondWord[0] + secondWord.size());
    vector<Word*> words1;
    vector<Word*> words2;

    if (dictionary->find(firstWord) == dictionary->end()) {
        Word *newWord = new Word();
        newWord->word = firstWord;
        newWord->partOfSpeech = L"UNKW";
        dictionary->emplace(newWord->word, vector<Word*>{newWord});
    }

    if (dictionary->find(secondWord) == dictionary->end()) {
        Word *newWord = new Word();
        newWord->word = secondWord;
        newWord->partOfSpeech = L"UNKW";
        dictionary->emplace(newWord->word, vector<Word*>{newWord});
    }

    words1 = dictionary->at(firstWord);
    words2 = dictionary->at(secondWord);

    for (Word* word1: words1) {
        for (Word* word2: words2) {
            if (word1->word == word2->word)
                return true;
        }
    }

    return false;
}

void addContextsToSet(vector<WordContext>& contexts, unordered_map <wstring, WordContext>* NGramms, const string& filePath) {
    for (WordContext context : contexts) {
        if (NGramms->find(context.normalizedForm) == NGramms->end())
            NGramms->emplace(context.normalizedForm, context);

        NGramms->at(context.normalizedForm).totalEntryCount++;
        NGramms->at(context.normalizedForm).textEntry.insert(filePath);
    }
}

void handleWord(const wstring& wordStr, int windowSize,
                unordered_map <wstring, vector<Word*>> *dictionary,
                vector<wstring>* leftStringNGrams,
                unordered_map <wstring, WordContext> *NGrams,
                const string& filePath) {
    if (leftStringNGrams->size() == windowSize) {
        vector<WordContext> phraseContexts = processContext(*leftStringNGrams, dictionary);
        addContextsToSet(phraseContexts, NGrams, filePath);
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

void findConcordances(const string& dictPath, const string& corpusPath,
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

    for(const auto& pair : NGrams)
        leftContexts.push_back(pair.second);

    struct {
        bool operator()(const WordContext& a, const WordContext& b) const { return a.totalEntryCount > b.totalEntryCount; }
    } comp;

    sort(leftContexts.begin(), leftContexts.end(), comp);

    ofstream outFile (outputFile.c_str());

    int printCount = 0;
    cout << "\n";
    for (const WordContext& context : leftContexts) {
        if (context.totalEntryCount >= CONTEXT_FREQUENCY_THRESHOLD) {
            wcout << "<\"" << context.rawValue
            << "\", Total entries: " << context.totalEntryCount
            << ", TF: " << context.textEntry.size()
            << ">\n";
            printCount++;
        }

        string line = wstring_convert<codecvt_utf8<wchar_t>>().to_bytes(context.rawValue);
        outFile << "<\"" << line << "\", " << context.totalEntryCount << ">\n";
    }

    outFile.close();
}

int main() {
    CONTEXT_FREQUENCY_THRESHOLD = 10;

    locale::global(locale("ru_RU.UTF-8"));
    wcout.imbue(locale("ru_RU.UTF-8"));

    string dictPath = "dict_opcorpora_clear.txt";
    string corpusPath = "/Users/titrom/Desktop/Computational Linguistics/Articles";
    const int windowSize = 3;

    string outputFile = "concordances.txt";

    findConcordances(dictPath, corpusPath, windowSize, outputFile);
    return 0;
}
