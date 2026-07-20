#include <cstring>
#include <symspell/symspell_sqlite.hpp>

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

constexpr const char* kSetTermFrequency = R"(
    INSERT INTO symspell_terms (term, frequency) VALUES (?, ?)
    ON CONFLICT(term) DO UPDATE SET frequency = excluded.frequency
    RETURNING id
)";

constexpr const char* kAddDelete = R"(
    INSERT OR IGNORE INTO symspell_deletes (delete_hash, term_id)
    VALUES (?, (SELECT id FROM symspell_terms WHERE term = ?))
)";

constexpr const char* kAddDeleteById = R"(
    INSERT OR IGNORE INTO symspell_deletes (delete_hash, term_id)
    VALUES (?, ?)
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

Result<void> executeSql(sqlite3* db, const char* sql, std::string_view operation) {
    char* errorMessage = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);
    if (rc == SQLITE_OK) {
        return {};
    }

    std::string message(operation);
    message += ": ";
    message += errorMessage != nullptr ? errorMessage : sqlite3_errmsg(db);
    sqlite3_free(errorMessage);
    return Error{ErrorCode::DatabaseError, std::move(message)};
}

Error statementError(sqlite3* db, std::string_view operation) {
    return Error{ErrorCode::DatabaseError, std::string(operation) + ": " + sqlite3_errmsg(db)};
}

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
        return Result<void>(Error(ErrorCode::DatabaseError, std::move(msg)));
    }

    if (sqlite3_exec(db, kCreateDeletesHashIndex, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string msg = "Failed to create deletes hash index: ";
        msg += errMsg;
        sqlite3_free(errMsg);
        return Result<void>(Error(ErrorCode::DatabaseError, std::move(msg)));
    }

    return Result<void>();
}

SQLiteStore::SQLiteStore(sqlite3* db, int maxEditDistance, int prefixLength, bool readOnly)
    : db_(db), readOnly_(readOnly) {
    (void)maxEditDistance;
    (void)prefixLength;
    auto result = prepareStatements();
    if (!result) {
        throw std::runtime_error("Failed to prepare SQLite statements");
    }
}

SQLiteStore::~SQLiteStore() {
    if (inTransaction_) {
        (void)rollbackTransaction();
    }
    finalizeStatements();
}

Result<void> SQLiteStore::prepareStatements() {
    if (!readOnly_) {
        if (sqlite3_prepare_v2(db_, kSetTermFrequency, -1, &setFrequencyStmt_, nullptr) !=
            SQLITE_OK) {
            return Result<void>(Error(ErrorCode::DatabaseError,
                                      std::string("Failed to prepare setFrequency statement: ") +
                                          sqlite3_errmsg(db_)));
        }

        if (sqlite3_prepare_v2(db_, kAddDelete, -1, &addDeleteStmt_, nullptr) != SQLITE_OK) {
            return Result<void>(Error(ErrorCode::DatabaseError,
                                      std::string("Failed to prepare addDelete statement: ") +
                                          sqlite3_errmsg(db_)));
        }

        if (sqlite3_prepare_v2(db_, kAddDeleteById, -1, &addDeleteByIdStmt_, nullptr) !=
            SQLITE_OK) {
            return Result<void>(Error(ErrorCode::DatabaseError,
                                      std::string("Failed to prepare addDeleteById statement: ") +
                                          sqlite3_errmsg(db_)));
        }
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
    if (addDeleteByIdStmt_) {
        sqlite3_finalize(addDeleteByIdStmt_);
        addDeleteByIdStmt_ = nullptr;
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

Result<void> SQLiteStore::addDelete(int hash, std::string_view term) {
    if (readOnly_) {
        return Error{ErrorCode::DatabaseError, "Cannot add a delete key to a read-only store"};
    }
    if (!addDeleteStmt_) {
        return Error{ErrorCode::InternalError, "Delete-key statement is not prepared"};
    }

    const auto resetStatement = [&] {
        sqlite3_reset(addDeleteStmt_);
        sqlite3_clear_bindings(addDeleteStmt_);
    };
    if (sqlite3_bind_int(addDeleteStmt_, 1, hash) != SQLITE_OK ||
        sqlite3_bind_text(addDeleteStmt_, 2, term.data(), static_cast<int>(term.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        auto error = statementError(db_, "Failed to bind delete key");
        resetStatement();
        return error;
    }

    if (sqlite3_step(addDeleteStmt_) != SQLITE_DONE) {
        auto error = statementError(db_, "Failed to add delete key");
        resetStatement();
        return error;
    }
    resetStatement();
    return {};
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

Result<void> SQLiteStore::setFrequency(std::string_view term, int64_t freq) {
    if (readOnly_) {
        return Error{ErrorCode::DatabaseError, "Cannot set frequency on a read-only store"};
    }
    if (!setFrequencyStmt_) {
        return Error{ErrorCode::InternalError, "Frequency statement is not prepared"};
    }

    const auto resetStatement = [&] {
        sqlite3_reset(setFrequencyStmt_);
        sqlite3_clear_bindings(setFrequencyStmt_);
    };
    if (sqlite3_bind_text(setFrequencyStmt_, 1, term.data(), static_cast<int>(term.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(setFrequencyStmt_, 2, freq) != SQLITE_OK) {
        auto error = statementError(db_, "Failed to bind term frequency");
        resetStatement();
        return error;
    }

    if (sqlite3_step(setFrequencyStmt_) != SQLITE_ROW ||
        sqlite3_step(setFrequencyStmt_) != SQLITE_DONE) {
        auto error = statementError(db_, "Failed to set term frequency");
        resetStatement();
        return error;
    }
    resetStatement();
    return {};
}

Result<void> SQLiteStore::setFrequencyAndAddDeletes(std::string_view term, int64_t freq,
                                                    const std::vector<int>& deleteHashes) {
    if (readOnly_) {
        return Error{ErrorCode::DatabaseError, "Cannot add a term to a read-only store"};
    }
    if (!setFrequencyStmt_ || !addDeleteByIdStmt_) {
        return Error{ErrorCode::InternalError, "Term insertion statements are not prepared"};
    }

    auto savepoint =
        executeSql(db_, "SAVEPOINT symspell_dictionary_entry", "Failed to begin term insertion");
    if (!savepoint) {
        return savepoint.error();
    }
    const auto rollback = [&] {
        (void)executeSql(db_, "ROLLBACK TO symspell_dictionary_entry",
                         "Failed to roll back term insertion");
        (void)executeSql(db_, "RELEASE symspell_dictionary_entry",
                         "Failed to release term insertion savepoint");
    };
    const auto resetFrequencyStatement = [&] {
        sqlite3_reset(setFrequencyStmt_);
        sqlite3_clear_bindings(setFrequencyStmt_);
    };
    const auto resetDeleteStatement = [&] {
        sqlite3_reset(addDeleteByIdStmt_);
        sqlite3_clear_bindings(addDeleteByIdStmt_);
    };

    if (sqlite3_bind_text(setFrequencyStmt_, 1, term.data(), static_cast<int>(term.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(setFrequencyStmt_, 2, freq) != SQLITE_OK) {
        auto error = statementError(db_, "Failed to bind dictionary term");
        resetFrequencyStatement();
        rollback();
        return error;
    }

    int64_t termId = 0;
    if (sqlite3_step(setFrequencyStmt_) == SQLITE_ROW) {
        termId = sqlite3_column_int64(setFrequencyStmt_, 0);
    } else {
        auto error = statementError(db_, "Failed to store dictionary term");
        resetFrequencyStatement();
        rollback();
        return error;
    }
    if (sqlite3_step(setFrequencyStmt_) != SQLITE_DONE) {
        auto error = statementError(db_, "Failed to finish storing dictionary term");
        resetFrequencyStatement();
        rollback();
        return error;
    }
    resetFrequencyStatement();
    if (termId <= 0) {
        rollback();
        return Error{ErrorCode::DatabaseError, "Stored dictionary term has no row identifier"};
    }

    for (const int hash : deleteHashes) {
        if (sqlite3_bind_int(addDeleteByIdStmt_, 1, hash) != SQLITE_OK ||
            sqlite3_bind_int64(addDeleteByIdStmt_, 2, termId) != SQLITE_OK) {
            auto error = statementError(db_, "Failed to bind dictionary delete key");
            resetDeleteStatement();
            rollback();
            return error;
        }
        if (sqlite3_step(addDeleteByIdStmt_) != SQLITE_DONE) {
            auto error = statementError(db_, "Failed to store dictionary delete key");
            resetDeleteStatement();
            rollback();
            return error;
        }
        resetDeleteStatement();
    }

    auto release =
        executeSql(db_, "RELEASE symspell_dictionary_entry", "Failed to commit term insertion");
    if (!release) {
        rollback();
        return release.error();
    }
    return {};
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

Result<void> SQLiteStore::beginTransaction() {
    if (readOnly_) {
        return Error{ErrorCode::DatabaseError, "Cannot begin a transaction on a read-only store"};
    }
    if (inTransaction_) {
        return {};
    }

    auto result = executeSql(db_, "BEGIN IMMEDIATE TRANSACTION", "Failed to begin transaction");
    if (!result) {
        return result.error();
    }
    inTransaction_ = true;
    return {};
}

Result<void> SQLiteStore::commitTransaction() {
    if (readOnly_) {
        return Error{ErrorCode::DatabaseError, "Cannot commit a transaction on a read-only store"};
    }
    if (!inTransaction_) {
        return {};
    }

    auto result = executeSql(db_, "COMMIT", "Failed to commit transaction");
    if (!result) {
        (void)executeSql(db_, "ROLLBACK", "Failed to roll back transaction");
        inTransaction_ = false;
        return result.error();
    }
    inTransaction_ = false;
    return {};
}

Result<void> SQLiteStore::rollbackTransaction() {
    if (readOnly_) {
        return Error{ErrorCode::DatabaseError,
                     "Cannot roll back a transaction on a read-only store"};
    }
    if (!inTransaction_) {
        return {};
    }

    auto result = executeSql(db_, "ROLLBACK", "Failed to roll back transaction");
    inTransaction_ = false;
    return result;
}

Result<void> SQLiteStore::clear() {
    if (readOnly_) {
        return Error{ErrorCode::DatabaseError, "Cannot clear a read-only store"};
    }

    auto savepoint = executeSql(db_, "SAVEPOINT symspell_clear", "Failed to begin store clear");
    if (!savepoint) {
        return savepoint.error();
    }
    const auto rollback = [&] {
        (void)executeSql(db_, "ROLLBACK TO symspell_clear", "Failed to roll back store clear");
        (void)executeSql(db_, "RELEASE symspell_clear", "Failed to release clear savepoint");
    };

    auto deletes = executeSql(db_, "DELETE FROM symspell_deletes", "Failed to clear delete keys");
    if (!deletes) {
        rollback();
        return deletes.error();
    }
    auto terms = executeSql(db_, "DELETE FROM symspell_terms", "Failed to clear terms");
    if (!terms) {
        rollback();
        return terms.error();
    }
    auto release = executeSql(db_, "RELEASE symspell_clear", "Failed to commit store clear");
    if (!release) {
        rollback();
        return release.error();
    }
    return {};
}

} // namespace yams::symspell
