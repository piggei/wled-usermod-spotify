#!/usr/bin/env python3
"""Compile and run the actual dev.2n-r20 header-only EOS policy."""
from pathlib import Path
import shutil, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
header = root / 'spotify/SpotifySpircEosPolicy.h'
assert header.exists()

src = r'''
#include <cassert>
#include <cstddef>
#include "spotify/SpotifySpircEosPolicy.h"
using namespace spotify_spirc_eos;

static Input base() {
  Input i;
  i.localActive = true;
  i.clockRunning = true;
  i.playStatus = 1u;
  i.positionMs = 100000u;
  i.durationMs = 100000u;
  i.trackIndex = 2u;
  i.trackCount = 10u;
  i.repeat = false;
  i.generation = 7u;
  i.handledGeneration = 0u;
  return i;
}

int main() {
  { Input i{true, true, 1u, 100000u, 100000u, 2u, 10u, false, 7u, 0u}; assert(decide(i)==Action::Advance); }
  { auto i=base(); assert(decide(i)==Action::Advance); }
  { auto i=base(); i.positionMs=99999u; assert(decide(i)==Action::None); }
  { auto i=base(); i.clockRunning=false; assert(decide(i)==Action::None); }
  { auto i=base(); i.playStatus=2u; assert(decide(i)==Action::None); }
  { auto i=base(); i.localActive=false; assert(decide(i)==Action::None); }
  { auto i=base(); i.durationMs=0u; assert(decide(i)==Action::None); }
  { auto i=base(); i.generation=0u; assert(decide(i)==Action::None); }
  { auto i=base(); i.handledGeneration=7u; assert(decide(i)==Action::None); }
  { auto i=base(); i.repeat=true; assert(decide(i)==Action::HoldRepeat); }
  { auto i=base(); i.trackIndex=9u; assert(decide(i)==Action::HoldBoundary); }
  { auto i=base(); i.trackCount=0u; assert(decide(i)==Action::HoldBoundary); }
  assert(std::string(actionName(Action::Advance)) == "advance");
  assert(std::string(actionName(Action::HoldRepeat)) == "repeat-held");
  assert(std::string(actionName(Action::HoldBoundary)) == "queue-boundary");
  return 0;
}
'''
# Need string.
src = src.replace('#include <cstddef>', '#include <cstddef>\n#include <string>')

compilers = [c for c in ('g++','clang++') if shutil.which(c)]
assert compilers, 'no host C++ compiler found'
with tempfile.TemporaryDirectory(prefix='r20eos-') as td:
  td = Path(td)
  cpp = td/'test.cpp'; cpp.write_text(src)
  for compiler in compilers:
    for std in ('c++11','c++17'):
      exe = td/(Path(compiler).name + '-' + std + '.bin')
      subprocess.run([compiler, '-std=' + std, '-Wall', '-Wextra', '-Werror', '-I', str(root), str(cpp), '-o', str(exe)], check=True)
      subprocess.run([str(exe)], check=True)
print('r20 EOS policy native PASS (C++11/C++17):', ', '.join(compilers))
