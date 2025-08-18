project('polarh9-recorder', 'cpp',
  version: '0.1.11',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20',
  ],
)

system = host_machine.system()

# libsystemd is only required on non-Android builds.
deps = []
if system != 'android'
  libsd = dependency('libsystemd', required: true)
  deps += [libsd]
endif

# Keep debug info by default.
add_project_arguments('-g', language: 'cpp')

# Build the single TU normally so Meson picks the right compiler
# (NDK triplet for cross, host compiler for native).
exe = executable(
  'polarh9',
  'main.cpp',
  dependencies: deps,
  install: true,
  install_dir: get_option('bindir'),
)

summary({
  'Target OS' : system,
  'Dependencies' : deps,
})
