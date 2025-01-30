project('embeddedTS Examples', 'c', version: '1.0.0',
  default_options : [
    'warning_level=3',
  ]
)
add_project_arguments('-DRELEASE="' + meson.project_version() + '"', language: 'c')

# Get enable options
can_opt = get_option('can')

# Check if any number of specific tools are enabled
any_explicit = can_opt.enabled()

# If no specific tools are enabled, build all, otherwise, only build what is enabled
tool_enabled = {
  'can': can_opt.enabled() or (can_opt.auto() and not any_explicit),
}

# Add subdirectories of tools based on what is enabled
if tool_enabled['can']
  subdir('src/can')
endif
