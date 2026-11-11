#!/usr/bin/env python3
"""Exercise the actual FOC sensor adapter after an encoder transport failure."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'Drivers/libvoltbro/voltbro/motors/bldc/foc/foc.cpp').read_text()
signature = 'bool FOC::update_sensors()'
start = source.index(signature)
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[opening:end]
assert 'if (!update_sensors()) { stop(); return; }' in source
program = r'''
#include <cassert>
#define VB_PROFILE_DETAIL_BEGIN()
#define VB_PROFILE_DETAIL_SPLIT(x)
#define VB_PROFILE_DETAIL_END(x,y)
struct Inverter { void update() {} };
struct FOC {
  Inverter inverter;
  bool ok = true; int filter = 0, shaft = 0;
  bool update_angle() { return ok; }
  void apply_kalman() { ++filter; }
  void update_shaft_angle() { ++shaft; }
  bool update_sensors() BODY
};
int main() {
  FOC f;
  f.ok = false; assert(!f.update_sensors()); assert(f.filter == 0 && f.shaft == 0);
  f.ok = true; assert(f.update_sensors()); assert(f.filter == 1 && f.shaft == 1);
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(dir='/tmp/opencode') as d:
    cpp, exe = Path(d)/'test.cpp', Path(d)/'test'
    cpp.write_text(program)
    subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Actual FOC sensor adapter aborts before filter on transport error: PASS')
