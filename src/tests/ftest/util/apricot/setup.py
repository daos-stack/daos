''' apricot setup '''
from setuptools import setup

setup(name='apricot',
      description='Apricot - Avocado SubFramwork',
      version=open("VERSION", "r").read().strip(),
      author='Apricot Developers',
      author_email='apricot-devel@example.com',
      packages=['apricot'],
      include_package_data=True,
      install_requires=['avocado-framework']
      )
