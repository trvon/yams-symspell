#include <sqlite3.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <symspell/symspell.hpp>
#include <symspell/symspell_sqlite.hpp>

using namespace yams::symspell;

class Benchmark {
public:
    void run() {
        std::cout << "=== SymSpell Benchmark ===" << std::endl;

        benchmarkDictionaryCreation();
        benchmarkLookup();
        benchmarkConcurrentAccess();
        benchmarkSQLitePersistence();
        benchmarkLargeDictionary();

        std::cout << "\n=== Benchmark Complete ===" << std::endl;
    }

private:
    void printResult(const std::string& name, std::chrono::microseconds duration,
                     size_t count = 1) {
        double ms = duration.count() / 1000.0;
        double perOp = count > 0 ? ms / count * 1000.0 : 0;
        std::cout << std::left << std::setw(40) << name << std::right << std::fixed
                  << std::setprecision(2) << std::setw(12) << ms << " ms" << std::setw(12) << perOp
                  << " us/op" << std::endl;
    }

    void benchmarkDictionaryCreation() {
        std::cout << "\n--- Dictionary Creation ---" << std::endl;

        std::vector<std::string> words;
        for (int i = 0; i < 10000; ++i) {
            words.push_back("word" + std::to_string(i));
        }

        auto store = std::make_unique<MemoryStore>(2, 7);
        SymSpell spell(std::move(store), 2, 7);

        auto start = std::chrono::high_resolution_clock::now();
        for (const auto& word : words) {
            spell.createDictionaryEntry(word, 100);
        }
        auto end = std::chrono::high_resolution_clock::now();

        printResult("Create 10,000 entries",
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    }

    void benchmarkLookup() {
        std::cout << "\n--- Lookup Performance ---" << std::endl;

        auto store = std::make_unique<MemoryStore>(2, 7);
        SymSpell spell(std::move(store), 2, 7);

        for (int i = 0; i < 5000; ++i) {
            spell.createDictionaryEntry("word" + std::to_string(i), 100);
        }

        std::vector<std::string> queries = {
            "wrod1000", "hellp", "wolrd", "woed", "wod",
        };

        auto start = std::chrono::high_resolution_clock::now();
        size_t totalResults = 0;
        for (int iter = 0; iter < 1000; ++iter) {
            for (const auto& query : queries) {
                auto results = spell.lookup(query, Verbosity::Closest);
                totalResults += results.size();
            }
        }
        auto end = std::chrono::high_resolution_clock::now();

        printResult("5,000 lookups x 1,000 iterations",
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start));
        std::cout << "  Total suggestions found: " << totalResults << std::endl;
    }

    void benchmarkConcurrentAccess() {
        std::cout << "\n--- Concurrent Access ---" << std::endl;

        auto store = std::make_unique<MemoryStore>(2, 7);
        SymSpell spell(std::move(store), 2, 7);

        for (int i = 0; i < 1000; ++i) {
            spell.createDictionaryEntry("word" + std::to_string(i), 100);
        }

        auto start = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> threads;

        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&spell, t]() {
                for (int i = 0; i < 250; ++i) {
                    spell.lookup("wrod" + std::to_string(i), Verbosity::Closest);
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
        auto end = std::chrono::high_resolution_clock::now();

        printResult("4 threads x 1,000 lookups",
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    }

    void benchmarkSQLitePersistence() {
        std::cout << "\n--- SQLite Persistence ---" << std::endl;

        const char* path = "/tmp/symspell_bench.db";

        {
            sqlite3* db;
            sqlite3_open(path, &db);

            auto initResult = SQLiteStore::initializeDatabase(db);
            auto store = std::make_unique<SQLiteStore>(db, 2, 7);

            // Use transaction for bulk inserts
            store->beginTransaction();
            SymSpell spell(std::move(store), 2, 7);

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 1000; ++i) {
                spell.createDictionaryEntry("word" + std::to_string(i), 100);
            }
            auto end = std::chrono::high_resolution_clock::now();

            // Note: We can't call commitTransaction on moved store, so we commit via db directly
            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

            printResult("Create 1,000 entries (SQLite)",
                        std::chrono::duration_cast<std::chrono::microseconds>(end - start));

            sqlite3_close(db);
        }

        {
            sqlite3* db;
            sqlite3_open(path, &db);

            auto store = std::make_unique<SQLiteStore>(db, 2, 7);
            SymSpell spell(std::move(store), 2, 7);

            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < 10000; ++i) {
                spell.lookup("wrd" + std::to_string(i), Verbosity::Closest);
            }
            auto end = std::chrono::high_resolution_clock::now();

            printResult("10,000 lookups (SQLite)",
                        std::chrono::duration_cast<std::chrono::microseconds>(end - start));

            sqlite3_close(db);
        }

        std::remove(path);
    }

    void benchmarkLargeDictionary() {
        std::cout << "\n--- Large Dictionary (50,000 entries) ---" << std::endl;

        auto store = std::make_unique<MemoryStore>(2, 7);
        SymSpell spell(std::move(store), 2, 7);

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 50000; ++i) {
            spell.createDictionaryEntry("dictionaryword" + std::to_string(i), 100);
        }
        auto end = std::chrono::high_resolution_clock::now();

        printResult("Create 50,000 entries",
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start));

        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1000; ++i) {
            spell.lookup("dictonaryword" + std::to_string(i % 50000), Verbosity::Closest);
        }
        end = std::chrono::high_resolution_clock::now();

        printResult("1,000 lookups (50K dict)",
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    }
};

int main() {
    Benchmark bench;
    bench.run();
    return 0;
}
