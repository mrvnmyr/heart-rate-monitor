project('polarh9-recorder', 'cpp',
  version: '0.1.1',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20'
  ],
)

cpp = meson.get_compiler('cpp')

# Try to enable C++20 modules TS if supported (harmless if not)
if cpp.has_argument('-fmodules-ts')
  add_project_arguments('-fmodules-ts', language: 'cpp')
endif

system = host_machine.system()
deps = []

if system != 'android'
  # libsystemd provides sd-bus (modern D-Bus API, not deprecated)
  deps += dependency('libsystemd', required: true)
endif

exe = executable(
  'polarh9',
  ['app.cpp', 'polarh9.cpp'],
  dependencies: deps,
  install: true
)

summary({
  'Target OS' : system,
  'C++ modules flag used' : cpp.has_argument('-fmodules-ts'),
})
