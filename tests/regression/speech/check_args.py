"""Compare native argument parsing with the subprocess launcher's arguments."""
import subprocess
import sys

cases = [[], ["train", "manifest.tsv", "checkpoint.tns", "20", "1000"],
         ["", "a b", "a\tb", 'a"b', "tail\\", 'a\\\"b', '""', "héllo 世界"],
         ["\\", "\\\\", "a b\\", 'a\\\\"b', " leading", "trailing "]]
for arguments in cases:
    process = subprocess.run([sys.argv[1], *arguments], capture_output=True, timeout=10)
    assert process.returncode == 0, process.stderr
    expected = [str(len(arguments)), *(item.encode("utf-8").hex() for item in arguments)]
    assert process.stdout.decode("ascii").splitlines() == expected, (arguments, process.stdout, expected)
print("native_args_pass", len(cases))
