#pragma once

#include <sqlite3.h>
#include <memory>
#include <string>
#include <vector>
#include <symspell/symspell.hpp>
#include <yams/core/types.h>

namespace yams::symspell {

class SQLiteStore : public ISymSpellStore {
public:
    SQLiteStore(sqlite3* db, int maxEditDistance = 2, int prefixLength = 7);
    ~SQLiteStore() override;

    SQLiteStore(const SQLiteStore&) = delete;
    SQLiteStore& operator=(const SQLiteStore&) = delete;
    SQLiteStore(SQLiteStore&&) = delete;
    SQLiteStore& operator=(SQLiteStore&&) = delete;

    static Result<void> initializeDatabase(sqlite3* db);

    void addDelete(int hash, std::string_view term) override;
    std::vector<std::string> getTerms(int hash) override;
    void setFrequency(std::string_view term, int64_t freq) override;
    std::optional<int64_t> getFrequency(std::string_view term) override;
    bool termExists(std::string_view term) override;

    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();

private:
    sqlite3* db_;
    sqlite3_stmt* addDeleteStmt_ = nullptr;
    sqlite3_stmt* getTermsStmt_ = nullptr;
    sqlite3_stmt* setFrequencyStmt_ = nullptr;
    sqlite3_stmt* getFrequencyStmt_ = nullptr;
    sqlite3_stmt* termExistsStmt_ = nullptr;
    bool inTransaction_ = false;

    Result<void> prepareStatements();
    void finalizeStatements();
};

} // namespace yams::symspell
