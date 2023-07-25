import os

path = '/tmp/flr-data/lib/rippled/db/'

def get_dir_size(path='.'):
    total = 0
    with os.scandir(path) as it:
        for entry in it:
            if entry.is_file():
                total += entry.stat().st_size
            elif entry.is_dir():
                total += get_dir_size(entry.path)
    return total

def func(x,*, more=None):
    print(x)
    print(more)

print(f"{(get_dir_size(path)/2**20):.2f}")
