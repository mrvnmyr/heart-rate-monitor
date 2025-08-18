project('polarh9-recorder', 'cpp',
  version: '0.1.11',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20',
  ],
)

# Always use clang++ to build & link, regardless of Meson's detected C++ compiler.
clangpp = find_program('clang++', required: true)

system = host_machine.system()

# Ensure libsystemd exists on non-Android, but keep args simple & portable.
if system != 'android'
  libsd = dependency('libsystemd', required: true)
endif

common_flags = ['-std=c++20', '-O2', '-g']

# Link the final binary with clang++; add -lsystemd on Linux hosts.
link_args = []
if system != 'android'
  link_args += ['-lsystemd']
endif

# Single-translation-unit build: compile and link in one step via a custom target.
bin = custom_target(
  'polarh9',
  input: 'main.cpp',
  output: 'polarh9',
  command: [clangpp] + common_flags + ['@INPUT@', '-o', '@OUTPUT@'] + link_args,
  install: true,
  install_dir: get_option('bindir'),
  build_by_default: true
)

summary({
  'Target OS' : system,
  'Compiler forced' : 'clang++',
  'Clang path' : clangpp.full_path(),
  'Compile flags' : common_flags,
  'Link args' : link_args,
})
