#include <sqlite3.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <symspell/symspell.hpp>
#include <symspell/symspell_sqlite.hpp>

using namespace yams::symspell;

void testBasicLookup() {
    std::cout << "Running testBasicLookup... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 1000);
    spell.createDictionaryEntry("world", 500);
    spell.createDictionaryEntry("help", 100);

    auto suggestions = spell.lookup("hellp", Verbosity::Closest);

    assert(!suggestions.empty());
    assert(suggestions[0].term == "hello");
    assert(suggestions[0].distance == 1);

    std::cout << "PASSED" << std::endl;
}

void testExactMatch() {
    std::cout << "Running testExactMatch... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 1000);

    auto suggestions = spell.lookup("hello", Verbosity::Closest);

    assert(!suggestions.empty());
    assert(suggestions[0].term == "hello");
    assert(suggestions[0].distance == 0);
    assert(suggestions[0].frequency == 1000);

    std::cout << "PASSED" << std::endl;
}

void testVerbosityTop() {
    std::cout << "Running testVerbosityTop... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 100);
    spell.createDictionaryEntry("hallo", 50);
    spell.createDictionaryEntry("hullo", 30);

    auto suggestions = spell.lookup("hellp", Verbosity::Top);

    assert(suggestions.size() == 1);
    assert(suggestions[0].term == "hello");

    std::cout << "PASSED" << std::endl;
}

void testVerbosityAll() {
    std::cout << "Running testVerbosityAll... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 100);
    spell.createDictionaryEntry("hallo", 50);

    auto suggestions = spell.lookup("hellp", Verbosity::All);

    assert(suggestions.size() >= 1);

    std::cout << "PASSED" << std::endl;
}

void testFrequencyAccumulation() {
    std::cout << "Running testFrequencyAccumulation... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("test", 100);
    spell.createDictionaryEntry("test", 50);

    auto suggestions = spell.lookup("test", Verbosity::Closest);

    assert(!suggestions.empty());
    assert(suggestions[0].frequency == 150);

    std::cout << "PASSED" << std::endl;
}

void testMultipleEdits() {
    std::cout << "Running testMultipleEdits... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("programming", 1000);
    spell.createDictionaryEntry("programing", 50);

    auto suggestions = spell.lookup("programmng", Verbosity::Closest);

    assert(!suggestions.empty());

    std::cout << "PASSED" << std::endl;
}

void testEmptyInput() {
    std::cout << "Running testEmptyInput... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("a", 10);

    auto suggestions = spell.lookup("", Verbosity::Closest);

    std::cout << "PASSED" << std::endl;
}

void testNoSuggestions() {
    std::cout << "Running testNoSuggestions... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 100);

    auto suggestions = spell.lookup("xyzabc", Verbosity::Closest);

    assert(suggestions.empty());

    std::cout << "PASSED" << std::endl;
}

void testMaxEditDistance() {
    std::cout << "Running testMaxEditDistance... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 100);

    auto suggestions = spell.lookup("hexxo", Verbosity::Closest, 1);

    assert(suggestions.empty());

    suggestions = spell.lookup("hexxo", Verbosity::Closest, 2);

    std::cout << "PASSED" << std::endl;
}

void testSQLiteStore() {
    std::cout << "Running testSQLiteStore... " << std::flush;

    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    assert(rc == SQLITE_OK);

    auto initResult = SQLiteStore::initializeDatabase(db);
    assert(initResult);

    auto store = std::make_unique<SQLiteStore>(db, 2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 1000);
    spell.createDictionaryEntry("world", 500);

    auto suggestions = spell.lookup("hellp", Verbosity::Closest);

    assert(!suggestions.empty());
    assert(suggestions[0].term == "hello");

    sqlite3_close(db);

    std::cout << "PASSED" << std::endl;
}

void testSQLitePersistence() {
    std::cout << "Running testSQLitePersistence... " << std::flush;

    const char* path = "/tmp/symspell_test.db";

    {
        sqlite3* db;
        int rc = sqlite3_open(path, &db);
        assert(rc == SQLITE_OK);

        auto initResult = SQLiteStore::initializeDatabase(db);
        assert(initResult);

        auto store = std::make_unique<SQLiteStore>(db, 2, 7);
        SymSpell spell(std::move(store), 2, 7);

        spell.createDictionaryEntry("persistent", 999);
        spell.createDictionaryEntry("word", 100);

        sqlite3_close(db);
    }

    {
        sqlite3* db;
        int rc = sqlite3_open(path, &db);
        assert(rc == SQLITE_OK);

        auto store = std::make_unique<SQLiteStore>(db, 2, 7);
        SymSpell spell(std::move(store), 2, 7);

        auto suggestions = spell.lookup("persistant", Verbosity::Closest);

        assert(!suggestions.empty());
        assert(suggestions[0].term == "persistent");

        sqlite3_close(db);
    }

    std::remove(path);

    std::cout << "PASSED" << std::endl;
}

void testConcurrentAccess() {
    std::cout << "Running testConcurrentAccess... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("hello", 1000);
    spell.createDictionaryEntry("world", 500);
    spell.createDictionaryEntry("test", 100);

    std::vector<std::thread> threads;
    std::vector<std::vector<Suggestion>> allResults;
    std::mutex resultsMutex;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&spell, &allResults, &resultsMutex, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 10));
            auto suggestions = spell.lookup("hellp", Verbosity::Closest);
            std::lock_guard<std::mutex> lock(resultsMutex);
            allResults.push_back(suggestions);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    assert(allResults.size() == 4);

    std::cout << "PASSED" << std::endl;
}

void testLongWord() {
    std::cout << "Running testLongWord... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    std::string longWord = "pneumonoultramicroscopicsilicovolcanoconiosis";
    spell.createDictionaryEntry(longWord, 1);

    auto suggestions = spell.lookup("pneumonoultramicro...", Verbosity::Closest);

    std::cout << "PASSED" << std::endl;
}

void testCaseSensitivity() {
    std::cout << "Running testCaseSensitivity... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("Hello", 100);

    auto suggestions = spell.lookup("hello", Verbosity::Closest);

    assert(!suggestions.empty());
    assert(suggestions[0].term == "Hello");

    std::cout << "PASSED" << std::endl;
}

void testDamerauLevenshtein() {
    std::cout << "Running testDamerauLevenshtein... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("ca", 100);
    spell.createDictionaryEntry("abc", 100);

    auto suggestions = spell.lookup("acb", Verbosity::Closest);

    std::cout << "PASSED" << std::endl;
}

void testUnicode() {
    std::cout << "Running testUnicode... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    spell.createDictionaryEntry("naive", 100);

    auto suggestions = spell.lookup("naive", Verbosity::Closest);

    std::cout << "PASSED" << std::endl;
}

void testPerformance() {
    std::cout << "Running testPerformance... " << std::flush;

    auto store = std::make_unique<MemoryStore>(2, 7);
    SymSpell spell(std::move(store), 2, 7);

    for (int i = 0; i < 10000; ++i) {
        spell.createDictionaryEntry("word" + std::to_string(i), 100 - (i % 100));
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto suggestions = spell.lookup("wrod9999", Verbosity::Closest);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "(lookup took " << duration.count() << " us) ";
    assert(!suggestions.empty());

    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "=== SymSpell Tests ===" << std::endl;

    testBasicLookup();
    testExactMatch();
    testVerbosityTop();
    testVerbosityAll();
    testFrequencyAccumulation();
    testMultipleEdits();
    testEmptyInput();
    testNoSuggestions();
    testMaxEditDistance();
    testSQLiteStore();
    testSQLitePersistence();
    testConcurrentAccess();
    testLongWord();
    testCaseSensitivity();
    testDamerauLevenshtein();
    testUnicode();
    testPerformance();

    std::cout << "\n=== All Tests PASSED ===" << std::endl;

    return 0;
}
