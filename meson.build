project('polarh9-recorder', 'cpp',
  version: '0.1.2',
  default_options: [
    'warning_level=2',
    'optimization=2',
    'cpp_std=c++20'
  ],
)

cpp = meson.get_compiler('cpp')

# Enable C++20 Modules TS if supported
if cpp.has_argument('-fmodules-ts')
  add_project_arguments('-fmodules-ts', language: 'cpp')
endif

system = host_machine.system()
deps = []

if system != 'android'
  # libsystemd provides sd-bus (modern D-Bus API, not deprecated)
  deps += dependency('libsystemd', required: true)
endif

# IMPORTANT: put the module interface unit (.cppm) before importers.
# Meson (>=1.3) auto-detects module dependencies better with .cppm/.ixx.
exe = executable(
  'polarh9',
  ['polarh9.cppm', 'app.cpp'],
  dependencies: deps,
  install: true
)

summary({
  'Target OS' : system,
  'C++ modules flag used' : cpp.has_argument('-fmodules-ts'),
})
