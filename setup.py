from distutils.core import setup, Extension

extension = Extension('wcp._wcp',
                      sources=['wcp.c'],
                      extra_compile_args=['-O0'],
                      libraries=['rt'])

setup(name='wcp',
      scripts=['scripts/wcp'],
      ext_modules=[extension],
      packages=['wcp'])
