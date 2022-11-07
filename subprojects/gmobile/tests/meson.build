if get_option('tests')

test_env = [
  'G_DEBUG=gc-friendly,fatal-warnings',
  'MALLOC_CHECK_=2',
  'NO_AT_BRIDGE=1',
]

timeout_test_sources = [
  'test-timeout.c',
]

timeout_test = executable(
  'timeout', timeout_test_sources,
  pie: true,
  link_with : gm_lib,
  dependencies : libgmobile_dep,
)

test ('timeout', timeout_test, env: test_env)

endif
