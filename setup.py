#!/usr/bin/env python3
from setuptools import setup, find_packages

setup(
    name="bluetooth-jieli-research",
    version="1.0.0",
    description="Security analysis of Jieli-based Bluetooth audio devices",
    author="S.S. Sauron",
    url="https://github.com/SS-Sauron/Bluetooth-Jieli-Research",
    python_requires=">=3.8",
    packages=find_packages(),
    install_requires=[
        "pybluez>=0.23",
        "bleak>=0.21.0",
    ],
    extras_require={
        "dev": [
            "pylint>=2.15.0",
            "black>=22.0.0",
            "pytest>=7.0.0",
        ],
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Topic :: Security",
    ],
)
