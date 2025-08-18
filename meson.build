project('polarh9-recorder', 'cpp',
  version: '0.1.8',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20',
  ],
)

# Always use clang++ to build & link, regardless of Meson's detected C++ compiler.
clangpp = find_program('clang++', required: true)

system = host_machine.system()
libsd_cargs = []
libsd_link = []
if system != 'android'
  libsd = dependency('libsystemd', required: true)
  libsd_cargs = libsd.get_compile_args()
  libsd_link = libsd.get_link_args()
endif

common_flags = ['-std=c++20', '-fmodules-ts', '-O2', '-g']

# Build the module implementation object with clang++
mod_obj = custom_target(
  'polarh9.obj',
  input: 'polarh9.cppm',
  output: 'polarh9.o',
  command: [clangpp] + common_flags + libsd_cargs + [
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

# Link the final binary with clang++
bin = custom_target(
  'polarh9',
  input: [app_obj, mod_obj],
  output: 'polarh9',
  command: [clangpp, '@INPUT@', '-o', '@OUTPUT@'] + libsd_link,
  install: true,
  install_dir: get_option('bindir'),
  build_by_default: true
)

summary({
  'Target OS' : system,
  'Compiler forced' : 'clang++',
  'Clang path' : clangpp.full_path(),
  'Using libsystemd' : system != 'android',
})
