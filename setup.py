import os
import sys
import uwsgiconfig as uc
import shutil

from setuptools import setup
from setuptools.dist import Distribution
from setuptools.command.install import install
from setuptools.command.install_lib import install_lib
from setuptools.command.build_ext import build_ext

def get_profile():
    profile = os.environ.get('UWSGI_PROFILE','buildconf/default.ini')
    if not profile.endswith('.ini'):
        profile = "%s.ini" % profile
    if not '/' in profile:
        profile = "buildconf/%s" % profile

    return profile

def patch_bin_path(cmd, conf):

    bin_name = conf.get('bin_name')

    try:
        if not os.path.exists(cmd.install_scripts):
            os.makedirs(cmd.install_scripts)
        if not os.path.isabs(bin_name):
            print('Patching "bin_name" to properly install_scripts dir')
            conf.set('bin_name', os.path.join(cmd.install_scripts, conf.get('bin_name')))
    except:
        conf.set('bin_name', sys.prefix + '/bin/' + bin_name)


class uWSGIBuilder(build_ext):

    def run(self):
        conf = uc.uConf(get_profile())
        patch_bin_path(self, conf)
        uc.build_uwsgi( conf )


class uWSGIInstall(install):

    def run(self):

        conf = uc.uConf(get_profile())
        patch_bin_path(self, conf)
        uc.build_uwsgi( conf )
        install.run(self)

class uWSGIInstallLib(install_lib):

    def run(self):
        conf = uc.uConf(get_profile())
        patch_bin_path(self, conf)
        uc.build_uwsgi( conf )

class uWSGIDistribution(Distribution):

    def __init__(self, *attrs):
        Distribution.__init__(self, *attrs)
        self.cmdclass['install'] = uWSGIInstall
        self.cmdclass['install_lib'] = uWSGIInstallLib
        self.cmdclass['build_ext'] = uWSGIBuilder


print os.environ
print sys.argv
setup(name='uWSGI',
      version=uc.uwsgi_version,
      description='The uWSGI server',
      author='Unbit',
      author_email='info@unbit.it',
      url='http://projects.unbit.it/uwsgi/',
      license='GPL2',
      distclass = uWSGIDistribution,
     )

