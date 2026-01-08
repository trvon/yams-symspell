# yams-symspell

Modern C++20/23 implementation of the SymSpell algorithm for fast fuzzy string matching and spelling correction.

## Overview

This library provides a high-performance SymSpell implementation with:

- **Modern C++20**: Uses `std::string_view`, concepts, and ranges
- **Header-only core**: `symspell.hpp` for in-memory usage
- **SQLite persistence**: Optional `SQLiteStore` for persistent dictionaries
- **YAMS integration**: Designed to work with the YAMS codebase patterns

## Algorithm

SymSpell uses the Symmetric Delete spelling correction algorithm, which is:

- **1000x faster** than traditional BK-tree approaches
- **Language independent**: Only requires deletes (no transposes/replaces/inserts)
- **Memory efficient**: Uses hash-based delete dictionary

## File Structure

```
third_party/symspell/
├── include/symspell/
│   ├── symspell.hpp       # Header-only core algorithm
│   └── symspell_sqlite.hpp # SQLite persistence interface
├── src/
│   └── symspell_sqlite.cpp # SQLite persistence implementation
├── tests/
├── meson.build
├── LICENSE
└── README.md
```

## Usage

### Basic In-Memory Usage

```cpp
#include <symspell/symspell.hpp>

using namespace yams::symspell;

// Create in-memory store
auto store = std::make_unique<MemoryStore>(2, 7);

// Create SymSpell instance
SymSpell spell(std::move(store), 2, 7);

// Add dictionary entries
spell.createDictionaryEntry("hello", 1000);
spell.createDictionaryEntry("world", 500);
spell.createDictionaryEntry("help", 100);

// Lookup suggestions
auto suggestions = spell.lookup("hellp", Verbosity::Closest);

for (const auto& s : suggestions) {
    std::cout << s.term << " (distance=" << s.distance 
              << ", freq=" << s.frequency << ")\n";
}
// Output: hello (distance=1, freq=1000)
```

### SQLite Persistence

```cpp
#include <symspell/symspell_sqlite.hpp>

sqlite3* db;
// ... open database ...

// Initialize schema
SQLiteStore::initializeDatabase(db);

// Create store
auto store = std::make_unique<SQLiteStore>(db, 2, 7);

// Create SymSpell
SymSpell spell(std::move(store), 2, 7);

// Add entries (persisted to SQLite)
spell.createDictionaryEntry("document", 1000);
spell.createDictionaryEntry("file", 800);

// Lookup works the same
auto suggestions = spell.lookup("documant", Verbosity::Closest);
```

## API Reference

### Enums

- `Verbosity::Top` - Return only the top suggestion
- `Verbosity::Closest` - Return all suggestions with minimum edit distance
- `Verbosity::All` - Return all suggestions within max edit distance

### Structures

```cpp
struct Suggestion {
    std::string term;      // Suggested term
    int distance;          // Edit distance from input
    int64_t frequency;     // Term frequency in dictionary
};
```

### Classes

#### ISymSpellStore (Abstract Interface)
```cpp
class ISymSpellStore {
    virtual void addDelete(int hash, std::string_view term) = 0;
    virtual std::vector<std::string> getTerms(int hash) = 0;
    virtual void setFrequency(std::string_view term, int64_t freq) = 0;
    virtual std::optional<int64_t> getFrequency(std::string_view term) = 0;
    virtual bool termExists(std::string_view term) = 0;
};
```

#### SymSpell
```cpp
class SymSpell {
    SymSpell(std::unique_ptr<ISymSpellStore> store,
             int maxEditDistance = 2,
             int prefixLength = 7);
    
    bool createDictionaryEntry(std::string_view key, int64_t count = 1);
    std::vector<Suggestion> lookup(std::string_view input,
                                    Verbosity verbosity = Verbosity::Closest,
                                    int maxEditDistance = -1);
};
```

## Integration with YAMS

To integrate into YAMS for fuzzy search:

1. Add to `third_party/symspell/` subdir in root `meson.build`
2. Include `<symspell/symspell.hpp>` and `<symspell/symspell_sqlite.hpp>`
3. Create `SQLiteStore` using the metadata database
4. Replace `HybridFuzzySearch` usage with `SymSpell::lookup()`

## Performance

| Metric | BK-tree | SymSpell |
|--------|---------|----------|
| Cold start | 100+ sec | <1 sec |
| Query time | ~5 ms | ~0.5 ms |
| Persistence | None | SQLite |

## References

- Original Algorithm: [SymSpell by Wolf Garbe](https://github.com/wolfgarbe/SymSpell)
- [Symmetric Delete Spelling Correction](https://seekstorm.com/blog/1000x-spelling-correction/)

## License

MIT License - see LICENSE file
