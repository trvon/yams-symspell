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
)

symspell_bench_timeout = get_option('bench-timeout')
if get_option('enable-tsan') or get_option('enable-asan')
  if symspell_bench_timeout < 600
    symspell_bench_timeout = 600
  endif
elif symspell_bench_timeout < 180
  symspell_bench_timeout = 180
endif

test('symspell_bench', symspell_bench_exe,
  is_parallel: false,
  timeout: symspell_bench_timeout,
)
