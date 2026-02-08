# SymSpell fuzzy search library for YAMS
# This file is designed to be included via subdir() from the parent project

# SQLite3 dependency - inherit from parent or find it ourselves
if not is_variable('sqlite3_dep')
  sqlite3_dep = dependency('sqlite3', version: '>=3.38.0', required: false, method: 'pkg-config')
  if not sqlite3_dep.found()
    sqlite3_dep = dependency('unofficial-sqlite3', required: false, method: 'cmake', modules: ['unofficial::sqlite3::sqlite3'])
  endif
  if not sqlite3_dep.found()
    sqlite3_dep = dependency('sqlite3', version: '>=3.38.0', required: false, method: 'cmake')
  endif
  if not sqlite3_dep.found()
    sqlite3_dep = dependency('sqlite3', version: '>=3.38.0', required: true)
  endif
endif

# Project include directories
symspell_inc = include_directories('include')

# SQLite persistence layer (only this needs to be compiled)
symspell_sqlite_lib = static_library(
  'yams_symspell_sqlite',
  [files('src/symspell_sqlite.cpp')],
  include_directories: [symspell_inc],
  dependencies: [sqlite3_dep],
  install: false,
)

# Combined dependency for consumers
symspell_dep = declare_dependency(
  include_directories: symspell_inc,
  link_with: symspell_sqlite_lib,
  dependencies: [sqlite3_dep],
)

message('SymSpell library configured')

# Build tests/benchmarks if explicitly enabled or in debug mode
# Use the same logic as the parent project
_symspell_tests_opt = get_option('build-tests')
_symspell_buildtype = get_option('buildtype')
_symspell_tests_enabled = _symspell_tests_opt or (_symspell_buildtype == 'debug')

if _symspell_tests_enabled
  subdir('tests')
  subdir('benchmarks')
endif
