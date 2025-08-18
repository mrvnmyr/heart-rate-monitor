project('polarh9-recorder', 'cpp',
  version: '0.1.3',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20'
  ],
)

cpp = meson.get_compiler('cpp')

# Keep Modules TS enabled for the code that needs it.
if cpp.has_argument('-fmodules-ts')
  add_project_arguments('-fmodules-ts', language: 'cpp')
endif

system = host_machine.system()
deps = []

if system != 'android'
  deps += dependency('libsystemd', required: true)
endif

# Work around GCC module BMI ordering/scanner issues by compiling the
# module interface with a custom_target (no depfile flags).
mod_src = files('polarh9.cppm')
mod_obj = custom_target(
  'polarh9.obj',
  input: mod_src,
  output: 'polarh9.o',
  command: [
    cpp.get_exelist(),
    '-std=c++20', '-fmodules-ts', '-O2', '-g',
    '-c', '@INPUT@', '-o', '@OUTPUT@'
  ],
  build_by_default: true
)

exe = executable(
  'polarh9',
  ['app.cpp', mod_obj],
  dependencies: deps,
  install: true
)

summary({
  'Target OS' : host_machine.system(),
  'C++ modules flag used' : cpp.has_argument('-fmodules-ts'),
})
