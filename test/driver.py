#!/usr/bin/env python
import dataclasses
import enum
import logging
import pathlib
import re
import shlex
import subprocess


logger = logging.getLogger(__name__)

class ExpectedResult(enum.Enum):
    Success = 0
    Fail = 1


def main():
    import argparse
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("--h2incc", type=pathlib.Path, required=True, help="path of h2incc")
    parser.add_argument("--iniconfig", type=pathlib.Path, required=True, help="path to ini config")
    parser.add_argument("--case", type=pathlib.Path, required=True, help="path of test case")
    parser.add_argument("--loglevel", type=int, default=logging.WARNING, help="logging level")
    parser.add_argument("--update", action="store_true", help="update reference file (if applicable)")
    args = parser.parse_args()

    logging.basicConfig(level=args.loglevel)

    h2incc_args = []
    expected = ExpectedResult.Success
    reference_path = None

    found_driver_spec = False

    with args.case.open() as f:
        while line := f.readline():
            m = re.match(r"^//\W+driver:\W*(.*)\n", line)
            if not m:
                break
            found_driver_spec = True
            key, value = m.group(1).split("=", 1)
            key, value = key.strip(), value.strip()
            if key == "args":
                value = value.replace("%INICONFIG%", f"'{args.iniconfig}'")
                h2incc_args = shlex.split(value)
            elif key == "expected":
                expected = {k.lower():ExpectedResult[k] for k in ExpectedResult.__members__}[value]
            elif key == "reference":
                reference_path = args.case.parent / value
            else:
                raise ValueError(key)

    if not found_driver_spec:
        raise ValueError(f"{args.case} does not start with a test driver specification")


    if expected == ExpectedResult.Success and not reference_path:
        raise ValueError("Success state requires reference file")

    cmd = [str(args.h2incc), str(args.case)] + h2incc_args

    logger.info("expected: %r", expected)
    logger.info("reference_path: %r", reference_path)
    logger.info("cmd: `%s`", shlex.join(cmd))

    result = subprocess.run(cmd, capture_output=True, text=True)
    if expected == ExpectedResult.Success and result.returncode != 0:
        raise ValueError(f"return code was {result.returncode}, expected 0")

    logger.debug("output=%r", result.stdout)

    if reference_path:
        if args.update:
            with reference_path.open("w") as f:
                f.write(result.stdout)

        with reference_path.open() as f:
            reference = f.read()
        logger.debug("reference=%r", reference)
        if result.stdout != reference:
            raise ValueError


if __name__ == "__main__":
    raise SystemExit(main())
