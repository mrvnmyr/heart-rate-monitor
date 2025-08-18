project('polarh9-recorder', 'cpp',
  version: '0.1.7',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20',
  ],
)

cpp = meson.get_compiler('cpp')

# Enforce Clang-only toolchain.
if cpp.get_id() != 'clang'
  error('This project currently supports **Clang** only.')
endif

# Keep Modules TS enabled for the module interface unit.
if cpp.has_argument('-fmodules-ts')
  add_project_arguments('-fmodules-ts', language: 'cpp')
endif

system = host_machine.system()
deps = []

if system != 'android'
  deps += dependency('libsystemd', required: true)
endif

# Compile the module interface with a custom_target using the chosen Clang.
# We only need an object; the entry TU avoids importing to skip BMI wiring.
mod_obj = custom_target(
  'polarh9.obj',
  input: 'polarh9.cppm',
  output: 'polarh9.o',
  command: [
    cpp.cmd_array(),
    '-std=c++20', '-fmodules-ts', '-O2', '-g',
    '-c', '@INPUT@', '-o', '@OUTPUT@'
  ],
  build_by_default: true
)

exe = executable(
  'polarh9',
  ['app.cppm', mod_obj],
  dependencies: deps,
  install: true
)

summary({
  'Target OS' : host_machine.system(),
  'C++ modules flag used' : cpp.has_argument('-fmodules-ts'),
  'Compiler' : cpp.get_id(),
})
