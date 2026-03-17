import shutil
from pathlib import Path

from milc import cli

from qmk import svn


@cli.argument('--check', arg_only=True, action='store_true', help='Check if the SVN dependencies are up to date.')
@cli.argument('--sync', arg_only=True, action='store_true', help='Checkout any missing SVN dependencies.')
@cli.argument('-f', '--force', action='store_true', help='Remove and re-checkout all SVN dependencies.')
@cli.subcommand('SVN dependency actions.')
def svn_checkout(cli):
    """Manage SVN repository dependencies.
    """
    if cli.args.check:
        return all(item['status'] for item in svn.status().values())

    if cli.args.sync:
        for name, item in svn.status().items():
            if item['status'] is None:
                svn.update(name)
        return True

    if cli.config.svn_checkout.force:
        for name, item in svn.status().items():
            dep_path = Path(name)
            if dep_path.is_dir():
                print(f"Removing '{name}'")
                shutil.rmtree(dep_path)

    svn.update()
