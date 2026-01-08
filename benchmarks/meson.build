# SymSpell benchmarks
# Only built when tests are enabled in parent project

yams_core_dep = dependency('yams_core', required: false)
sqlite_vec_cpp_dep = dependency('sqlite-vec-cpp', required: false)

symspell_bench_exe = executable(
  'symspell_bench',
  'symspell_bench.cpp',
  include_directories: symspell_inc,
  dependencies: [
    sqlite3_dep,
    yams_core_dep,
    sqlite_vec_cpp_dep,
  ],
  link_with: symspell_sqlite_lib,
  cpp_args: ['-O3', '-DNDEBUG'],
)

test('symspell_bench', symspell_bench_exe, is_parallel: false)
