"""Functions for working with QMK's SVN dependencies.
"""
import json
from pathlib import Path

from milc import cli


def _load_config():
    """Load the .svnmodules configuration file.

    Returns a dict mapping paths to their SVN dependency configuration:

        {
            'lib/some_dep': {
                'url': 'https://svn.example.com/repo/trunk',
                'revision': 12345  # or None for HEAD
            }
        }
    """
    config_path = Path('.svnmodules')
    if not config_path.exists():
        return {}

    with config_path.open() as f:
        return json.load(f)


def status():
    """Returns a dictionary of SVN dependencies and their status.

    Each entry is a dict of the form:

        {
            'name': 'dependency_path',
            'status': None/False/True,
            'url': 'svn://...',
            'revision': 12345,
            'current_revision': 12345,
            'pinned_revision': 12345 or None
        }

    status is None when the checkout doesn't exist, False when it's out of date, and True when it's current.
    """
    config = _load_config()
    dependencies = {}

    for path, dep in config.items():
        entry = {
            'name': path,
            'url': dep['url'],
            'pinned_revision': dep.get('revision'),
            'status': None,
            'current_revision': None,
        }

        checkout_path = Path(path)
        if not checkout_path.exists():
            dependencies[path] = entry
            continue

        # Get info about the working copy
        svn_info = cli.run(['svn', 'info', '--show-item', 'revision', path], timeout=30)
        if svn_info.returncode != 0:
            dependencies[path] = entry
            continue

        current_rev = int(svn_info.stdout.strip())
        entry['current_revision'] = current_rev

        if dep.get('revision') is not None:
            # Pinned to a specific revision
            entry['status'] = current_rev == dep['revision']
        else:
            # Check if working copy is at HEAD
            svn_remote = cli.run(['svn', 'info', '--show-item', 'revision', dep['url']], timeout=30)
            if svn_remote.returncode == 0:
                head_rev = int(svn_remote.stdout.strip())
                entry['status'] = current_rev == head_rev
            else:
                # Can't reach remote, assume current
                entry['status'] = True

        dependencies[path] = entry

    return dependencies


def update(dependencies=None):
    """Update SVN dependencies.

        dependencies
            A string containing a single dependency path, a list of paths,
            or None to update all.
    """
    config = _load_config()

    if not config:
        cli.log.info('No SVN dependencies configured in .svnmodules.')
        return

    if dependencies is None:
        targets = config.items()
    elif isinstance(dependencies, str):
        targets = [(dependencies, config[dependencies])]
    else:
        targets = [(d, config[d]) for d in dependencies]

    for path, dep in targets:
        _update_one(path, dep)


def _update_one(path, dep):
    """Checkout or update a single SVN dependency.
    """
    checkout_path = Path(path)
    url = dep['url']
    revision = dep.get('revision')

    rev_args = ['-r', str(revision)] if revision is not None else []
    rev_display = f'@{revision}' if revision is not None else '@HEAD'

    if checkout_path.exists() and (checkout_path / '.svn').exists():
        # Already checked out, update
        cli.log.info(f'Updating {path} {rev_display}')
        cmd = ['svn', 'update', *rev_args, path]
        cli.run(cmd, capture_output=False, check=True)
    else:
        # Fresh checkout
        checkout_path.parent.mkdir(parents=True, exist_ok=True)
        cli.log.info(f'Checking out {url} -> {path} {rev_display}')
        cmd = ['svn', 'checkout', *rev_args, url, path]
        cli.run(cmd, capture_output=False, check=True)
