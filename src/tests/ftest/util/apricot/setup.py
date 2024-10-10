''' apricot setup '''
from setuptools import setup

setup(name='apricot',
      description='Apricot - Avocado SubFramwork',
      # pylint: disable-next=consider-using-with
      version=open("VERSION", "r").read().strip(),
      author='Apricot Developers',
      author_email='apricot-devel@example.com',
      packages=['apricot'],
      include_package_data=True,
      install_requires=['avocado-framework']
      )
