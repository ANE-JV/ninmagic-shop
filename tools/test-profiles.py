#!/usr/bin/env python3
"""Exercise production profile editing and actual minIni persistence on host."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parent.parent
def path_for(compiler,path):
    value=Path(path).resolve().as_posix()
    if sys.platform=='win32' and '/usr/bin/' in compiler.replace('\\','/').lower() and len(value)>2 and value[1]==':':
        return '/'+value[0].lower()+value[2:]
    return value
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--minini',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'.local/profile-tests')
    parser.add_argument('--cc',default=shutil.which('gcc') or shutil.which('clang'))
    parser.add_argument('--cxx',default=shutil.which('g++') or shutil.which('clang++'))
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    if not args.cc or not args.cxx:parser.error('Host C and C++ compilers are required')
    include=args.minini/'include';obj=args.output/'minIni.o';exe=args.output/('profile-test.exe' if sys.platform=='win32' else 'profile-test')
    subprocess.run([args.cc,'-std=c99','-O1','-include','strings.h','-DMININI_USE_NX=0','-DMININI_USE_STDIO=1','-I',path_for(args.cc,include),'-c',path_for(args.cc,args.minini/'source/minIni.c'),'-o',path_for(args.cc,obj)],check=True)
    subprocess.run([args.cxx,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-I',path_for(args.cxx,ROOT/'sphaira/include'),'-I',path_for(args.cxx,include),path_for(args.cxx,ROOT/'tools/tests/test_ownfoil_profile.cpp'),path_for(args.cxx,obj),'-o',path_for(args.cxx,exe)],check=True)
    with tempfile.TemporaryDirectory(prefix='ninmagic-shop-profile-test-') as directory:
        subprocess.run([str(exe.resolve()),path_for(args.cxx,Path(directory)/'profiles.ini')],check=True)
if __name__=='__main__':main()
