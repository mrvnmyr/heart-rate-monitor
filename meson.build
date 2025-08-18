project('polarh9-recorder', 'cpp',
  version: '0.1.4',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20',
    # Work around GCC modules depfile bug: disable depfiles globally.
    'b_depfiles=false',
  ],
)

cpp = meson.get_compiler('cpp')

# Keep Modules TS enabled for the module interface unit.
if cpp.has_argument('-fmodules-ts')
  add_project_arguments('-fmodules-ts', language: 'cpp')
endif

system = host_machine.system()
deps = []

if system != 'android'
  deps += dependency('libsystemd', required: true)
endif

# Build normally; depfiles are disabled so GCC won't trip on modules.
exe = executable(
  'polarh9',
  ['polarh9.cppm', 'app.cpp'],
  dependencies: deps,
  install: true
)

summary({
  'Target OS' : host_machine.system(),
  'C++ modules flag used' : cpp.has_argument('-fmodules-ts'),
  'Depfiles disabled' : get_option('b_depfiles'),
})
