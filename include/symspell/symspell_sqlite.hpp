#pragma once

#include <sqlite3.h>
#include <memory>
#include <string>
#include <vector>
#include <symspell/result.hpp>
#include <symspell/symspell.hpp>

namespace yams::symspell {

using yams::symspell::Error;
using yams::symspell::ErrorCode;
using yams::symspell::Result;

class SQLiteStore : public ISymSpellStore {
public:
    SQLiteStore(sqlite3* db, int maxEditDistance = 2, int prefixLength = 7, bool readOnly = false);
    ~SQLiteStore() override;

    SQLiteStore(const SQLiteStore&) = delete;
    SQLiteStore& operator=(const SQLiteStore&) = delete;
    SQLiteStore(SQLiteStore&&) = delete;
    SQLiteStore& operator=(SQLiteStore&&) = delete;

    static Result<void> initializeDatabase(sqlite3* db);

    Result<void> addDelete(int hash, std::string_view term) override;
    std::vector<std::string> getTerms(int hash) override;
    Result<void> setFrequency(std::string_view term, int64_t freq) override;
    Result<void> setFrequencyAndAddDeletes(std::string_view term, int64_t freq,
                                           const std::vector<int>& deleteHashes) override;
    std::optional<int64_t> getFrequency(std::string_view term) override;
    bool termExists(std::string_view term) override;

    Result<void> beginTransaction() override;
    Result<void> commitTransaction() override;
    Result<void> rollbackTransaction() override;
    Result<void> clear() override;

private:
    sqlite3* db_;
    sqlite3_stmt* addDeleteStmt_ = nullptr;
    sqlite3_stmt* addDeleteByIdStmt_ = nullptr;
    sqlite3_stmt* getTermsStmt_ = nullptr;
    sqlite3_stmt* setFrequencyStmt_ = nullptr;
    sqlite3_stmt* getFrequencyStmt_ = nullptr;
    sqlite3_stmt* termExistsStmt_ = nullptr;
    bool inTransaction_ = false;
    bool readOnly_ = false;

    Result<void> prepareStatements();
    void finalizeStatements();
};

} // namespace yams::symspell
