from requests import get
import json
import subprocess
import inspect
import os
import logging

OWNER = 'XRPLF'
REPO = 'rippled'
DEFAULT_BRANCH = 'develop'
URL = f"https://api.github.com/repos/{OWNER}/{REPO}"
RIPPLED = "/opt/ripple/bin/rippled"
BRANCH_URL = f"commits/heads/{DEFAULT_BRANCH}"
TAG = "1.11.0"
TAGS_URL = f"commits/tags/{TAG}"
debug_rippled = "/home/emel/dev/Ripple/rippled/build/develop-debug/rippled"
release_rippled = "/home/emel/dev/Ripple/rippled/build/FLR/rippled"
rippleds = [debug_rippled, release_rippled]


def is_repo_current(repo_path: str, branch: str) -> bool:
    """
    TODO: branch param is unused
    Currently only checks if the currently checked out branch is up to date, which may be undesired.
    At least print that that is what is being checked.

    """
    check_repo_command = "git fetch --dry-run".split()
    try:
        os.chdir(repo_path)
        try:
            current = not subprocess.check_output(check_repo_command, stderr=subprocess.STDOUT, encoding='utf-8')
            dir_name = os.path.basename(repo_path)
            if current:
                logging.info(f"repo in {dir_name} current!")
            else:
                logging.info(f"repo in {dir_name} needs update!")
            return current
        except subprocess.CalledProcessError as e:
            logging.info(f"{repo_path} isn't a git repo!\n{e}")
            logging.debug(e)
    except FileNotFoundError as e:
        logging.info(f"{repo_path} isn't a valid path!")
        logging.debug(e)


def get_version(rippled: str = release_rippled) -> str:
    """
    returns the tag if release build, git ref if debug build
    TODO: Doesn't tell if debug or release
    if DEBUG, doesn't tell version #, only git ref
    """
    output = subprocess.check_output([rippled, '--version'],  encoding='utf-8').strip()
    version_string = output.split('version')[-1].strip()
    version = version_string.rsplit('.', 1)[0].split("+")[1] if version_string.endswith('.DEBUG') else version_string
    return version


def get_last_commit(owner: str = OWNER, repo: str = REPO, branch: str = DEFAULT_BRANCH, tag: str = None) -> str:
    """
    Gets he last commit from default branch or given ref
    BUG: broken!
    TODO: owner/repo as params; currenly only checks

    """
    url = f"https://api.github.com/repos/{owner}/{repo}"

    # f = inspect.currentframe().f_code.co_name
    # print(inspect.signature(__name__))
    try:
        branch and tag
    except:
        print("Can't provide branch _and_ tag!")

    try:
        response = get(url+"/tags")
    except:
        pass

    if tag:
        url = f"{url}/{TAGS_URL}/{tag}"
    elif branch:
        url = f"{url}/{BRANCH_URL}/{branch}"

    try:
        # print(f"Getting URL: {url}")
        breakpoint()
        response = get(url).json()
        return response['sha']
    except Exception as e:
        print(f"whoops!\n{repr(e)}")
        exit(1)


# for repo in ['rippled', 'clio', 'butts', 'neato']:
#     repo_path=f"/home/emel/dev/Ripple/{repo}"
#     if is_repo_current(repo_path):
#         print(f"Repo @ {repo} is current.")
#     else:
#         print(f"Repo @ {repo} is nota current.")

# for rippled in rippleds:
#     version = get_version(rippled)
#     if '.' in version:
#         commit = get_last_commit(branch=version)
#     else:
#         commit = version
#     print(f"Last commit for {version}: {commit}")

# def test_get_last_commit():
#     real_last_commit = '3c9db4b69efb8f8e5382e4dfed5927ef28a303a3'
#     assert commit == real_last_commit, "Commit wrong!"
