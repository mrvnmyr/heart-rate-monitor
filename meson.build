project('polarh9-recorder', 'cpp',
  version: '0.1.10',
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

# Clang's C++20 modules do NOT use '-fmodules-ts' (that was a GCC flag).
common_flags = ['-std=c++20', '-O2', '-g']

# Build the module implementation object with clang++
mod_obj = custom_target(
  'polarh9.obj',
  input: 'polarh9.cppm',
  output: 'polarh9.o',
  command: [clangpp] + common_flags + [
    '-c', '@INPUT@', '-o', '@OUTPUT@'
  ],
  build_by_default: true
)

# Build the entry TU (module interface with main) with clang++
app_obj = custom_target(
  'app.obj',
  input: 'app.cppm',
  output: 'app.o',
  command: [clangpp] + common_flags + [
    '-c', '@INPUT@', '-o', '@OUTPUT@'
  ],
  build_by_default: true
)

# Link the final binary with clang++; add -lsystemd on Linux hosts.
link_args = []
if system != 'android'
  link_args += ['-lsystemd']
endif

bin = custom_target(
  'polarh9',
  input: [app_obj, mod_obj],
  output: 'polarh9',
  command: [clangpp, '@INPUT@', '-o', '@OUTPUT@'] + link_args,
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
