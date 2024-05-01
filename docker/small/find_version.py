import re

build_info = "src/ripple/protocol/impl/BuildInfo.cpp"

def get_version_from_file(filename: str = build_info):
    pattern = r'.*String = "(.*)"\n$'
    with open(filename, 'r') as infile:
        for line in infile:
            if m := re.match(pattern, line):
                return m.group(1)

def parse_version(version):
    pre = None
    if "-" in version:
        version, pre = version.split("-")
    major, minor, patch = version.split(".")
    return major, minor, patch, pre

if __name__ == "__main__":
    import sys
    version = get_version_from_file(sys.argv[1])
    major, minor, patch, pre = parse_version(version)
    print(f"Version {version}")
    if pre:
        print(f"Prerelease: {pre}")
    print(f"{major=}")
    print(f"{minor=}")
    print(f"{patch=}")
    print(version)
