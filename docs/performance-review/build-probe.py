"""Build the diagnostic against existing Release benchmark objects; change no plugin files."""
import shlex
import subprocess
import tempfile
from pathlib import Path

source = Path(__file__).resolve().with_name('playback-probe.cpp')
root = source.parents[2] / 'Builds-native-release'
output = Path(tempfile.mkdtemp(prefix='ksh-performance-review-'))
subprocess.run(['cmake', '--build', str(root), '--target', 'Benchmarks'], check=True)
lines = subprocess.check_output(
    ['ninja', '-C', str(root), '-t', 'commands', 'Benchmarks'], text=True
).splitlines()
compile_args = shlex.split(next(
    line for line in lines if ' -c ' in line and '/benchmarks/Catch2Main.cpp' in line
))
for flag, replacement in {
    '-MT': output / 'probe.o', '-MF': output / 'probe.d',
    '-o': output / 'probe.o', '-c': source,
}.items():
    compile_args[compile_args.index(flag) + 1] = str(replacement)
subprocess.run(compile_args, cwd=root, check=True)
link = next(line for line in lines if ' -o Benchmarks ' in line).split(' && ')[1]
args = [str(output / 'probe.o') if arg == 'CMakeFiles/Benchmarks.dir/benchmarks/Catch2Main.cpp.o' else arg
        for arg in shlex.split(link)]
args[args.index('-o') + 1] = str(output / 'probe')
subprocess.run(args, cwd=root, check=True)
subprocess.run([str(output / 'probe')], check=True)
