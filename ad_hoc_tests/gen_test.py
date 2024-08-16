#!/Users/manchineel/.pyenv/shims/python
import os
import sys
import subprocess

script_dir = os.path.dirname(os.path.realpath(__file__))
platform = sys.platform

USAGE_NOTE = "Usage: gen_test.py <test|retest> <test_name> [arg1] [arg2] ..."
BIN_MATRIX = {
    "darwin": {
        "test": "test_gen_2024_macos",
        "sol": "sol_2024_macos",
    },
    "linux": {
        "test": "test_gen_2024_linux",
        "sol": "sol_2024_linux",
    }
}

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(USAGE_NOTE)
        sys.exit(1)

    test_name = os.path.join(script_dir, f"test_case_{sys.argv[2]}.txt")
    extra_args = sys.argv[3:]

    match sys.argv[1]:
        case "test":
            with open(os.path.join(script_dir, test_name), "w") as f:
                subprocess.run([os.path.join(script_dir, BIN_MATRIX[platform]["test"])] + extra_args, stdout=f)
        case "retest":
            pass
        case _:
            print(USAGE_NOTE)
            sys.exit(1)

    solution_name = os.path.join(script_dir, f"test_case_{sys.argv[2]}_solution.txt")
    output_name = os.path.join(script_dir, f"test_case_{sys.argv[2]}_output.txt")

    with (
        open(test_name, "r") as f1,
        open(test_name, "r") as f2,
        open(solution_name, "w") as sol,
        open(output_name, "w") as out,
    ):
        solution_job = subprocess.Popen([os.path.join(script_dir, BIN_MATRIX[platform]["sol"])], stdin=f1, stdout=sol)
        test_job = subprocess.Popen(["./trie_test"], stdin=f2, stdout=out)
        solution_job.wait()
        test_job.wait()

    subprocess.run(["diff", "-u", output_name, solution_name])
    subprocess.run(["code", "-d", output_name, solution_name])  # for VS Code
