#include <cstring>
#include <iostream>
#include <symspell/symspell_sqlite.hpp>
#include <yams/core/types.h>

namespace yams::symspell {

namespace {

constexpr const char* kCreateTermsTable = R"(
    CREATE TABLE IF NOT EXISTS symspell_terms (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        term TEXT UNIQUE NOT NULL,
        frequency INTEGER DEFAULT 1
    )
)";

constexpr const char* kCreateDeletesTable = R"(
    CREATE TABLE IF NOT EXISTS symspell_deletes (
        delete_hash INTEGER NOT NULL,
        term_id INTEGER NOT NULL,
        FOREIGN KEY (term_id) REFERENCES symspell_terms(id) ON DELETE CASCADE,
        PRIMARY KEY (delete_hash, term_id)
    ) WITHOUT ROWID
)";

constexpr const char* kCreateTermsIndex = R"(
    CREATE INDEX IF NOT EXISTS idx_symspell_terms_term ON symspell_terms(term)
)";

constexpr const char* kCreateDeletesHashIndex = R"(
    CREATE INDEX IF NOT EXISTS idx_symspell_deletes_hash ON symspell_deletes(delete_hash)
)";

constexpr const char* kInsertOrUpdateTerm = R"(
    INSERT INTO symspell_terms (term, frequency) VALUES (?, ?)
    ON CONFLICT(term) DO UPDATE SET frequency = frequency + excluded.frequency
)";

constexpr const char* kAddDelete = R"(
    INSERT OR IGNORE INTO symspell_deletes (delete_hash, term_id)
    VALUES (?, (SELECT id FROM symspell_terms WHERE term = ?))
)";

constexpr const char* kGetTerms = R"(
    SELECT t.term FROM symspell_terms t
    INNER JOIN symspell_deletes d ON t.id = d.term_id
    WHERE d.delete_hash = ?
)";

constexpr const char* kGetFrequency = R"(
    SELECT frequency FROM symspell_terms WHERE term = ?
)";

constexpr const char* kTermExists = R"(
    SELECT 1 FROM symspell_terms WHERE term = ? LIMIT 1
)";

} // namespace

Result<void> SQLiteStore::initializeDatabase(sqlite3* db) {
    char* errMsg = nullptr;

    if (sqlite3_exec(db, kCreateTermsTable, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string msg = "Failed to create terms table: ";
        msg += errMsg;
        sqlite3_free(errMsg);
        return Result<void>(Error(ErrorCode::DatabaseError, std::move(msg)));
    }

    if (sqlite3_exec(db, kCreateDeletesTable, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string msg = "Failed to create deletes table: ";
        msg += errMsg;
        sqlite3_free(errMsg);
        return Result<void>(Error(ErrorCode::DatabaseError, std::move(msg)));
    }

    if (sqlite3_exec(db, kCreateTermsIndex, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string msg = "Failed to create terms index: ";
        msg += errMsg;
        sqlite3_free(errMsg);
    }

    if (sqlite3_exec(db, kCreateDeletesHashIndex, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string msg = "Failed to create deletes hash index: ";
        msg += errMsg;
        sqlite3_free(errMsg);
    }

    return Result<void>();
}

SQLiteStore::SQLiteStore(sqlite3* db, int maxEditDistance, int prefixLength) : db_(db) {
    (void)maxEditDistance;
    (void)prefixLength;
    auto result = prepareStatements();
    if (!result) {
        throw std::runtime_error("Failed to prepare SQLite statements");
    }
}

SQLiteStore::~SQLiteStore() {
    finalizeStatements();
}

Result<void> SQLiteStore::prepareStatements() {
    if (sqlite3_prepare_v2(db_, kInsertOrUpdateTerm, -1, &setFrequencyStmt_, nullptr) !=
        SQLITE_OK) {
        return Result<void>(
            Error(ErrorCode::DatabaseError,
                  std::string("Failed to prepare setFrequency statement: ") + sqlite3_errmsg(db_)));
    }

    if (sqlite3_prepare_v2(db_, kAddDelete, -1, &addDeleteStmt_, nullptr) != SQLITE_OK) {
        return Result<void>(
            Error(ErrorCode::DatabaseError,
                  std::string("Failed to prepare addDelete statement: ") + sqlite3_errmsg(db_)));
    }

    if (sqlite3_prepare_v2(db_, kGetTerms, -1, &getTermsStmt_, nullptr) != SQLITE_OK) {
        return Result<void>(
            Error(ErrorCode::DatabaseError,
                  std::string("Failed to prepare getTerms statement: ") + sqlite3_errmsg(db_)));
    }

    if (sqlite3_prepare_v2(db_, kGetFrequency, -1, &getFrequencyStmt_, nullptr) != SQLITE_OK) {
        return Result<void>(
            Error(ErrorCode::DatabaseError,
                  std::string("Failed to prepare getFrequency statement: ") + sqlite3_errmsg(db_)));
    }

    if (sqlite3_prepare_v2(db_, kTermExists, -1, &termExistsStmt_, nullptr) != SQLITE_OK) {
        return Result<void>(
            Error(ErrorCode::DatabaseError,
                  std::string("Failed to prepare termExists statement: ") + sqlite3_errmsg(db_)));
    }

    return Result<void>();
}

void SQLiteStore::finalizeStatements() {
    if (setFrequencyStmt_) {
        sqlite3_finalize(setFrequencyStmt_);
        setFrequencyStmt_ = nullptr;
    }
    if (addDeleteStmt_) {
        sqlite3_finalize(addDeleteStmt_);
        addDeleteStmt_ = nullptr;
    }
    if (getTermsStmt_) {
        sqlite3_finalize(getTermsStmt_);
        getTermsStmt_ = nullptr;
    }
    if (getFrequencyStmt_) {
        sqlite3_finalize(getFrequencyStmt_);
        getFrequencyStmt_ = nullptr;
    }
    if (termExistsStmt_) {
        sqlite3_finalize(termExistsStmt_);
        termExistsStmt_ = nullptr;
    }
}

void SQLiteStore::addDelete(int hash, std::string_view term) {
    if (!addDeleteStmt_) {
        return;
    }

    sqlite3_bind_int(addDeleteStmt_, 1, hash);
    sqlite3_bind_text(addDeleteStmt_, 2, term.data(), static_cast<int>(term.size()), SQLITE_STATIC);

    sqlite3_step(addDeleteStmt_);
    sqlite3_reset(addDeleteStmt_);
    sqlite3_clear_bindings(addDeleteStmt_);
}

std::vector<std::string> SQLiteStore::getTerms(int hash) {
    std::vector<std::string> result;

    if (!getTermsStmt_) {
        return result;
    }

    sqlite3_bind_int(getTermsStmt_, 1, hash);

    while (sqlite3_step(getTermsStmt_) == SQLITE_ROW) {
        const char* term = reinterpret_cast<const char*>(sqlite3_column_text(getTermsStmt_, 0));
        if (term) {
            result.push_back(term);
        }
    }

    sqlite3_reset(getTermsStmt_);

    return result;
}

void SQLiteStore::setFrequency(std::string_view term, int64_t freq) {
    if (!setFrequencyStmt_) {
        return;
    }

    sqlite3_bind_text(setFrequencyStmt_, 1, term.data(), static_cast<int>(term.size()),
                      SQLITE_STATIC);
    sqlite3_bind_int64(setFrequencyStmt_, 2, freq);

    sqlite3_step(setFrequencyStmt_);
    sqlite3_reset(setFrequencyStmt_);
    sqlite3_clear_bindings(setFrequencyStmt_);
}

std::optional<int64_t> SQLiteStore::getFrequency(std::string_view term) {
    if (!getFrequencyStmt_) {
        return std::nullopt;
    }

    sqlite3_bind_text(getFrequencyStmt_, 1, term.data(), static_cast<int>(term.size()),
                      SQLITE_STATIC);

    int64_t result = 0;
    bool found = false;

    if (sqlite3_step(getFrequencyStmt_) == SQLITE_ROW) {
        result = sqlite3_column_int64(getFrequencyStmt_, 0);
        found = true;
    }

    sqlite3_reset(getFrequencyStmt_);
    sqlite3_clear_bindings(getFrequencyStmt_);

    return found ? std::optional<int64_t>(result) : std::nullopt;
}

bool SQLiteStore::termExists(std::string_view term) {
    if (!termExistsStmt_) {
        return false;
    }

    sqlite3_bind_text(termExistsStmt_, 1, term.data(), static_cast<int>(term.size()),
                      SQLITE_STATIC);

    bool exists = false;
    if (sqlite3_step(termExistsStmt_) == SQLITE_ROW) {
        exists = true;
    }

    sqlite3_reset(termExistsStmt_);
    sqlite3_clear_bindings(termExistsStmt_);

    return exists;
}

void SQLiteStore::beginTransaction() {
    if (!inTransaction_) {
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "Failed to begin transaction: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        } else {
            inTransaction_ = true;
        }
    }
}

void SQLiteStore::commitTransaction() {
    if (inTransaction_) {
        char* errMsg = nullptr;
        if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "Failed to commit transaction: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        } else {
            inTransaction_ = false;
        }
    }
}

void SQLiteStore::rollbackTransaction() {
    if (inTransaction_) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        inTransaction_ = false;
    }
}

} // namespace yams::symspell
