#!/usr/bin/env python3
"""Exercise AAC dependency retention through compilation, archiving and packaging."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
AAC_ASSETS = (
    'lib/libfdk-aac.so',
    'lib/fdk-aac-build.txt',
    'content/licenses/fdk-aac.txt',
)


class AacPackageTests(unittest.TestCase):
    def package(self, root, bundled=True, missing=None):
        build, runtime, commands, assets = (root / name for name in (
            'build', 'runtime', 'bin', 'assets'))
        for path in (build / 'content', runtime, commands, assets):
            path.mkdir(parents=True)
        binary = build / 'cobalt'
        binary.write_text('/web/adblock/adblockPreload.js\0' + (
            '$ORIGIN/lib/libfdk-aac.so' if bundled else ''))
        binary.chmod(0o755)
        for name in AAC_ASSETS:
            if bundled and name != missing:
                target = build / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('fixture: ' + name)
        for name in ('libstdc++.so.6', 'libgcc_s.so.1'):
            (runtime / name).write_text('fixture runtime')
        for name, text in (
                ('adblockMain.js', '__shorts'), ('adblockMain.css', 'body {}'),
                ('adblockPreload.js', '__ytafPreloadExecuted')):
            (assets / name).write_text(text)
        # Inspect the real staging tree at the external ares-package boundary.
        # No native executable or proprietary SDK is needed for these tests.
        packager = commands / 'ares-package'
        packager.write_text('#!' + sys.executable + '\n' + textwrap.dedent('''
            import json, os, sys
            from pathlib import Path
            stage = Path(sys.argv[-1])
            names = json.loads(os.environ['AAC_ASSETS'])
            report = {name: (stage / name).read_text() if (stage / name).exists()
                      else None for name in names}
            Path(os.environ['STAGE_REPORT']).write_text(json.dumps(report))
            sys.exit(91)
        '''))
        packager.chmod(0o755)
        report = root / 'stage.json'
        result = subprocess.run(
            ['bash', str(ROOT / 'scripts/package-starterless-cobalt.sh')],
            env=dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'],
                     COBALT_BUILD_DIR=str(build), COBALT_RUNTIME_DIR=str(runtime),
                     COBALT_PACKAGE_OUTPUT_DIR=str(root / 'output'),
                     WEBAPP_OUTPUT_DIR=str(assets), STAGE_REPORT=str(report),
                     AAC_ASSETS=json.dumps(AAC_ASSETS)),
            capture_output=True, text=True)
        return result, report

    def test_new_runtime_rejects_each_missing_dependency_asset(self):
        for name in AAC_ASSETS:
            with self.subTest(asset=name), tempfile.TemporaryDirectory() as temporary:
                result, report = self.package(Path(temporary), missing=name)
                self.assertEqual(result.returncode, 4, result.stdout + result.stderr)
                self.assertIn('Missing bundled AAC runtime asset:', result.stderr)
                self.assertIn(name, result.stderr)
                self.assertFalse(report.exists())

    def test_new_runtime_stages_library_provenance_and_notice(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, report = self.package(Path(temporary))
            self.assertEqual(result.returncode, 91, result.stdout + result.stderr)
            self.assertEqual(json.loads(report.read_text()), {
                name: 'fixture: ' + name for name in AAC_ASSETS})

    def test_historical_runtime_can_still_be_packaged(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, report = self.package(Path(temporary), bundled=False)
            self.assertEqual(result.returncode, 91, result.stdout + result.stderr)
            self.assertEqual(json.loads(report.read_text()), {
                name: None for name in AAC_ASSETS})

    def test_compile_copies_aac_only_after_success(self):
        script = (ROOT / 'scripts/build-starterless-cobalt-docker.sh').read_text()
        # Execute the actual container command with only compiler boundaries
        # replaced. This catches missing copies and copies after a failed build.
        command = script.split('  sh -c ', 1)[1].split(' \\\n  2>&1', 1)[0]
        for gn_exit, ninja_exit in ((0, 0), (17, 0), (0, 23)):
            with self.subTest(gn=gn_exit, ninja=ninja_exit), \
                    tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                bundle = root / 'out/fdk-aac-webos/bundle'
                bundle.mkdir(parents=True)
                for name in AAC_ASSETS:
                    (bundle / Path(name).name).write_text(name)
                shell = '''
                    out_dir=out/test
                    build_type=gold
                    parallel=2
                    git() { :; }
                    gn() { return "$GN_EXIT"; }
                    ninja() { return "$NINJA_EXIT"; }
                    sh() { eval "$2"; }
                '''
                result = subprocess.run(['bash', '-c', textwrap.dedent(shell) + 'sh -c ' + command],
                                        cwd=root, env=dict(os.environ, GN_EXIT=str(gn_exit),
                                                         NINJA_EXIT=str(ninja_exit)),
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, gn_exit or ninja_exit,
                                 result.stdout + result.stderr)
                for name in AAC_ASSETS:
                    target = root / 'out/test' / name
                    if gn_exit or ninja_exit:
                        self.assertFalse(target.exists())
                    else:
                        self.assertEqual(target.read_text(), name)

    def test_runtime_archive_retains_dependency_for_separate_packaging(self):
        workflow = (ROOT / '.github/workflows/build-starterless-cobalt.yml').read_text()
        step = workflow.split('      - name: Create Cobalt runtime archive\n', 1)[1]
        script = textwrap.dedent(step.split('        run: |\n', 1)[1].split(
            '\n      - name:', 1)[0])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cobalt = root / 'cobalt'
            build = cobalt / 'out/webos-arm-sbversion-13_gold'
            for name in ('cobalt', *AAC_ASSETS):
                target = build / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(name)
            (build / 'cobalt').chmod(0o755)
            (root / 'output').mkdir()
            result = subprocess.run(['bash', '-euc', script], cwd=root,
                                    env=dict(os.environ, COBALT_SOURCE_DIR=str(cobalt),
                                             BUILD_TYPE='gold', COBALT_SLUG='23.lts.6',
                                             SHORT_SHA='fixture', SOURCE_SHA='fixture',
                                             COBALT_REF='23.lts.6', GITHUB_REF_NAME='fixture',
                                             GITHUB_REPOSITORY='fixture/fixture',
                                             GITHUB_WORKSPACE=str(root),
                                             GITHUB_OUTPUT=str(root / 'github-output')),
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            archive, = (root / 'output').glob('*.tar.xz')
            with tarfile.open(archive) as runtime:
                for name in AAC_ASSETS:
                    self.assertEqual(runtime.extractfile(name).read().decode(), name)


if __name__ == '__main__':
    unittest.main()
