from distutils.core import setup, Extension

extension = Extension('wcp._prof',
                      sources=['prof.c'],
                      libraries=['rt'])

setup(name='wcp',
      scripts=['scripts/wcp'],
      ext_modules=[extension],
      packages=['wcp'])
