from setuptools import setup, find_packages

setup(name='apricot',
      description='Apricot - Avocado SubFramwork',
      version=open("VERSION", "r").read().strip(),
      author='Apricot Developers',
      author_email='apricot-devel@example.com',
      packages=['apricot'],
      include_package_data=True,
      install_requires=['avocado-framework']
      )
